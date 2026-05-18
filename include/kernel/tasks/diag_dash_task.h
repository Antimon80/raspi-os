#ifndef KERNEL_TASKS_DIAG_TASK_H
#define KERNEL_TASKS_DIAG_TASK_H

void diag_dash_task(void);
void diag_dash_set_task_id(int id);
int diag_dash_get_task_id(void);
void diag_dash_cleanup_resources(void);

#endif