#include "kernel/joy_menu.h"
#include "kernel/scheduler.h"

extern volatile int joystick_pending;

// will be replaced with I2C driver
static joy_event_t joystick_read_event(void){
    return JOY_EVENT_NONE;
}

void joystick_task(void){
    joy_menu_init();

    while(1){
        if(joystick_pending){
            joystick_pending = 0;

            joy_event_t ev = joystick_read_event();

            if(ev != JOY_EVENT_NONE){
                joy_menu_handle_event(ev);
            }
        }

        task_sleep(1);
    }
}