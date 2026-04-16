#ifndef KERNEL_SHELL_SHELL_H
#define KERNEL_SHELL_SHELL_H

void shell_task(void);

void shell_cmd_help(void);
void shell_cmd_ps(void);
void shell_cmd_trace_dump(void);
void shell_cmd_start_arg(const char *name);
void shell_cmd_stop_arg(const char *name);
void shell_cmd_stop_id(int id);
int shell_find_task_by_name(const char *name);

#endif