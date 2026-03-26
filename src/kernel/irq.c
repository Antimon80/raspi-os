#include "kernel/irq.h"
#include "rpi4/uart.h"
#include "rpi4/mmio.h"

/* Base address of peripheral MMIO region (Raspberry Pi 4 BCM2711) */
#define PERIPHERAL_BASE ((uintptr_t)0xFE000000)

/*
 * Interrupt controller registers.
 *
 * IRQ_PENDING1 - reports pending interrupt sources in the first IRQ bank
 * IRQ_ENABLE1  - enables interrupt sources in the first IRQ bank
 */
#define IRQ_PENDING1 (PERIPHERAL_BASE + 0xB204)
#define IRQ_ENABLE1 (PERIPHERAL_BASE + 0xB210)

/*
 * AUX interrupt bit in IRQ_PENDING1 / IRQ_ENABLE1.
 *
 * The Mini UART belongs to the auxiliary peripheral block and signals
 * interrupts through the AUX interrupt line.
 */
#define IRQ_AUX (1 << 29)

/*
 * Size of the software receive buffer for UART input.
 *
 * Incoming characters are copied from the UART hardware into this ring
 * buffer inside the IRQ handler. The main loop or later a task can then
 * read characters from this buffer without accessing the hardware directly.
 */
#define UART_BUFFER_SIZE 128

/* Ring buffer storage and buffer indices */
static volatile char uart_buffer[UART_BUFFER_SIZE];
static volatile uint32_t uart_head = 0;
static volatile uint32_t uart_tail = 0;

/* Base address of the auxiliary peripheral block (contains Mini UART) */
#define AUX_BASE (PERIPHERAL_BASE + 0x215000)

/*
 * Mini UART registers used by the IRQ subsystem.
 *
 * AUX_MU_IO_REG  - UART data register
 * AUX_MU_LSR_REG - line status register
 */
#define AUX_MU_IO_REG (AUX_BASE + 0x40)
#define AUX_MU_LSR_REG (AUX_BASE + 0x54)

/*
 * Check whether at least one character is waiting in the Mini UART
 * receive register.
 *
 * Bit 0 of the Line Status Register indicates "data ready".
 */
static int uart_data_ready(void)
{
    return mmio_read(AUX_MU_LSR_REG) & 0x01;
}

/*
 * Handle a UART receive interrupt.
 *
 * The Mini UART may already contain more than one received character
 * when the interrupt is handled. Therefore, the handler reads all
 * currently available bytes and stores them in a ring buffer.
 *
 * If the ring buffer is full, newly received characters are dropped.
 */
static void uart_irq_handler(void)
{
    while (uart_data_ready())
    {
        char c = (char)mmio_read(AUX_MU_IO_REG);

        uint32_t next = (uart_head + 1) % UART_BUFFER_SIZE;

        if (next != uart_tail)
        {
            uart_buffer[uart_head] = c;
            uart_head = next;
        }
    }
}

/*
 * Read one character from the UART software receive buffer.
 *
 * Returns:
 *   1 if a character was available and written to *c
 *   0 if the buffer is currently empty
 *
 * This function does not read directly from the UART hardware.
 * It only consumes characters that were previously collected
 * by the IRQ handler.
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
 * Central IRQ dispatcher.
 *
 * This function is called from the assembly IRQ entry code after the
 * CPU state has been saved. It checks which interrupt source is pending
 * and forwards handling to the corresponding device-specific handler.
 *
 * At the moment, only the AUX / Mini UART interrupt is handled here.
 * Later, timer or GPIO interrupt sources can be added in the same place.
 */
void handle_irq(void)
{
    uint32_t pending = mmio_read(IRQ_PENDING1);

    if (pending & IRQ_AUX)
    {
        uart_irq_handler();
    }
}

/*
 * Debug handler for unexpected exceptions.
 *
 * This function is called from the assembly exception fallback path.
 * It prints basic diagnostic information to the UART:
 *
 *   ESR_EL1 - Exception Syndrome Register
 *   ELR_EL1 - Exception Link Register
 *
 * ESR_EL1 describes the reason for the exception.
 * ELR_EL1 contains the program address at which the exception occurred.
 *
 * After printing this information, control returns to the assembly
 * fallback code, which halts the system in an infinite loop.
 */
void exception_debug(void)
{
    uart_puts("\n!!! EXCEPTION !!!\n");

    uint64_t esr, elr;

    asm volatile("mrs %0, esr_el1" : "=r"(esr));
    asm volatile("mrs %0, elr_el1" : "=r"(elr));

    uart_puts("ESR: ");
    uart_putc('0');
    uart_putc('x');

    for (int i = 60; i >= 0; i -= 4)
    {
        int v = (esr >> i) & 0xF;
        uart_putc(v < 10 ? '0' + v : 'A' + v - 10);
    }

    uart_puts("\nELR: ");
    uart_putc('0');
    uart_putc('x');
    for (int i = 60; i >= 0; i -= 4)
    {
        int v = (elr >> i) & 0xF;
        uart_putc(v < 10 ? '0' + v : 'A' + v - 10);
    }

    uart_puts("\nSystem halted.\n");
}