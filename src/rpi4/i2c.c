#include "rpi4/i2c.h"
#include "rpi4/mmio.h"
#include "rpi4/gpio.h"
#include "rpi4/uart.h"
#include "kernel/irq.h"
#include "kernel/sched/scheduler.h"

/*
 * Interrupt-driven I2C driver.
 *
 * Transfers are executed asynchronously in the IRQ handler while the
 * calling task blocks. Completion is signaled via task_wakeup().
 *
 * Only a single transfer is supported at a time.
 */

#define PERIPHERAL_BASE ((uintptr_t)0xFE000000)

/*
 * BSC1 / I2C1 on Raspberry Pi GPIO header:
 *   GPIO2 = SDA1
 *   GPIO3 = SCL1
 */
#define BSC1_BASE (PERIPHERAL_BASE + 0x804000)

#define BSC_C (BSC1_BASE + 0x00)
#define BSC_S (BSC1_BASE + 0x04)
#define BSC_DLEN (BSC1_BASE + 0x08)
#define BSC_A (BSC1_BASE + 0x0C)
#define BSC_FIFO (BSC1_BASE + 0x10)
#define BSC_DIV (BSC1_BASE + 0x14)
#define BSC_DEL (BSC1_BASE + 0x18)
#define BSC_CLKT (BSC1_BASE + 0x1C)

/* Control register bits */
#define BSC_C_I2CEN (1u << 15)
#define BSC_C_ST (1u << 7)
#define BSC_C_CLEAR_FIFO (2u << 4)
#define BSC_C_READ (1u << 0)

/* Interrupt enable bits */
#define BSC_C_INTR (1u << 10)
#define BSC_C_INTT (1u << 9)
#define BSC_C_INTD (1u << 8)

/* Status register bits */
#define BSC_S_CLKT (1u << 9)
#define BSC_S_ERR (1u << 8)
#define BSC_S_RXD (1u << 5)
#define BSC_S_TXD (1u << 4)
#define BSC_S_DONE (1u << 1)
#define BSC_S_TA (1u << 0)

/* Clear-on-write status bits */
#define BSC_S_CLEAR (BSC_S_CLKT | BSC_S_ERR | BSC_S_DONE)

#define I2C_DEFAULT_DIVIDER 1500u

typedef enum
{
    I2C_NONE = 0,
    I2C_WRITE,
    I2C_READ
} i2c_op_t;

typedef struct
{
    volatile int busy;
    volatile int done;
    volatile int error;

    i2c_op_t op;
    int waiting_task_id;

    const uint8_t *tx_buf;
    size_t tx_len;
    size_t tx_pos;

    uint8_t *rx_buf;
    size_t rx_len;
    size_t rx_pos;
} i2c_transfer_t;

static i2c_transfer_t i2c_transfer;

/*
 * Print a decoded view of the BSC status register for debugging.
 *
 * This is useful to diagnose I2C errors (clock stretch timeout, NACK, etc.)
 * and observe FIFO / transfer state transitions.
 */
static void i2c_debug_status(const char *where, uint32_t status)
{
    uart_puts("i2c ");
    uart_puts(where);
    uart_puts(" status=");
    uart_put_uint(status);
    uart_puts(" [");

    if (status & BSC_S_CLKT)
        uart_puts("CLKT ");
    if (status & BSC_S_ERR)
        uart_puts("ERR ");
    if (status & BSC_S_RXD)
        uart_puts("RXD ");
    if (status & BSC_S_TXD)
        uart_puts("TXD ");
    if (status & BSC_S_DONE)
        uart_puts("DONE ");
    if (status & BSC_S_TA)
        uart_puts("TA ");

    uart_puts("]\n");
}

/*
 * Stop the controller, clear all status flags and flush the FIFO.
 *
 * This puts the hardware into a clean baseline state before starting
 * a new transfer or after an error condition.
 */
static void i2c_hw_clear(void)
{
    mmio_write(BSC_C, 0);
    mmio_write(BSC_S, BSC_S_CLEAR);
    mmio_write(BSC_C, BSC_C_I2CEN | BSC_C_CLEAR_FIFO);
}

/*
 * Fully reset the I2C controller configuration.
 *
 * This includes:
 * - clearing controller state and FIFO
 * - restoring clock divider and timing parameters
 * - disabling clock stretch timeout
 *
 * Used during initialization and after serious error conditions.
 */
static void i2c_reset_controller(void)
{
    i2c_hw_clear();

    mmio_write(BSC_DIV, I2C_DEFAULT_DIVIDER);
    mmio_write(BSC_DEL, 0x00300030);

    // Disable clock-stretch timeout for now
    mmio_write(BSC_CLKT, 0);

    mmio_write(BSC_C, BSC_C_I2CEN);
    mmio_write(BSC_S, BSC_S_CLEAR);
}

/*
 * Reset the software transfer state.
 *
 * This clears all bookkeeping fields and marks the transfer engine
 * as idle. Does not touch hardware.
 */
static void i2c_transfer_reset(void)
{
    i2c_transfer.busy = 0;
    i2c_transfer.done = 0;
    i2c_transfer.error = 0;
    i2c_transfer.op = I2C_NONE;
    i2c_transfer.waiting_task_id = -1;
    i2c_transfer.tx_buf = 0;
    i2c_transfer.tx_len = 0;
    i2c_transfer.tx_pos = 0;
    i2c_transfer.rx_buf = 0;
    i2c_transfer.rx_len = 0;
    i2c_transfer.rx_pos = 0;
}

/*
 * Complete the current transfer.
 *
 * - Stores result (success or error)
 * - Marks transfer as done and not busy
 * - Resets controller status flags
 * - Wakes up the waiting task (if any)
 *
 * Called exclusively from IRQ context.
 */
static void i2c_finish_transfer(int error)
{
    int waiting_id = i2c_transfer.waiting_task_id;

    i2c_transfer.error = error;
    i2c_transfer.done = 1;
    i2c_transfer.busy = 0;
    i2c_transfer.op = I2C_NONE;

    mmio_write(BSC_C, BSC_C_I2CEN);
    mmio_write(BSC_S, BSC_S_CLEAR);

    if (waiting_id >= 0)
    {
        task_wakeup(waiting_id);
    }
}

/*
 * Push as many bytes as possible into the TX FIFO.
 *
 * This function is used both before starting a transfer (initial fill)
 * and during IRQ handling when more FIFO space becomes available.
 */
static void i2c_service_tx(void)
{
    while (i2c_transfer.tx_pos < i2c_transfer.tx_len)
    {
        uint32_t status = mmio_read(BSC_S);

        if ((status & BSC_S_TXD) == 0)
        {
            break;
        }

        mmio_write(BSC_FIFO, i2c_transfer.tx_buf[i2c_transfer.tx_pos++]);
    }
}

/*
 * Drain as many bytes as possible from the RX FIFO.
 *
 * This function is called from IRQ context and at transfer completion
 * to ensure all received bytes are copied into the user buffer.
 */
static void i2c_service_rx(void)
{
    while (i2c_transfer.rx_pos < i2c_transfer.rx_len)
    {
        uint32_t status = mmio_read(BSC_S);

        if ((status & BSC_S_RXD) == 0)
        {
            break;
        }

        i2c_transfer.rx_buf[i2c_transfer.rx_pos++] = (uint8_t)mmio_read(BSC_FIFO);
    }
}

/*
 * Prepare hardware registers for a new transfer.
 *
 * This clears the controller and programs:
 * - slave address
 * - transfer length
 *
 * Does not start the transfer.
 */
static void i2c_prepare_transfer(uint8_t addr, uint32_t dlen)
{
    i2c_hw_clear();
    mmio_write(BSC_A, addr);
    mmio_write(BSC_DLEN, dlen);
}

/*
 * Initialize the shared transfer state for a new operation.
 *
 * Sets up buffers, counters and the calling task ID.
 * Assumes that no transfer is currently active.
 */
static void i2c_setup_transfer(i2c_op_t op, const uint8_t *tx_buf, size_t tx_len,
                               uint8_t *rx_buf, size_t rx_len)
{
    i2c_transfer.busy = 1;
    i2c_transfer.done = 0;
    i2c_transfer.error = 0;
    i2c_transfer.op = op;
    i2c_transfer.waiting_task_id = scheduler_current_task_id();

    i2c_transfer.tx_buf = tx_buf;
    i2c_transfer.tx_len = tx_len;
    i2c_transfer.tx_pos = 0;

    i2c_transfer.rx_buf = rx_buf;
    i2c_transfer.rx_len = rx_len;
    i2c_transfer.rx_pos = 0;
}

/*
 * Validate that a transfer can be started.
 *
 * Ensures:
 * - we are in task context (not IRQ)
 * - no other transfer is currently active
 */
static int i2c_can_start_transfer(void)
{
    if (scheduler_current_task_id() < 0)
    {
        return -1;
    }

    if (i2c_transfer.busy)
    {
        return -1;
    }

    return 0;
}

/*
 * Start an asynchronous I2C write transfer.
 *
 * The calling task will block until completion.
 * Data is written via FIFO and completed in IRQ context.
 */
static int i2c_start_write(uint8_t addr, const uint8_t *data, size_t len)
{
    if (!data || len == 0)
    {
        return -1;
    }

    if (i2c_can_start_transfer() < 0)
    {
        return -1;
    }

    i2c_setup_transfer(I2C_WRITE, data, len, 0, 0);
    i2c_prepare_transfer(addr, (uint32_t)len);

    // Pre-fill the FIFO before asserting ST so the controller can start
    // transmitting immediately.
    i2c_service_tx();

    mmio_write(BSC_C, BSC_C_I2CEN | BSC_C_ST | BSC_C_INTT | BSC_C_INTD);

    return 0;
}

/*
 * Start an asynchronous I2C read transfer.
 *
 * The calling task will block until completion.
 * Data is received via FIFO and filled in IRQ context.
 */
static int i2c_start_read(uint8_t addr, uint8_t *data, size_t len)
{
    if (!data || len == 0)
    {
        return -1;
    }

    if (i2c_can_start_transfer() < 0)
    {
        return -1;
    }

    i2c_setup_transfer(I2C_READ, 0, 0, data, len);
    i2c_prepare_transfer(addr, (uint32_t)len);

    mmio_write(BSC_C, BSC_C_I2CEN | BSC_C_ST | BSC_C_READ | BSC_C_INTR | BSC_C_INTD);

    return 0;
}

/*
 * Block the current task until the active transfer completes.
 *
 * If the transfer already finished, returns immediately.
 * Otherwise:
 * - marks task as blocked
 * - yields to scheduler
 * - resumes when IRQ wakes it up
 *
 * Returns 0 on success, negative on error.
 */
static int i2c_wait_for_transfer(void)
{
    irq_disable();

    if (i2c_transfer.done)
    {
        int error = i2c_transfer.error;
        irq_enable();
        return error;
    }

    task_block_current_no_yield();
    irq_enable();

    scheduler_yield();

    return i2c_transfer.error;
}

/*
 * Initialize the I2C subsystem.
 *
 * - configures GPIO pins for I2C (ALT0)
 * - resets transfer state
 * - initializes controller registers
 */
void i2c_init(void)
{
    gpio_use_as_alt0(2);
    gpio_use_as_alt0(3);

    i2c_transfer_reset();
    i2c_reset_controller();
}

/*
 * Perform a blocking I2C write transfer.
 *
 * Wrapper around interrupt-driven write.
 * Returns 0 on success, negative on failure.
 */
int i2c_write(uint8_t addr, const uint8_t *data, size_t len)
{
    if (i2c_start_write(addr, data, len) < 0)
    {
        return -1;
    }

    return i2c_wait_for_transfer();
}

/*
 * Perform a blocking I2C read transfer.
 *
 * Wrapper around interrupt-driven read.
 * Returns 0 on success, negative on failure.
 */
int i2c_read(uint8_t addr, uint8_t *data, size_t len)
{
    if (i2c_start_read(addr, data, len) < 0)
    {
        return -1;
    }

    return i2c_wait_for_transfer();
}

/*
 * Perform a write followed by a read operation.
 *
 * This is implemented as two independent transfers with a controller reset
 * in between (no repeated-start condition).
 *
 * Works for devices that tolerate STOP between write and read phases.
 */
int i2c_write_read(uint8_t addr,
                   const uint8_t *tx_data,
                   size_t tx_len,
                   uint8_t *rx_data,
                   size_t rx_len)
{
    if (!tx_data || tx_len == 0 || !rx_data || rx_len == 0)
    {
        return -1;
    }

    if (i2c_write(addr, tx_data, tx_len) < 0)
    {
        uart_puts("i2c_write_read: write failed\n");
        return -1;
    }

    i2c_reset_controller();

    if (i2c_read(addr, rx_data, rx_len) < 0)
    {
        uart_puts("i2c_write_read: read failed\n");
        return -1;
    }

    return 0;
}

/*
 * Write a single 8-bit value to a register.
 *
 * Convenience helper for typical register-based I2C devices.
 */
int i2c_write_reg8(uint8_t addr, uint8_t reg, uint8_t value)
{
    uint8_t buffer[2];

    buffer[0] = reg;
    buffer[1] = value;

    return i2c_write(addr, buffer, 2);
}

/*
 * Read a single 8-bit register value.
 *
 * Internally performs write (register select) + read.
 */
int i2c_read_reg8(uint8_t addr, uint8_t reg, uint8_t *value)
{
    if (!value)
    {
        return -1;
    }

    return i2c_write_read(addr, &reg, 1, value, 1);
}

/*
 * I2C interrupt handler.
 *
 * Handles the full transfer lifecycle:
 * - detects and handles errors
 * - feeds TX FIFO / drains RX FIFO
 * - detects transfer completion (DONE)
 * - wakes up waiting task
 *
 * Runs in IRQ context.
 */
void i2c_handle_irq(void)
{
    uint32_t status = mmio_read(BSC_S);

    if (!i2c_transfer.busy)
    {
        mmio_write(BSC_C, BSC_C_I2CEN);
        mmio_write(BSC_S, BSC_S_CLEAR);
        return;
    }

    if (status & (BSC_S_ERR | BSC_S_CLKT))
    {
        i2c_debug_status("irq-error", status);
        i2c_reset_controller();
        i2c_finish_transfer(-1);
        return;
    }

    if (i2c_transfer.op == I2C_WRITE)
    {
        i2c_service_tx();
    }
    else if (i2c_transfer.op == I2C_READ)
    {
        i2c_service_rx();
    }

    status = mmio_read(BSC_S);

    if (status & (BSC_S_ERR | BSC_S_CLKT))
    {
        i2c_debug_status("irq-post-service-error", status);
        i2c_reset_controller();
        i2c_finish_transfer(-1);
        return;
    }

    if (status & BSC_S_DONE)
    {
        if (i2c_transfer.op == I2C_READ)
        {
            i2c_service_rx();
        }

        mmio_write(BSC_S, BSC_S_CLEAR);
        i2c_finish_transfer(0);
    }
}