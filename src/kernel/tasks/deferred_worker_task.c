#include "kernel/tasks/deferred_worker_task.h"
#include "kernel/sync/deferred_work.h"
#include "kernel/sched/task.h"
#include "kernel/sched/scheduler.h"
#include "kernel/irq.h"

static int deferred_worker_task_id = -1;

void deferred_worker_register_task_id(int id)
{
    deferred_worker_task_id = id;
}

int deferred_worker_get_task_id(void)
{
    return deferred_worker_task_id;
}

void deferred_worker_task(void)
{
    while (1)
    {
        int id;
        task_t *task;

        irq_disable();

        if (!deferred_work_has_items())
        {
            id = scheduler_current_task_id();
            if (id < 0)
            {
                irq_enable();
                continue;
            }

            task = task_get(id);
            if (!task)
            {
                irq_enable();
                continue;
            }

            task->state = BLOCKED;
            irq_enable();
            scheduler_yield();
            continue;
        }

        irq_enable();

        deferred_work_run_next();
        scheduler_yield();
    }
}