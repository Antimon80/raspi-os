#ifndef RPI4_JOYSTICK_H
#define PRI4_JOYSTICK_H

#include "kernel/joy_menu.h"

#define JOYSTICK_INT_GPIO 23

int joystick_init(void);
joy_event_t joystick_read_event(void);

#endif