#ifndef KERNEL_TASKS_DEFERRED_WORKER_TASK_H
#define KERNEL_TASKS_DEFERRED_WORKER_TASK_H

void deferred_worker_task(void);
void deferred_worker_register_task_id(int id);
int deferred_worker_get_task_id(void);

#endif