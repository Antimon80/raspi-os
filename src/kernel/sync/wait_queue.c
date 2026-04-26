#include "kernel/sync/wait_queue.h"
#include "kernel/sched/scheduler.h"
#include "kernel/sched/task.h"

/*
 * Initialize an empty wait queue.
 */
void wait_queue_init(wait_queue_t *queue)
{
    if (!queue)
    {
        return;
    }

    queue->waiter_mask = 0;
}

/*
 * Add the current task to the wait queue.
 * Expects IRQs to already be disabled.
 */
void wait_queue_add_current_irq_disabled(wait_queue_t *queue)
{
    int id;

    if (!queue)
    {
        return;
    }

    id = scheduler_current_task_id();

    if (id < 0)
    {
        return;
    }

    queue->waiter_mask |= (1u << id);
}

/*
 * Wake one queued task.
 * Expects IRQs to already be disabled.
 *
 * Returns the woken task ID, or -1 if no blocked waiter exists.
 */
int wait_queue_wake_task_irq_disabled(wait_queue_t *queue)
{
    if (!queue)
    {
        return -1;
    }

    for (int i = 0; i < MAX_TASKS; i++)
    {
        if ((queue->waiter_mask & (1u << i)) == 0)
        {
            continue;
        }

        queue->waiter_mask &= ~(1u << i);

        task_t *task = task_get(i);

        if (task && task->state == BLOCKED)
        {
            task->state = READY;
            return i;
        }
    }

    return -1;
}