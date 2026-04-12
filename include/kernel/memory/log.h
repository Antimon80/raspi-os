#ifndef KERNEL_MEMORY_LOG_H
#define KERNEL_MEMORY_LOG_H

#include "kernel/sched/task.h"

void log_init(void);
int log_append_task_id(int task_id, const char *message);
int log_append_task(task_t *task, const char *message);
int log_append_current_task(const char *message);
void log_dump_task_id(int task_id);
void log_clear_task_id(int task_id);

#endif