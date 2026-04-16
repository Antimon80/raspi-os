#ifndef KERNEL_DEFERRED_WORK_H
#define KERNEL_DEFERRED_WORK_H

typedef void(*deferred_work_fn_t)(void *arg);

void deferred_work_init(void);
int deferred_work_schedule(deferred_work_fn_t, void *arg);
int deferred_work_has_items(void);
void deferred_work_run_next(void);

#endif