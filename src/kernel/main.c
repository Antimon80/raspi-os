#include "rpi4/uart.h"
#include "rpi4/mmio.h"
#include "rpi4/gpio.h"
#include "kernel/irq.h"
#include "kernel/task.h"
#include "kernel/scheduler.h"
#include "kernel/panic.h"
#include "kernel/shell.h"
#include "kernel/timer.h"
#include "kernel/tasks/joystick_task.h"

/*
 * Kernel entry point.
 */
void main(void)
{
    uart_init();
    uart_puts("Boot OK\n");
    uart_puts("UART OK\n");

    task_init_system();
    scheduler_init();

    if (task_create(shell_task, "shell") < 0)
    {
        kernel_panic("Failed to create shell task\n");
    }
    if(task_create(joystick_task, "joystick") < 0){
        kernel_panic("Failed to create joystick task\n");
    }

    irq_init();
    gic_init();
    timer_init(100); // 100 Hz = 10 ms per tick

    gpio_use_as_input(23);
    gpio_set_pull(23, GPIO_PULL_NONE);

    gpio_clear_event(23);
    gpio_enable_rising_edge(23);
    gpio_enable_falling_edge(23);

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