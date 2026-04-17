#include "kernel/shell/joy_menu.h"
#include "kernel/sched/scheduler.h"
#include "kernel/sched/task.h"
#include "kernel/irq.h"
#include "sensehat/joystick.h"
#include "rpi4/uart.h"

static int joystick_task_id = -1;

void joystick_register_task_id(int id)
{
    joystick_task_id = id;
}

int joystick_get_task_id(void)
{
    return joystick_task_id;
}

void joystick_task(void)
{
    joystick_irq_set_enabled(0);

    if (joystick_init() < 0)
    {
        uart_puts("joystick init failed\n");
        while (1)
        {
            task_block_current();
        }
    }

    joystick_irq_set_enabled(1);

    joy_menu_init();

    while (1)
    {
        joy_event_t ev;

        irq_disable();

        if (!joystick_has_event())
        {
            task_block_current_no_yield();
            irq_enable();
            scheduler_yield();
            continue;
        }

        ev = joystick_read_event();
        irq_enable();

        if (ev != JOY_EVENT_NONE)
        {
            joy_menu_handle_event(ev);
        }

        scheduler_yield();
    }
}