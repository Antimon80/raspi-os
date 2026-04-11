#include "rpi4/uart.h"
#include "rpi4/mmio.h"
#include "rpi4/gpio.h"
#include "kernel/irq.h"
#include "kernel/task.h"
#include "kernel/scheduler.h"
#include "kernel/panic.h"
#include "kernel/shell.h"
#include "kernel/timer.h"
#include "kernel/heap.h"
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

    void *a = kmalloc(64);
    void *b = kmalloc(128);

    if (!a || !b)
    {
        kernel_panic("Heap test allocation failed");
    }

    uart_puts("Heap test allocations OK\n");
    heap_dump();

    kfree(a);
    kfree(b);

    uart_puts("Heap test free OK\n");
    heap_dump();

    // uart_puts("Heap OK\n");

    task_init_system();
    scheduler_init();

    if (task_create(shell_task, "shell") < 0)
    {
        kernel_panic("Failed to create shell task\n");
    }
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