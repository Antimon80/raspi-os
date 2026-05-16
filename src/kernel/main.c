#include "rpi4/drivers/uart.h"
#include "rpi4/hdmi/hdmi.h"
#include "rpi4/hdmi/hdmi_draw.h"
#include "rpi4/soc/mmio.h"
#include "rpi4/soc/gpio.h"
#include "rpi4/drivers/i2c.h"
#include "rpi4/drivers/i2c_bus.h"
#include "kernel/irq.h"
#include "kernel/sched/task.h"
#include "kernel/sched/scheduler.h"
#include "kernel/debug/panic.h"
#include "kernel/io/shell.h"
#include "kernel/io/console.h"
#include "kernel/io/hdmi_console.h"
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
    int hdmi_ok = 0;

    uart_init();

    heap_init();
    log_init();

    i2c_init();
    i2c_bus_init();

    if (hdmi_init())
    {
        hdmi_ok = 1;
        hdmi_show_bootscreen();
        hdmi_reset_console();
    }
    else
    {
        uart_puts("HDMI init failed\n");
    }

    task_init_system();
    scheduler_init();

    console_init();

    int shell_id = task_create_system(shell_task, "shell");
    if (shell_id < 0)
    {
        kernel_panic("Failed to create shell task\n");
    }

    uart_set_rx_task(shell_id);

    if (hdmi_ok)
    {
        int hdmi_console_id = task_create_system(hdmi_console_task, "hdmi_console");
        if (hdmi_console_id < 0)
        {
            uart_puts("Failed to create HDMI console task\n");
        }
        else
        {
            hdmi_console_register_task_id(hdmi_console_id);
            hdmi_console_enable(1);
        }
    }

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

    uart_init_tx_lock();

    irq_enable();

    uart_puts("Kernel initialized\n");

    scheduler_start();

    while (1)
    {
        asm volatile("wfe");
    }
}
