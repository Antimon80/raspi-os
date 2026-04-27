#ifndef KERNEL_TASKS_JOYSTICK_TASK_H
#define KERNEL_TASKS_JOYSTICK_TASK_H

#include "kernel/io/joy_menu.h"

typedef void (*joystick_event_handler_t)(joy_event_t event);

void joystick_task(void);
void joystick_register_task_id(int id);
int joystick_get_task_id(void);
void joystick_set_event_handler(joystick_event_handler_t handler);
void joystick_clear_event_handler(void);

#endif
