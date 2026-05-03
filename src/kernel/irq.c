#include "kernel/irq.h"
#include "kernel/timer.h"
#include "kernel/sched/scheduler.h"
#include "kernel/tasks/joystick_task.h"
#include "rpi4/uart.h"
#include "rpi4/mmio.h"
#include "rpi4/gpio.h"
#include "rpi4/i2c.h"
#include "sensehat/joystick.h"

/*
 * GIC-400 base addresses (BCM2711)
 */
#define GIC_BASE ((uintptr_t)0xFF840000)
#define GICD_BASE (GIC_BASE + 0x1000) // distributor
#define GICC_BASE (GIC_BASE + 0x2000) // CPU interface

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
#define GICD_IPRIORITYR(n) (GICD_BASE + 0x400 + (((n) / 4) * 4))
#define GICD_ITARGETSR(n) (GICD_BASE + 0x800 + (((n) / 4) * 4))

/*
 * GIC CPU interface registers
 */
#define GICC_CTLR (GICC_BASE + 0x000)
#define GICC_PMR (GICC_BASE + 0x004)
#define GICC_IAR (GICC_BASE + 0x00C)  // interrupt acknowledge
#define GICC_EOIR (GICC_BASE + 0x010) // end of interrupt

/*
 * Interrupt IDs currently used by the kernel.
 *
 * UART1 (Mini UART): VC IRQ 29 -> GIC SPI 96 + 29 = 125
 * Generic timer: PPI 30
 * GPIO bank 0: VC IRQ 49 -> GIC SPI 96 + 49 = 145
 *
 * I2C1 / BSC1:
 * Keep this as a single macro so the mapping can be adjusted easily
 * if your board/setup uses a different routed interrupt ID.
 */
#define UART1_GIC_INTID 125
#define TIMER_GIC_INTID 30
#define GPIO0_GIC_INTID 145
#define I2C1_GIC_INTID 149

/*
 * Enable a shared peripheral interrupt (SPI) in the GIC distributor.
 *
 * This configures the interrupt as Group 1, assigns its priority,
 * routes it to CPU0 and enables it in the distributor.
 *
 * Used for device interrupts such as UART, GPIO and I2C.
 */
static void gic_enable_spi_irq(uint32_t intid, uint8_t priority)
{
    uint32_t bit = 1u << (intid % 32);
    uint32_t shift = (intid % 4) * 8;
    uint32_t reg;

    // put interrupt into Group 1 (normal IRQ in EL1)
    reg = mmio_read(GICD_IGROUPR(intid / 32));
    reg |= bit;
    mmio_write(GICD_IGROUPR(intid / 32), reg);

    // set priority (8-bit field inside 32-bit word)
    reg = mmio_read(GICD_IPRIORITYR(intid));
    reg &= ~(0xFFu << shift);
    reg |= ((uint32_t)priority << shift);
    mmio_write(GICD_IPRIORITYR(intid), reg);

    // route SPI to CPU0
    reg = mmio_read(GICD_ITARGETSR(intid));
    reg &= ~(0xFFu << shift);
    reg |= (0x01u << shift);
    mmio_write(GICD_ITARGETSR(intid), reg);

    // enable interrupt
    mmio_write(GICD_ISENABLER(intid / 32), bit);
}

/*
 * Enable a private peripheral interrupt (PPI) in the GIC distributor.
 *
 * This configures the interrupt as Group 1, assigns its priority
 * and enables it for the current CPU interface.
 *
 * Used for per-core interrupts such as the generic timer.
 */
static void gic_enable_ppi_irq(uint32_t intid, uint8_t priority)
{
    uint32_t bit = 1u << (intid % 32);
    uint32_t shift = (intid % 4) * 8;
    uint32_t reg;

    // put interrupt into Group 1
    reg = mmio_read(GICD_IGROUPR(intid / 32));
    reg |= bit;
    mmio_write(GICD_IGROUPR(intid / 32), reg);

    // set priority
    reg = mmio_read(GICD_IPRIORITYR(intid));
    reg &= ~(0xFFu << shift);
    reg |= ((uint32_t)priority << shift);
    mmio_write(GICD_IPRIORITYR(intid), reg);

    // enable interrupt
    mmio_write(GICD_ISENABLER(intid / 32), bit);
}

/*
 * Handle a GPIO bank 0 interrupt caused by the Sense HAT joystick.
 *
 * If the configured joystick interrupt GPIO has a pending event,
 * the event flag is cleared and the joystick task is woken up.
 */
static void handle_gpio_irq(void)
{
    int joystick_id;

    if (!gpio_event_detected(JOYSTICK_INT_GPIO))
    {
        return;
    }

    gpio_clear_event(JOYSTICK_INT_GPIO);
    joystick_signal_irq();

    joystick_id = joystick_get_task_id();
    if (joystick_id >= 0)
    {
        task_wakeup_irq_disabled(joystick_id);
    }
}

/*
 * Initialize the interrupt controller for currently used interrupts:
 *  - Mini UART receive interrupt
 *  - Generic timer interrupt
 *  - GPIO interrupt
 *  - I2C1 / BSC1 interrupt
 */
void gic_init(void)
{
    // disable distributor during setup
    mmio_write(GICD_CTLR, 0);

    // SPI interrupts
    gic_enable_spi_irq(UART1_GIC_INTID, 0x80);
    gic_enable_spi_irq(GPIO0_GIC_INTID, 0x90);
    gic_enable_spi_irq(I2C1_GIC_INTID, 0x91);

    // PPI interrupt
    gic_enable_ppi_irq(TIMER_GIC_INTID, 0x88);

    // allow all priorities
    mmio_write(GICC_PMR, 0xFF);

    // enable distributor and CPU interface
    mmio_write(GICD_CTLR, 1);
    mmio_write(GICC_CTLR, 1);
}

/*
 * Central IRQ dispatcher called from the exception vector table.
 *
 * The handler acknowledges the interrupt at the CPU interface,
 * dispatches to the responsible subsystem, then signals completion.
 */
void handle_irq(void)
{
    uint32_t iar = mmio_read(GICC_IAR);
    uint32_t intid = iar & 0x3FF;

    if (intid == UART1_GIC_INTID)
    {
        uart_handle_irq();
    }
    else if (intid == TIMER_GIC_INTID)
    {
        timer_handle_tick();
    }
    else if (intid == GPIO0_GIC_INTID)
    {
        handle_gpio_irq();
    }
    else if (intid == I2C1_GIC_INTID)
    {
        i2c_handle_irq();
    }
    else
    {
        uart_puts("Unhandled IRQ intid=");
        uart_put_uint((unsigned int)intid);
        uart_puts("\n");
    }

    // signal end of interrupt to GIC
    mmio_write(GICC_EOIR, iar);
}

/*
 * Print ESR_EL1 and ELR_EL1 for unexpected synchronous exceptions.
 * Used as a simple debug fallback before halting the system.
 */
void exception_debug(void)
{
    uart_puts("\n!!! EXCEPTION !!!\n");

    uint64_t esr, elr, spsr, far;

    asm volatile("mrs %0, esr_el1" : "=r"(esr));
    asm volatile("mrs %0, elr_el1" : "=r"(elr));
    asm volatile("mrs %0, spsr_el1" : "=r"(spsr));
    asm volatile("mrs %0, far_el1" : "=r"(far));

    uart_puts("ESR: 0x");
    for (int i = 60; i >= 0; i -= 4)
    {
        int v = (esr >> i) & 0xF;
        uart_putc(v < 10 ? '0' + v : 'A' + v - 10);
    }

    uart_puts("\nEC: 0x");
    uart_put_uint((unsigned int)((esr >> 26) & 0x3F));

    uart_puts("\nELR: 0x");
    for (int i = 60; i >= 0; i -= 4)
    {
        int v = (elr >> i) & 0xF;
        uart_putc(v < 10 ? '0' + v : 'A' + v - 10);
    }

    uart_puts("\nSPSR: 0x");
    for (int i = 60; i >= 0; i -= 4)
    {
        int v = (spsr >> i) & 0xF;
        uart_putc(v < 10 ? '0' + v : 'A' + v - 10);
    }

    uart_puts("\nFAR: 0x");
    for (int i = 60; i >= 0; i -= 4)
    {
        int v = (far >> i) & 0xF;
        uart_putc(v < 10 ? '0' + v : 'A' + v - 10);
    }

    uart_puts("\nSystem halted.\n");
}
