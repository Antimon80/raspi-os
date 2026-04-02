#ifndef KERNEL_SCHEDULER_H
#define KERNEL_SCHEDULER_H

void scheduler_init(void);
void scheduler_start(void);
void scheduler_yield(void);
int scheduler_current_task_id(void);

#endif