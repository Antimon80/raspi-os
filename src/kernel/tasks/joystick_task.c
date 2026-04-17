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
    if (joystick_init() < 0)
    {
        uart_puts("joystick init failed\n");
        while (1)
        {
            task_block_current();
        }
    }

    joy_menu_init();

    while (1)
    {
        joy_event_t ev;

        // Sleep until GPIO IRQ wakes this task
        task_block_current_no_yield();
        scheduler_yield();

        // One threaded-botto-half pass:
        // reas 0xF2, decode state change, enqueue resulting event
        joystick_service_change();

        // Drain all queued logical events
        while (joystick_has_event())
        {
            ev = joystick_read_event();
            if (ev != JOY_EVENT_NONE)
            {
                joy_menu_handle_event(ev);
            }
        }

        scheduler_yield();
    }
}