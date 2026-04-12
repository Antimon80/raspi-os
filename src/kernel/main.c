#include "rpi4/uart.h"
#include "rpi4/mmio.h"
#include "rpi4/gpio.h"
#include "kernel/irq.h"
#include "kernel/sched/task.h"
#include "kernel/sched/scheduler.h"
#include "kernel/panic.h"
#include "kernel/shell/shell.h"
#include "kernel/timer.h"
#include "kernel/memory/heap.h"
#include "kernel/memory/log.h"
#include "kernel/tasks/joystick_task.h"

/*
 * Kernel entry point.
 */
void main(void)
{
    uart_init();
    uart_puts("Boot OK\n");
    uart_puts("UART OK\n");

    heap_init();
    uart_puts("Heap OK\n");

    log_init();
    uart_puts("Log OK\n");

    task_init_system();
    scheduler_init();

    int shell_id = task_create(shell_task, "shell");

    if (shell_id < 0)
    {
        kernel_panic("Failed to create shell task\n");
    }

    uart_set_rx_task(shell_id);

    if (task_create(joystick_task, "joystick") < 0)
    {
        kernel_panic("Failed to create joystick task\n");
    }

    irq_init();
    gic_init();
    timer_init(100); // 100 Hz = 10 ms per tick

    irq_enable();

    uart_puts("IRQ ready\n");
    uart_puts("Timer ready\n");
    uart_puts("Starting scheduler...\n");

    scheduler_start();

    while (1)
    {
        asm volatile("wfe");
    }
}