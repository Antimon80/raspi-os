#ifndef KERNEL_SYNC_MUTEX_H
#define KERNEL_SYNC_MUTEX_H

#include <stdint.h>

typedef struct mutex
{
    int locked;
    int owner_task_id;
    uint32_t waiter_mask;
} mutex_t;

void mutx_init(mutex_t *mutex);
int mutex_try_lock(mutex_t *mutex);
void mutex_lock(mutex_t *mutex);
void mutex_unlock(mutex_t *mutex);

#endif