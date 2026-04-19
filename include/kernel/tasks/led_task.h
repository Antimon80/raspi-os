#ifndef KERNEL_TASKS_LED_TASK_H
#define KERNEL_TASKS_LED_TASK_H

void led_register_task_id(int id);
int led_get_task_id(void);
void led_task(void);

#endif