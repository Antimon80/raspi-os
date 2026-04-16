#ifndef KERNEL_TASKS_JOYSTICK_TASK_H
#define KERNEL_TASKS_JOYSTICK_TASK_H

void joystick_task(void);
void joystick_register_task_id(int id);
int joystick_get_task_id(void);

#endif