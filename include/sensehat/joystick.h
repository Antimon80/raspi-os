#ifndef SENSEHAT_JOYSTICK_H
#define SENSEHAT_JOYSTICK_H

#include "kernel/shell/joy_menu.h"

#define JOYSTICK_INT_GPIO 23

int joystick_init(void);
joy_event_t joystick_read_event(void);

#endif