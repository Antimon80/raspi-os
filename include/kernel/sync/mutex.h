#ifndef KERNEL_SYNC_MUTEX_H
#define KERNEL_SYNC_MUTEX_H

#include "kernel/sync/wait_queue.h"

typedef struct mutex
{
    int locked;
    int owner;
    wait_queue_t waiters;
} mutex_t;

void mutex_init(mutex_t *mutex);
void mutex_lock(mutex_t *mutex);
void mutex_unlock(mutex_t *mutex);

void mutex_unlock_irq_disabled(mutex_t *mutex);

#endif