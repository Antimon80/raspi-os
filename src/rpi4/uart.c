#include "rpi4/uart.h"
#include "rpi4/mmio.h"
#include "rpi4/gpio.h"

/* Base address of peripheral MMIO region (Raspberry Pi 4 BCM2711) */
#define PERIPHERAL_BASE ((uintptr_t)0xFE000000)

/* Base address of the auxiliary peripheral block (contains Mini UART) */
#define AUX_BASE (PERIPHERAL_BASE + 0x215000)

/* Mini UART registers */
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
 * Check whether the UART transmit FIFO can accept a new byte.
 * Bit 5 of the Line Status Register indicates TX readiness.
 */
static uint32_t uart_can_write(void)
{
    return mmio_read(AUX_MU_LSR_REG) & 0x20;
}

/*
 * Initialize the Mini UART.
 *
 * Steps:
 *  - enable auxiliary peripheral block
 *  - configure UART registers
 *  - configure GPIO14/15 for UART function
 *  - enable transmitter and receiver
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
