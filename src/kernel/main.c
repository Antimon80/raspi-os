#include "rpi4/uart.h"
#include "rpi4/hdmi.h"
#include "rpi4/mmio.h"
#include "rpi4/gpio.h"
#include "rpi4/i2c.h"
#include "rpi4/i2c_bus.h"
#include "kernel/irq.h"
#include "kernel/sched/task.h"
#include "kernel/sched/scheduler.h"
#include "kernel/debug/panic.h"
#include "kernel/shell/shell.h"
#include "kernel/timer.h"
#include "kernel/memory/heap.h"
#include "kernel/memory/log.h"
#include "kernel/tasks/joystick_task.h"
#include "kernel/tasks/led_task.h"

/*
 * Kernel entry point.
 */
void main(void)
{
    uart_init();

    heap_init();
    log_init();

    i2c_init();
    i2c_bus_init();

    if (hdmi_init())
    {
        hdmi_show_bootscreen();
        hdmi_clear_console();
    }
    else
    {
        uart_puts("HDMI init failed\n");
    }

    task_init_system();
    scheduler_init();

    uart_init_tx_lock();

    uart_puts("Kernel initialized\n");

    int shell_id = task_create_system(shell_task, "shell");
    if (shell_id < 0)
    {
        kernel_panic("Failed to create shell task\n");
    }

    uart_set_rx_task(shell_id);

    int joystick_id = task_create_system(joystick_task, "joystick");
    if (joystick_id < 0)
    {
        kernel_panic("Failed to create joystick task\n");
    }
    joystick_register_task_id(joystick_id);

    int led_id = task_create_system(led_task, "led");
    if (led_id < 0)
    {
        kernel_panic("Failed to create LED task\n");
    }
    led_register_task_id(led_id);

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
