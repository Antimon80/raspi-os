#ifndef KERNEL_TASKS_LED_TASK_H
#define KERNEL_TASKS_LED_TASK_H

#include "sensehat/led_matrix.h"

void led_task(void);
void led_register_task_id(int id);
int led_get_task_id(void);
int led_submit_frame(int task_id, const led_frame_t *frame);
int led_submit_clear_frame(int task_id);
int led_acquire(int task_id);
void led_release(int task_id);

#endif