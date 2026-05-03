#include "kernel/io/joy_menu.h"
#include "kernel/tasks/joystick_task.h"
#include "kernel/sched/scheduler.h"
#include "kernel/sched/task.h"
#include "kernel/irq.h"
#include "sensehat/joystick.h"
#include "rpi4/uart.h"

static int joystick_task_id = -1;
static joystick_event_handler_t joystick_event_handler = 0;

/*
 * Register the task ID of the joystick worker task.
 */
void joystick_register_task_id(int id)
{
    joystick_task_id = id;
}

/*
 * Return the registered joystick worker task ID, or -1 if none exists.
 */
int joystick_get_task_id(void)
{
    return joystick_task_id;
}

int joystick_set_event_handler(joystick_event_handler_t handler)
{
    if (!handler)
    {
        return -1;
    }

    joystick_event_handler = handler;
    return 0;
}

void joystick_clear_event_handler(void)
{
    joystick_event_handler = 0;
}

/*
 * Sense HAT joystick task.
 *
 * If the joystick is not present, the task exits after reporting that the
 * optional device is unavailable. Otherwise it waits for IRQ-driven state
 * changes, polls the joystick state over I2C, and forwards decoded events
 * to the joystick menu frontend.
 */
void joystick_task(void)
{
    if (joystick_init() < 0)
    {
        while (1)
        {
            task_block_current();
        }
    }

    joy_menu_init();

    while (1)
    {
        joy_event_t ev;
        int id;
        task_t *task;

        while (joystick_consume_irq())
        {
            joystick_service_change();
        }

        while (joystick_has_event())
        {
            ev = joystick_read_event();
            if (ev != JOY_EVENT_NONE)
            {
                if (joystick_event_handler)
                {
                    joystick_event_handler(ev);
                }
                else
                {
                    joy_menu_handle_event(ev);
                }
            }
        }

        irq_disable();

        id = scheduler_current_task_id();
        if (id < 0)
        {
            irq_enable();
            continue;
        }

        task = task_get(id);
        if (!task)
        {
            irq_enable();
            continue;
        }

        if (joystick_has_event())
        {
            irq_enable();
            continue;
        }

        if (joystick_consume_irq())
        {
            irq_enable();
            joystick_service_change();
            continue;
        }

        task->state = BLOCKED;
        irq_enable();
        scheduler_yield();
    }
}
