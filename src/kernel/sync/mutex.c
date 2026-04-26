#include "kernel/sync/mutex.h"
#include "kernel/sched/scheduler.h"
#include "kernel/sched/task.h"
#include "kernel/irq.h"

void mutex_init(mutex_t *mutex)
{
    if (!mutex)
    {
        return;
    }

    mutex->locked = 0;
    mutex->owner = -1;
    wait_queue_init(&mutex->waiters);
}

void mutex_lock(mutex_t *mutex)
{
    int id;
    task_t *task;

    if (!mutex)
    {
        return;
    }

    while (1)
    {
        irq_disable();

        id = scheduler_current_task_id();

        if (!mutex->locked)
        {
            mutex->locked = 1;
            mutex->owner = id;
            irq_enable();
            return;
        }

        task = task_get(id);

        if (!task)
        {
            irq_enable();
            return;
        }

        wait_queue_add_current_irq_disabled(&mutex->waiters);
        task->state = BLOCKED;

        irq_enable();

        scheduler_yield();
    }
}

void mutex_unlock_irq_disabled(mutex_t *mutex)
{
    if (!mutex)
    {
        return;
    }

    mutex->locked = 0;
    mutex->owner = -1;

    wait_queue_wake_task_irq_disabled(&mutex->waiters);
}

void mutex_unlock(mutex_t *mutex)
{
    irq_disable();
    mutex_unlock_irq_disabled(mutex);
    irq_enable();
}