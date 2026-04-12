#include "rpi4/uart.h"
#include "rpi4/mmio.h"
#include "rpi4/gpio.h"
#include "util/convert.h"
#include "kernel/sched/scheduler.h"
#include "kernel/irq.h"

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

/*
 * Register the task that should be woken when UART RX data arrives.
 */
void uart_set_rx_task(int task_id)
{
    uart_rx_task_id = task_id;
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
 * Send a single character via UART.
 * Blocks until the transmit register is ready.
 */
void uart_putc(char c)
{
    while (!uart_can_write())
    {
    }

    mmio_write(AUX_MU_IO_REG, (uint32_t)c);
}

/*
 * Send a null-terminated string via UART.
 * Converts '\n' to CRLF ("\r\n") for terminal compatibility.
 */
void uart_puts(const char *s)
{
    while (*s)
    {
        if (*s == '\n')
        {
            uart_putc('\r');
        }
        uart_putc(*s++);
    }
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

        task_block_current_no_yield();
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

    if (value == 0)
    {
        uart_putc('0');
        return;
    }

    while (value > 0)
    {
        buffer[i++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (i > 0)
    {
        uart_putc(buffer[--i]);
    }
}

/*
 * Print an unsigned decimal integer to UART.
 */
void uart_put_u64(uint64_t value)
{
    char buffer[32];
    utoa_dec(value, buffer, sizeof(buffer));
    uart_puts(buffer);
}

/*
 * Print a pointer-sized value as hexadecimal to UART.
 */
void uart_put_hex_uintptr(uintptr_t value)
{
    char buffer[32];
    uart_puts("0x");
    utoa_hex((uint64_t)value, buffer, sizeof(buffer));
    uart_puts(buffer);
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
