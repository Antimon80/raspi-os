#ifndef KERNEL_TASKS_ENV_DASH_TASK_H
#define KERNEL_TASKS_ENV_DASH_TASK_H

void env_dash_task(void);

void env_dash_set_task_id(int id);
int env_dash_get_task_id(void);
void env_dash_cleanup_resources(void);

#endif