#include "kernel/shell/joy_menu.h"
#include "kernel/sched/scheduler.h"
#include "sensehat/joystick.h"
#include "rpi4/uart.h"

extern volatile int joystick_pending;

void joystick_task(void)
{
    joy_menu_init();

    if (joystick_init() < 0)
    {
        uart_puts("joystick init failed\n");

        while (1)
        {
            task_sleep(100);
        }
    }

    while (1)
    {
        if (joystick_pending)
        {
            joystick_pending = 0;

            joy_event_t ev = joystick_read_event();

            if (ev != JOY_EVENT_NONE)
            {
                joy_menu_handle_event(ev);
            }
        }

        task_sleep(1);
    }
}