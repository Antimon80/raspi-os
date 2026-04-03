#include "rpi4/uart.h"
#include "rpi4/mmio.h"
#include "kernel/irq.h"
#include "kernel/task.h"
#include "kernel/scheduler.h"
#include "kernel/panic.h"

/*
 * Simple UART echo task.
 *
 * Reads characters that were previously placed into the software
 * receive buffer by the UART interrupt handler and echoes them
 * back to the terminal.
 */
static void uart_echo_task(void)
{
    while (1)
    {
        char c;

        if (uart_read_char(&c))
        {
            uart_putc(c);
        }

        //  cooperative scheduling: explicitly give up the CPU
        scheduler_yield();
    }
}

/*
 * Simple demo task.
 *
 * Periodically prints a message so that task switching becomes
 * visible on the UART output.
 */
static void demo_task(void)
{
    while (1)
    {
        uart_puts("[demo]\n");

        //  small delay to slow down the output
        for (volatile uint64_t i = 0; i < 1000000; i++)
        {
        }

        scheduler_yield();
    }
}

/*
 * Kernel entry point.
 */
void main(void)
{
    uart_init();
    uart_puts("Boot OK\n");
    uart_puts("UART OK\n");

    irq_init();
    gic_init();
    irq_enable();

    uart_puts("IRQ ready\n");

    task_init_system();
    scheduler_init();

    if (task_create(demo_task) < 0)
    {
        kernel_panic("Failed to create demo_task\n");
    }

    if (task_create(uart_echo_task) < 0)
    {
        kernel_panic("Failed to create uart_echo_task\n");
    }

    uart_puts("Starting scheduler...\n");
    scheduler_start();

    while (1)
    {
        asm volatile("wfe");
    }
}