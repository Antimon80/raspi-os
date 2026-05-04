#ifndef KERNEL_TASKS_ENV_STATUS_TASK_H
#define KERNEL_TASKS_ENV_STATUS_TASK_H

void env_status_register_task_id(int id);
int env_status_get_task_id(void);
void env_status_task(void);

#endif