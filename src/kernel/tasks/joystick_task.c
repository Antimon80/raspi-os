#include "kernel/shell/joy_menu.h"
#include "kernel/sched/scheduler.h"
#include "kernel/sched/task.h"
#include "kernel/irq.h"
#include "sensehat/joystick.h"
#include "rpi4/uart.h"

static int joystick_task_id = -1;

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
        uart_puts("joystick task: Sense HAT joystick not detected, disabling task\n");
        return;
    }

    joy_menu_init();

    while (1)
    {
        joy_event_t ev;
        int id;
        task_t *task;

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

        task->state = BLOCKED;
        irq_enable();
        scheduler_yield();

        /*
         * Run one deferred joystick service pass:
         * read register 0xF2, decode the state transition, and enqueue
         * the resulting logical event.
         */
        joystick_service_change();

        /*
         * Drain all queued logical events and forward them to the menu.
         */
        while (joystick_has_event())
        {
            ev = joystick_read_event();
            if (ev != JOY_EVENT_NONE)
            {
                joy_menu_handle_event(ev);
            }
        }
    }
}
