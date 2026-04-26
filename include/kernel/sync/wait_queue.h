#ifndef KERNEL_SYNC_WAIT_QUEUE_H
#define KERNEL_SYNC_WAIT_QUEUE_H

#include <stdint.h>

typedef struct wait_queue
{
    uint32_t waiter_mask;
} wait_queue_t;

void wait_queue_init(wait_queue_t *queue);

void wait_queue_add_current_irq_disabled(wait_queue_t *queue);
int wait_queue_wake_task_irq_disabled(wait_queue_t *queue);

#endif