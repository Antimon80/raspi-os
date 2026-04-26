#ifndef KERNEL_SYNC_COND_H
#define KERNEL_SYNC_COND_H

#include "kernel/sync/wait_queue.h"
#include "kernel/sync/mutex.h"

typedef struct cond
{
    wait_queue_t waiters;
} cond_t;

void cond_init(cond_t *cond);
void cond_wait(cond_t *cond, mutex_t *mutex);
void cond_wait_irq_disabled(cond_t *cond, mutex_t *mutex);
void cond_signal(cond_t *cond);
void cond_signal_irq_disabled(cond_t *cond);

#endif