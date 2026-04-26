#include "rpi4/uart.h"
#include "rpi4/hdmi.h"
#include "rpi4/mmio.h"
#include "rpi4/gpio.h"
#include "util/convert.h"
#include "kernel/sched/scheduler.h"
#include "kernel/sched/task.h"
#include "kernel/irq.h"
#include "kernel/sync/mutex.h"

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
 * Simple ring buffer for received UART characters.
 * Characters are written by the interrupt handler and read by normal code.
 */
#define UART_BUFFER_SIZE 128

static volatile char uart_buffer[UART_BUFFER_SIZE];
static volatile unsigned int uart_head = 0;
static volatile unsigned int uart_tail = 0;
static int uart_rx_task_id = -1;

static mutex_t uart_tx_mutex;
static int uart_tx_lock_ready = 0;

static uint32_t uart_can_write(void);
static unsigned int uart_data_ready(void);
static void uart_write_byte(char c);

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
 * Internal byte output.
 *
 * This function is intentionally not exported and does not lock.
 * Public uart_put* functions are responsible for synchronization.
 */
static void uart_write_byte(char c)
{
    while (!uart_can_write())
    {
    }

    mmio_write(AUX_MU_IO_REG, (uint32_t)c);
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
    uart_head = 0;
    uart_tail = 0;
    irq_enable();
}

/*
 * Check whether the UART transmit FIFO can accept a new byte.
 * Bit 5 of the Line Status Register indicates TX readiness.
 */
static uint32_t uart_can_write(void)
{
    return mmio_read(AUX_MU_LSR_REG) & 0x20;
}

/*
 * Check whether at least one received byte is available.
 * LSR bit 0 = data ready.
 */
static unsigned int uart_data_ready(void)
{
    return mmio_read(AUX_MU_LSR_REG) & 0x01;
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

    mmio_write(AUX_MU_CNTL_REG, 3); // enable transmitter and receiver
    mmio_write(AUX_MU_IER_REG, 1);  // enable RX interrupt (bit 0)
}

/*
 * Enable serialized TX output.
 *
 * Call this after task_init_system() and scheduler_init().
 */
void uart_init_tx_lock(void)
{
    mutex_init(&uart_tx_mutex);
    uart_tx_lock_ready = 1;
}

/*
 * Send a single character via UART.
 * Blocks until the transmit register is ready.
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
 * Read one character from the software RX buffer.
 * Returns 1 on success, 0 if no character is available.
 */
int uart_read_char(char *c)
{
    if (uart_head == uart_tail)
    {
        return 0;
    }

    *c = uart_buffer[uart_tail];
    uart_tail = (uart_tail + 1) % UART_BUFFER_SIZE;
    return 1;
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
int uart_read_char_blocking(char *c)
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

        if (uart_read_char(c))
        {
            irq_enable();
            return 1;
        }

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
        buffer[i++] = (char)('0' + (value % 10));
        value /= 10;
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
 * Drains all currently available received characters from the hardware
 * FIFO and stores them in the software ring buffer.
 *
 * If the buffer is full, incoming characters are dropped.
 */
void uart_handle_irq(void)
{
    int received = 0;

    // AUX bit 0 indicates a Mini UART interrupt is pending.
    // While it is pendig, drain all available RX bytes.
    while (mmio_read(AUX_IRQ) & 0x1)
    {
        while (uart_data_ready())
        {
            char c = (char)mmio_read(AUX_MU_IO_REG);
            unsigned int next = (uart_head + 1) % UART_BUFFER_SIZE;

            if (next != uart_tail)
            {
                uart_buffer[uart_head] = c;
                uart_head = next;
                received = 1;
            }
        }

        // If AUX still reports a pending UART interrupt but no RX data is
        // available anymore, leave the handler to avoid a possible loop.
        if (!uart_data_ready())
        {
            break;
        }
    }

    if (received && uart_rx_task_id >= 0)
    {
        task_wakeup(uart_rx_task_id);
    }
}
