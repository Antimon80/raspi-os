#include "kernel/sync/cond.h"
#include "kernel/sched/scheduler.h"
#include "kernel/sched/task.h"
#include "kernel/irq.h"

/*
 * Initialize an empty condition variable wait queue.
 */
void cond_init(cond_t *cond)
{
    if (!cond)
    {
        return;
    }

    wait_queue_init(&cond->waiters);
}

/*
 * Wait on a condition variable.
 * The caller must hold mutex.
 *
 * Atomically queues the current task, marks it BLOCKED, releases mutex,
 * yields, and re-acquires mutex before returning.
 */
void cond_wait(cond_t *cond, mutex_t *mutex)
{
    int id;
    task_t *task;

    if (!cond || !mutex)
    {
        return;
    }

    irq_disable();

    id = scheduler_current_task_id();

    if (id < 0)
    {
        irq_enable();
        return;
    }

    task = task_get(id);

    if (!task)
    {
        irq_enable();
        return;
    }

    wait_queue_add_current_irq_disabled(&cond->waiters);
    task->state = BLOCKED;

    mutex_unlock_irq_disabled(mutex);

    irq_enable();

    scheduler_yield();

    mutex_lock(mutex);
}

/*
 * Same as cond_wait(), but expects IRQs to already be disabled.
 *
 * Used when the caller has already entered an IRQ-protected critical section.
 * Re-enables IRQs before yielding.
 */
void cond_wait_irq_disabled(cond_t *cond, mutex_t *mutex){
    int id;
    task_t *task;

    if(!cond || !mutex){
        irq_enable();
        return;
    }

    id = scheduler_current_task_id();

    if(id < 0){
        irq_enable();
        return;
    }

    task = task_get(id);

    if(!task){
        irq_enable();
        return;
    }

    wait_queue_add_current_irq_disabled(&cond->waiters);
    task->state = BLOCKED;

    mutex_unlock_irq_disabled(mutex);

    irq_enable();

    scheduler_yield();

    mutex_lock(mutex);
}

/*
 * Wake one task waiting on this condition variable.
 * Expects IRQs to already be disabled.
 */
void cond_signal_irq_disabled(cond_t *cond)
{
    if (!cond)
    {
        return;
    }

    wait_queue_wake_task_irq_disabled(&cond->waiters);
}

/*
 * Wake one task waiting on this condition variable.
 */
void cond_signal(cond_t *cond)
{
    irq_disable();
    cond_signal_irq_disabled(cond);
    irq_enable();
}