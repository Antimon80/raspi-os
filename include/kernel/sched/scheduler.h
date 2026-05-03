#ifndef KERNEL_SCHED_SCHEDULER_H
#define KERNEL_SCHED_SCHEDULER_H

#include <stdint.h>

void scheduler_init(void);
void scheduler_start(void);
void scheduler_yield(void);
int scheduler_current_task_id(void);

void task_sleep(uint64_t ticks);
void task_block_current(void);
void task_wakeup(int id);
void task_wakeup_irq_disabled(int id);

#endif