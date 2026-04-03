#include "rpi4/uart.h"
#include "rpi4/mmio.h"
#include "kernel/irq.h"
#include "kernel/task.h"
#include "kernel/scheduler.h"
#include "kernel/panic.h"
#include "kernel/shell.h"

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

    if (task_create(shell_task, "shell") < 0)
    {
        kernel_panic("Failed to create shell task\n");
    }

    uart_puts("Starting scheduler...\n");
    scheduler_start();

    while (1)
    {
        asm volatile("wfe");
    }
}