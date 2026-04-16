#include "kernel/irq.h"
#include "kernel/timer.h"
#include "kernel/deferred_work.h"
#include "kernel/sched/scheduler.h"
#include "kernel/tasks/joystick_task.h"
#include "kernel/tasks/deferred_worker_task.h"
#include "rpi4/uart.h"
#include "rpi4/mmio.h"
#include "rpi4/gpio.h"
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
 * UART1 (Mini UART) interrupt:
 * VC IRQ 29 → GIC SPI 96 + 29 = 125
 *
 * Generic timer: PPI 30
 * GPIO bank 0 interrupt:
 * VC IRQ 49 -> GIC SPI 96 + 49 = 145
 * GPIO23 belongs to bank 0.
 */
#define UART1_GIC_INTID 125
#define TIMER_GIC_INTID 30
#define GPIO0_GIC_INTID 145

static void joystick_deferred_work(void *arg)
{
    (void)arg;
    joystick_service_change();
}

static void handle_gpio_irq(void)
{
    int worker_id;

    if (!gpio_event_detected(JOYSTICK_INT_GPIO))
    {
        return;
    }

    gpio_clear_event(JOYSTICK_INT_GPIO);

    if (deferred_work_schedule(joystick_deferred_work, 0) < 0)
    {
        return;
    }

    worker_id = deferred_worker_get_task_id();
    if (worker_id >= 0)
    {
        task_wakeup(worker_id);
    }
}

/*
 * Initialize the interrupt controller for currently used interrupts:
 *  - Mini UART receive interrupt
 *  - Generic timer interrupt
 *  - GPIO interrupt
 */
void gic_init(void)
{
    uint32_t bit = 1u << (UART1_GIC_INTID % 32);
    uint32_t reg;
    uint32_t shift = (UART1_GIC_INTID % 4) * 8;

    // disable distributor during setup
    mmio_write(GICD_CTLR, 0);

    // --------------------
    // UART interrupt
    // --------------------

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

    // --------------------
    // timer interrupt
    // --------------------

    // generic timer interrupt setup (PPI 30)
    bit = 1u << (TIMER_GIC_INTID % 32);
    shift = (TIMER_GIC_INTID % 4) * 8;

    // put timer interrupt into Group 1
    reg = mmio_read(GICD_IGROUPR(TIMER_GIC_INTID / 32));
    reg |= bit;
    mmio_write(GICD_IGROUPR(TIMER_GIC_INTID / 32), reg);

    // set timer interrupt priority
    reg = mmio_read(GICD_IPRIORITYR(TIMER_GIC_INTID));
    reg &= ~(0xFFu << shift);
    reg |= (0x88u << shift);
    mmio_write(GICD_IPRIORITYR(TIMER_GIC_INTID), reg);

    // enable timer interrupt
    mmio_write(GICD_ISENABLER(TIMER_GIC_INTID / 32), bit);

    // --------------------
    // GPIO interrupt
    // --------------------

    bit = 1u << (GPIO0_GIC_INTID % 32);
    shift = (GPIO0_GIC_INTID % 4) * 8;

    // put GPIO interrupt into Group 1
    reg = mmio_read(GICD_IGROUPR(GPIO0_GIC_INTID / 32));
    reg |= bit;
    mmio_write(GICD_IGROUPR(GPIO0_GIC_INTID / 32), reg);

    // set priority
    reg = mmio_read(GICD_IPRIORITYR(GPIO0_GIC_INTID));
    reg &= ~(0xFFu << shift);
    reg |= (0x90u << shift);
    mmio_write(GICD_IPRIORITYR(GPIO0_GIC_INTID), reg);

    // route to CPU
    reg = mmio_read(GICD_ITARGETSR(GPIO0_GIC_INTID));
    reg &= ~(0xFFu << shift);
    reg |= (0x01u << shift);
    mmio_write(GICD_ITARGETSR(GPIO0_GIC_INTID), reg);

    // enable
    mmio_write(GICD_ISENABLER(GPIO0_GIC_INTID / 32), bit);

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