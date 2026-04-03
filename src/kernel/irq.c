#include "kernel/irq.h"
#include "rpi4/uart.h"
#include "rpi4/mmio.h"

/*
 * Base addresses for peripherals and interrupt controller
 */
#define PERIPHERAL_BASE ((uintptr_t)0xFE000000)
#define AUX_BASE (PERIPHERAL_BASE + 0x215000)

/*
 * AUX (Mini UART) registers
 */
#define AUX_IRQ (AUX_BASE + 0x00)          // AUX interrupt status
#define AUX_MU_IO_REG (AUX_BASE + 0x40)    // data register
#define AUX_MU_LSR_REG (AUX_BASE + 0x54)   // line status (bit 0 = data ready)

/*
 * GIC-400 base addresses (BCM2711)
 */
#define GIC_BASE ((uintptr_t)0xFF840000)
#define GICD_BASE (GIC_BASE + 0x1000)      // distributor
#define GICC_BASE (GIC_BASE + 0x2000)      // CPU interface

/*
 * GIC distributor registers
 */
#define GICD_CTLR (GICD_BASE + 0x000)
#define GICD_IGROUPR(n) (GICD_BASE + 0x080 + ((n) * 4))
#define GICD_ISENABLER(n) (GICD_BASE + 0x100 + ((n) * 4))

/*
 * Priority and target registers are byte-addressed,
 * but accessed as 32-bit words → align to 4 bytes
 */
#define GICD_IPRIORITYR(n)  (GICD_BASE + 0x400 + (((n) / 4) * 4))
#define GICD_ITARGETSR(n)   (GICD_BASE + 0x800 + (((n) / 4) * 4))

/*
 * GIC CPU interface registers
 */
#define GICC_CTLR (GICC_BASE + 0x000)
#define GICC_PMR (GICC_BASE + 0x004)
#define GICC_IAR (GICC_BASE + 0x00C)   // interrupt acknowledge
#define GICC_EOIR (GICC_BASE + 0x010)  // end of interrupt

/*
 * UART1 (Mini UART) interrupt:
 * VC IRQ 29 → GIC SPI 96 + 29 = 125
 */
#define UART1_GIC_INTID 125

/*
 * Simple ring buffer for received UART characters
 */
#define UART_BUFFER_SIZE 128

static volatile char uart_buffer[UART_BUFFER_SIZE];
static volatile uint32_t uart_head = 0;
static volatile uint32_t uart_tail = 0;

/*
 * Check if UART has received data
 */
static int uart_data_ready(void)
{
    return mmio_read(AUX_MU_LSR_REG) & 0x01;
}

/*
 * UART RX interrupt handler:
 * Drain all available characters into ring buffer
 */
static void uart_irq_handler(void)
{
    while (uart_data_ready())
    {
        char c = (char)mmio_read(AUX_MU_IO_REG);
        uint32_t next = (uart_head + 1) % UART_BUFFER_SIZE;

        // drop character if buffer is full
        if (next != uart_tail)
        {
            uart_buffer[uart_head] = c;
            uart_head = next;
        }
    }
}

/*
 * Read one character from software buffer
 * (non-blocking)
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
 * Initialize GIC for UART1 interrupt
 */
void gic_init(void)
{
    uint32_t bit = 1u << (UART1_GIC_INTID % 32);
    uint32_t reg;
    uint32_t shift = (UART1_GIC_INTID % 4) * 8;

    // disable distributor during setup
    mmio_write(GICD_CTLR, 0);

    // put interrupt into Group 1 (normal IRQ in EL1)
    reg = mmio_read(GICD_IGROUPR(UART1_GIC_INTID / 32));
    reg |= bit;
    mmio_write(GICD_IGROUPR(UART1_GIC_INTID / 32), reg);

    // set priority (8-bit field inside 32-bit word)
    reg = mmio_read(GICD_IPRIORITYR(UART1_GIC_INTID));
    reg &= ~(0xFFu << shift);
    reg |= (0x80u << shift);
    mmio_write(GICD_IPRIORITYR(UART1_GIC_INTID), reg);

    // route interrupt to CPU0
    reg = mmio_read(GICD_ITARGETSR(UART1_GIC_INTID));
    reg &= ~(0xFFu << shift);
    reg |= (0x01u << shift);
    mmio_write(GICD_ITARGETSR(UART1_GIC_INTID), reg);

    // enable interrupt
    mmio_write(GICD_ISENABLER(UART1_GIC_INTID / 32), bit);

    // allow all priorities
    mmio_write(GICC_PMR, 0xFF);

    // enable distributor and CPU interface
    mmio_write(GICD_CTLR, 1);
    mmio_write(GICC_CTLR, 1);
}

/*
 * Central IRQ handler (called from assembly vector)
 */
void handle_irq(void)
{
    uint32_t iar = mmio_read(GICC_IAR);
    uint32_t intid = iar & 0x3FF;

    if (intid == UART1_GIC_INTID)
    {
        // check AUX sub-interrupt (Mini UART = bit 0)
        if (mmio_read(AUX_IRQ) & 0x1)
        {
            uart_irq_handler();
        }
    }

    // signal end of interrupt to GIC
    mmio_write(GICC_EOIR, iar);
}

/*
 * Debug output for unexpected exceptions
 */
void exception_debug(void)
{
    uart_puts("\n!!! EXCEPTION !!!\n");

    uint64_t esr, elr;

    asm volatile("mrs %0, esr_el1" : "=r"(esr));
    asm volatile("mrs %0, elr_el1" : "=r"(elr));

    uart_puts("ESR: 0x");
    for (int i = 60; i >= 0; i -= 4)
    {
        int v = (esr >> i) & 0xF;
        uart_putc(v < 10 ? '0' + v : 'A' + v - 10);
    }

    uart_puts("\nELR: 0x");
    for (int i = 60; i >= 0; i -= 4)
    {
        int v = (elr >> i) & 0xF;
        uart_putc(v < 10 ? '0' + v : 'A' + v - 10);
    }

    uart_puts("\nSystem halted.\n");
}