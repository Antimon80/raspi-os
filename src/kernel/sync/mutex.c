#include "kernel/sync/mutex.h"
#include "kernel/irq.h"
#include "kernel/debug/panic.h"
#include "kernel/sched/task.h"
#include "kernel/sched/scheduler.h"

/*
 * Return the next valid waiter task ID, or -1 if none exists.
 *
 * Stale waiter bits are cleaned up while scanning.
 */
static int mutex_find_waiter(mutex_t *mutex)
{
    int current = scheduler_current_task_id();
    int start = (current < 0) ? 0 : current;

    for (int i = 1; i <= MAX_TASKS; i++)
    {
        int id = (start + i) % MAX_TASKS;

        if ((mutex->waiter_mask & (1u << id)) == 0)
        {
            continue;
        }

        task_t *task = task_get(id);
        if (!task || task->state == UNUSED || task->state == DYING)
        {
            mutex->waiter_mask &= ~(1u << id);
            continue;
        }

        if (task->state == BLOCKED)
        {
            return id;
        }

        mutex->waiter_mask &= ~(1u << id);
    }

    return -1;
}

/*
 * Initialize a mutex to the unlocked state.
 */
void mutex_init(mutex_t *mutex)
{
    if (!mutex)
    {
        return;
    }

    mutex->locked = 0;
    mutex->owner_task_id = -1;
    mutex->waiter_mask = 0;
}

/*
 * Try to acquire the mutex without blocking.
 *
 * Returns:
 *   1 on success
 *   0 if the mutex is already locked
 *
 * Panics if called without a current task.
 */
int mutex_try_lock(mutex_t *mutex)
{
    int current_id = scheduler_current_task_id();
    if (current_id < 0)
    {
        kernel_panic("mutex_try_lock: no current task\n");
    }

    if (!mutex)
    {
        return 0;
    }

    irq_disable();

    if (!mutex->locked)
    {
        mutex->locked = 1;
        mutex->owner_task_id = current_id;
        irq_enable();
        return 1;
    }

    if (mutex->owner_task_id == current_id)
    {
        irq_enable();
        kernel_panic("mutex_try_lock: recursive lock\n");
    }

    irq_enable();
    return 0;
}

/*
 * Acquire a mutex, blocking the current task if necessary.
 *
 * This mutex is non-recursive. Locking it twice from the same task panics.
 * Must only be used from task context, never from IRQ context.
 */
void mutex_lock(mutex_t *mutex)
{
    int current_id = scheduler_current_task_id();
    if (current_id < 0)
    {
        kernel_panic("mutex_lock: no current task\n");
    }

    if (!mutex)
    {
        kernel_panic("mutex_lock: mutex is null\n");
    }

    while (1)
    {
        irq_disable();

        if (!mutex->locked)
        {
            mutex->locked = 1;
            mutex->owner_task_id = current_id;
            irq_enable();
            return;
        }

        if (mutex->owner_task_id == current_id)
        {
            irq_enable();
            kernel_panic("mutex_lock: recursive lock\n");
        }

        task_t *current = task_get(current_id);
        if (!current)
        {
            irq_enable();
            kernel_panic("mutex_lock: task lookup failed\n");
        }

        mutex->waiter_mask |= (1u << current_id);
        current->state = BLOCKED;

        irq_enable();
        scheduler_yield();
    }
}

/*
 * Release a mutex and wake one waiting task, if present.
 *
 * Panics if the current task does not own the mutex.
 */
void mutex_unlock(mutex_t *mutex)
{
    int current_id = scheduler_current_task_id();
    if (current_id < 0)
    {
        kernel_panic("mutex_unlock: no current task\n");
    }

    if (!mutex)
    {
        kernel_panic("mutex_unlock: mutex is null\n");
    }

    irq_disable();

    if (!mutex->locked)
    {
        irq_enable();
        kernel_panic("mutex_unlock: mutex not locked\n");
    }

    if (mutex->owner_task_id != current_id)
    {
        irq_enable();
        kernel_panic("mutex_unlock: current task is not owner\n");
    }

    mutex->locked = 0;
    mutex->owner_task_id = -1;

    int waiter_id = mutex_find_waiter(mutex);
    if (waiter_id >= 0)
    {
        task_t *waiter = task_get(waiter_id);
        if (waiter && waiter->state == BLOCKED)
        {
            waiter->state = READY;
        }

        mutex->waiter_mask &= ~(1u << waiter_id);
    }

    irq_enable();
}