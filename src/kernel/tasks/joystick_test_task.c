#include "kernel/tasks/joystick_test_task.h"
#include "kernel/joy_menu.h"
#include "kernel/scheduler.h"

void joystick_test_task(void)
{
    joy_menu_init();

    while (1)
    {
        /* long press: open menu */
        joy_menu_handle_event(JOY_EVENT_CENTER_PRESS);
        task_sleep(60);
        joy_menu_handle_event(JOY_EVENT_CENTER_RELEASE);
        task_sleep(30);

        /* move to "ps" */
        joy_menu_handle_event(JOY_EVENT_DOWN);
        task_sleep(30);

        /* short press: execute */
        joy_menu_handle_event(JOY_EVENT_CENTER_PRESS);
        task_sleep(10);
        joy_menu_handle_event(JOY_EVENT_CENTER_RELEASE);
        task_sleep(100);

        /* long press: close menu */
        joy_menu_handle_event(JOY_EVENT_CENTER_PRESS);
        task_sleep(60);
        joy_menu_handle_event(JOY_EVENT_CENTER_RELEASE);
        task_sleep(100);
    }
}