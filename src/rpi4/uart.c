#include "rpi4/uart.h"
#include "rpi4/mmio.h"
#include "rpi4/gpio.h"
#include "util/convert.h"
#include "kernel/sched/scheduler.h"
#include "kernel/sched/task.h"
#include "kernel/irq.h"
#include "kernel/sync/mutex.h"
#include "kernel/sync/cond.h"

/* Base address of peripheral MMIO region (Raspberry Pi 4 BCM2711) */
#define PERIPHERAL_BASE ((uintptr_t)0xFE000000)

/* Base address of the auxiliary peripheral block (contains Mini UART) */
#define AUX_BASE (PERIPHERAL_BASE + 0x215000)

/* Mini UART registers */
#define AUX_IRQ (AUX_BASE + 0x00)         // AUX interrupt status
#define AUX_ENABLES (AUX_BASE + 0x04)     // Enable auxiliary peripherals
#define AUX_MU_IO_REG (AUX_BASE + 0x40)   // Data register (read/write characters)
#define AUX_MU_IER_REG (AUX_BASE + 0x44)  // Interrupt enable register
#define AUX_MU_IIR_REG (AUX_BASE + 0x48)  // Interrupt identify / FIFO control
#define AUX_MU_LCR_REG (AUX_BASE + 0x4C)  // Line control (data format)
#define AUX_MU_MCR_REG (AUX_BASE + 0x50)  // Modem control
#define AUX_MU_LSR_REG (AUX_BASE + 0x54)  // Line status register
#define AUX_MU_CNTL_REG (AUX_BASE + 0x60) // Enable transmitter/receiver
#define AUX_MU_BAUD_REG (AUX_BASE + 0x68) // Baud rate configuration

/* Mini UART input clock frequency */
#define AUX_UART_CLOCK 500000000

/* Compute baud rate register value */
#define AUX_MU_BAUD(baud) ((AUX_UART_CLOCK / ((baud) * 8)) - 1)

/*
 * Mini UART interrupt enable bits.
 *
 * Bit 0: RX interrupt enable
 * Bit 1: TX interrupt enable
 */
#define AUX_MU_IER_RX_ENABLE 0x01u
#define AUX_MU_IER_TX_ENABLE 0x02u

/*
 * Line Status Register bits.
 *
 * Bit 0: RX data ready
 * Bit 5: TX FIFO can accept at least one byte
 */
#define AUX_MU_LSR_RX_READY 0x01u
#define AUX_MU_LSR_TX_READY 0x20u

/*
 * RX is filled by the UART IRQ handler and consumed by task context.
 * Access from task context is protected by disabling IRQs.
 */
#define UART_RX_BUFFER_SIZE 128

/*
 * TX is filled by task context and drained by the UART IRQ handler.
 * Writers are serialized by uart_tx_mutex once the scheduler is running.
 */
#define UART_TX_BUFFER_SIZE 512

static volatile char uart_rx_buffer[UART_RX_BUFFER_SIZE];
static volatile unsigned int uart_rx_head = 0;
static volatile unsigned int uart_rx_tail = 0;

static volatile char uart_tx_buffer[UART_TX_BUFFER_SIZE];
static volatile unsigned int uart_tx_head = 0;
static volatile unsigned int uart_tx_tail = 0;
/*
 * Task woken when RX data arrives.
 */
static int uart_rx_task_id = -1;

/*
 * Serializes task-context UART output and lets writers sleep while
 * the TX ring buffer is full.
 */
static mutex_t uart_tx_mutex;
static cond_t uart_tx_not_full;
static int uart_tx_lock_ready = 0;

/*
 * Lock TX output only after scheduler synchronization is available.
 * Early boot output still uses direct polling.
 */
static int uart_lock_tx(void)
{
    if (!uart_tx_lock_ready)
    {
        return 0;
    }

    if (scheduler_current_task_id() < 0)
    {
        return 0;
    }

    mutex_lock(&uart_tx_mutex);
    return 1;
}

static void uart_unlock_tx(int locked)
{
    if (locked)
    {
        mutex_unlock(&uart_tx_mutex);
    }
}

/*
 * Check whether the UART transmit FIFO can accept a new byte.
 */
static uint32_t uart_can_write(void)
{
    return mmio_read(AUX_MU_LSR_REG) & AUX_MU_LSR_TX_READY;
}

/*
 * Check whether at least one received byte is available.
 */
static unsigned int uart_data_ready(void)
{
    return mmio_read(AUX_MU_LSR_REG) & AUX_MU_LSR_RX_READY;
}

/*
 * Enable UART TX interrupts.
 *
 * RX interrupt enable is preserved.
 */
static void uart_enable_tx_irq(void)
{
    uint32_t ier = mmio_read(AUX_MU_IER_REG);
    ier |= AUX_MU_IER_TX_ENABLE;
    mmio_write(AUX_MU_IER_REG, ier);
}

/*
 * Disable UART TX interrupts.
 *
 * RX interrupt enable is preserved.
 */
static void uart_disable_tx_irq(void)
{
    uint32_t ier = mmio_read(AUX_MU_IER_REG);
    ier &= ~AUX_MU_IER_TX_ENABLE;
    mmio_write(AUX_MU_IER_REG, ier);
}

/*
 * Return non-zero if the TX software buffer is empty.
 *
 * Caller must either run in IRQ context or hold interrupts disabled.
 */
static int uart_tx_buffer_empty(void)
{
    return uart_tx_head == uart_tx_tail;
}

/*
 * Push one byte into the TX software buffer.
 *
 * Returns 1 on success, 0 if the buffer is full.
 * Caller must either run in IRQ context or hold interrupts disabled.
 */
static int uart_tx_push(char c)
{
    unsigned int next = (uart_tx_head + 1u) % UART_TX_BUFFER_SIZE;

    if (next == uart_tx_tail)
    {
        return 0;
    }

    uart_tx_buffer[uart_tx_head] = c;
    uart_tx_head = next;
    return 1;
}

/*
 * Pop one byte from the TX software buffer.
 *
 * Returns 1 on success, 0 if the buffer is empty.
 * Caller must either run in IRQ context or hold interrupts disabled.
 */
static int uart_tx_pop(char *c)
{
    if (uart_tx_head == uart_tx_tail)
    {
        return 0;
    }

    *c = uart_tx_buffer[uart_tx_tail];
    uart_tx_tail = (uart_tx_tail + 1u) % UART_TX_BUFFER_SIZE;
    return 1;
}

/*
 * Move queued TX bytes into the hardware FIFO.
 * Called from the UART IRQ handler and from task context with IRQs disabled.
 */
static void uart_tx_drain(void)
{
    char c;
    int made_space = 0;

    while (uart_can_write())
    {
        if (!uart_tx_pop(&c))
        {
            uart_disable_tx_irq();
            break;
        }

        mmio_write(AUX_MU_IO_REG, (uint32_t)c);
        made_space = 1;
    }

    if (uart_tx_buffer_empty())
    {
        uart_disable_tx_irq();
    }
    else
    {
        uart_enable_tx_irq();
    }

    if (made_space)
    {
        cond_signal_irq_disabled(&uart_tx_not_full);
    }
}

/*
 * Queue one byte for interrupt-driven TX.
 * If the TX ring is full, the current writer sleeps until space is available.
 */
static void uart_write_byte(char c)
{
    // before the scheduler is active, keep direct boot output working
    if (!uart_tx_lock_ready || scheduler_current_task_id() < 0)
    {
        while (!uart_can_write())
        {
        }

        mmio_write(AUX_MU_IO_REG, (uint32_t)c);
        return;
    }

    while (1)
    {
        irq_disable();

        if (uart_tx_push(c))
        {
            uart_enable_tx_irq();

            // if the UART is already ready, push bytes immediately into
            // the hardware FIFO; remaining bytes continue via TX IRQ
            uart_tx_drain();

            irq_enable();
            return;
        }

        // TX ring is full
        uart_enable_tx_irq();
        cond_wait_irq_disabled(&uart_tx_not_full, &uart_tx_mutex);
    }
}

/*
 * Initialize the Mini UART.
 *
 * Steps:
 *  - enable auxiliary peripheral block
 *  - configure UART registers
 *  - configure GPIO14/15 for UART function
 *  - enable transmitter and receiver
 *  - enable receive interrupt
 */
void uart_init(void)
{
    mmio_write(AUX_ENABLES, 1);                       // enable Mini UART
    mmio_write(AUX_MU_IER_REG, 0);                    // disable interrupts
    mmio_write(AUX_MU_CNTL_REG, 0);                   // disable TX/RX during configuration
    mmio_write(AUX_MU_LCR_REG, 3);                    // 8-bit data mode
    mmio_write(AUX_MU_MCR_REG, 0);                    // no modem control
    mmio_write(AUX_MU_IIR_REG, 0xC6);                 // disable FIFOx and interrupts
    mmio_write(AUX_MU_BAUD_REG, AUX_MU_BAUD(115200)); // set baud rate

    // configure GPIO pins for UART
    gpio_use_as_alt5(14); // TX
    gpio_use_as_alt5(15); // RX

    mmio_write(AUX_MU_CNTL_REG, 3);                   // enable transmitter and receiver
    mmio_write(AUX_MU_IER_REG, AUX_MU_IER_RX_ENABLE); // enable RX interrupt (bit 0)
}

/*
 * Enable scheduler-aware UART TX locking.
 */
void uart_init_tx_lock(void)
{
    mutex_init(&uart_tx_mutex);
    cond_init(&uart_tx_not_full);
    uart_tx_lock_ready = 1;
}

/*
 * Register the task that should be woken when UART RX data arrives.
 */
void uart_set_rx_task(int task_id)
{
    uart_rx_task_id = task_id;
}

int uart_get_rx_task(void)
{
    return uart_rx_task_id;
}

void uart_flush_rx(void)
{
    irq_disable();
    uart_rx_head = 0;
    uart_rx_tail = 0;
    irq_enable();
}

/*
 * Read one character from the UART software RX buffer.
 *
 * If no character is currently available, the calling task is blocked
 * atomically with interrupts disabled to avoid lost wakeups. The task
 * is resumed by the UART interrupt handler once new RX data arrives.
 *
 * Returns 1 after a character has been stored in *c.
 */
int uart_read_char(char *c)
{
    int id;
    task_t *task;

    if (!c)
    {
        return 0;
    }

    while (1)
    {
        irq_disable();

        // fast path: consume one byte if the RX ring buffer is not empty
        if (uart_rx_head != uart_rx_tail)
        {
            *c = uart_rx_buffer[uart_rx_tail];
            uart_rx_tail = (uart_rx_tail + 1u) % UART_RX_BUFFER_SIZE;

            irq_enable();
            return 1;
        }

        // no input is available; the current task must block until the
        // UART RX interrupt handler receives data and wakes it again
        id = scheduler_current_task_id();
        if (id < 0)
        {
            irq_enable();
            return 0;
        }

        task = task_get(id);
        if (!task)
        {
            irq_enable();
            return 0;
        }

        task->state = BLOCKED;
        irq_enable();

        scheduler_yield();
    }
}

/*
 * Non-blocking RX read.
 */
int uart_try_read_char(char *c)
{
    int result = 0;

    if (!c)
    {
        return 0;
    }

    irq_disable();

    if (uart_rx_head != uart_rx_tail)
    {
        *c = uart_rx_buffer[uart_rx_tail];
        uart_rx_tail = (uart_rx_tail + 1u) % UART_RX_BUFFER_SIZE;
        result = 1;
    }

    irq_enable();

    return result;
}

/*
 * Send a single character via UART.
 *
 * In task context this queues the byte and lets the TX IRQ send it.
 * Before the scheduler is running this falls back to raw polling output.
 */
void uart_putc(char c)
{
    int locked = uart_lock_tx();
    uart_write_byte(c);
    uart_unlock_tx(locked);
}

/*
 * Send a null-terminated string via UART.
 * Converts '\n' to CRLF ("\r\n") for terminal compatibility.
 */
void uart_puts(const char *s)
{
    int locked;

    if (!s)
    {
        return;
    }

    locked = uart_lock_tx();

    while (*s)
    {
        if (*s == '\n')
        {
            uart_write_byte('\r');
        }

        uart_write_byte(*s++);
    }

    uart_unlock_tx(locked);
}

/*
 * Print an unsigned integer to UART.
 */
void uart_put_uint(unsigned int value)
{
    char buffer[16];
    int i = 0;
    int locked = uart_lock_tx();

    if (value == 0)
    {
        uart_write_byte('0');
        uart_unlock_tx(locked);
        return;
    }

    while (value > 0)
    {
        buffer[i++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (i > 0)
    {
        uart_write_byte(buffer[--i]);
    }

    uart_unlock_tx(locked);
}

/*
 * Print an unsigned decimal integer to UART.
 */
void uart_put_u64(uint64_t value)
{
    char buffer[32];
    int locked;

    utoa_dec(value, buffer, sizeof(buffer));

    locked = uart_lock_tx();

    for (int i = 0; buffer[i] != '\0'; i++)
    {
        uart_write_byte(buffer[i]);
    }

    uart_unlock_tx(locked);
}

/*
 * Print a pointer-sized value as hexadecimal to UART.
 */
void uart_put_hex_uintptr(uintptr_t value)
{
    char buffer[32];
    int locked;

    utoa_hex((uint64_t)value, buffer, sizeof(buffer));

    locked = uart_lock_tx();

    uart_write_byte('0');
    uart_write_byte('x');

    for (int i = 0; buffer[i] != '\0'; i++)
    {
        uart_write_byte(buffer[i]);
    }

    uart_unlock_tx(locked);
}

void uart_put_hex8(uint8_t value)
{
    const char *hex = "0123456789ABCDEF";
    int locked = uart_lock_tx();

    uart_write_byte('0');
    uart_write_byte('x');
    uart_write_byte(hex[(value >> 4) & 0xF]);
    uart_write_byte(hex[value & 0xF]);

    uart_unlock_tx(locked);
}

/*
 * UART interrupt handler.
 *
 * RX side:
 *   Drains available received characters into the RX software buffer and wakes
 *   the registered RX task.
 *
 * TX side:
 *   Drains queued TX characters into the hardware FIFO while space is available.
 *   Disables TX interrupts when no TX data remains.
 */
void uart_handle_irq(void)
{
    int received = 0;

    // RX: drain all currently available received bytes
    while (uart_data_ready())
    {
        char c = (char)mmio_read(AUX_MU_IO_REG);
        unsigned int next = (uart_rx_head + 1) % UART_RX_BUFFER_SIZE;

        if (next != uart_rx_tail)
        {
            uart_rx_buffer[uart_rx_head] = c;
            uart_rx_head = next;
            received = 1;
        }
    }

    if (received && uart_rx_task_id >= 0)
    {
        task_wakeup(uart_rx_task_id);
    }

    // TX: only drain if TX interrupts are currently enabled
    // the interrupt is enabled when data is queued and disabled once
    // the software TX buffer becomes empty
    if (mmio_read(AUX_MU_IER_REG) & AUX_MU_IER_TX_ENABLE)
    {
        uart_tx_drain();
    }
}