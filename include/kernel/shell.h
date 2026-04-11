#ifndef KERNEL_SHELL_H
#define KERNEL_SHELL_H

void shell_task(void);

/* Reusable shell command functions */
void shell_cmd_help(void);
void shell_cmd_ps(void);
void shell_cmd_start_demo(void);
void shell_cmd_stop_id(int id);

/* Helper for menu/other frontends */
int shell_find_task_by_name(const char *name);

#endif