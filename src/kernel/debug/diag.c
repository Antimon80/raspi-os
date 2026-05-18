#include "kernel/debug/diag.h"
#include "kernel/timer.h"
#include "kernel/irq.h"
#include "util/string.h"

static uint64_t last_runtime_ticks[MAX_TASKS];
static int initialized = 0;

void diag_init(void)
{
    irq_disable();

    for (int i = 0; i < MAX_TASKS; i++)
    {
        task_t *task = task_get(i);
        last_runtime_ticks[i] = task ? task->runtime_ticks : 0u;
    }

    initialized = 1;

    irq_enable();
}

static unsigned int diag_compute_busy_percent(uint64_t total_delta, uint64_t idle_delta)
{
    if (total_delta == 0u)
    {
        return 0u;
    }

    if (idle_delta >= total_delta)
    {
        return 0u;
    }

    return (unsigned int)(((total_delta - idle_delta) * 100u) / total_delta);
}

void diag_get_snapshot(diag_snapshot_t *snapshot)
{
    uint64_t total_delta = 0u;
    uint64_t idle_delta = 0u;
    unsigned int task_count = 0u;

    if (!snapshot)
    {
        return;
    }

    if (!initialized)
    {
        diag_init();
    }

    snapshot->tick = timer_get_ticks();
    snapshot->total_runtime_delta_ticks = 0u;
    snapshot->idle_runtime_delta_ticks = 0u;
    snapshot->cpu_busy_percent = 0u;
    snapshot->task_count = 0u;

    heap_get_stats(&snapshot->heap);

    irq_disable();

    for (int i = 0; i < MAX_TASKS; i++)
    {
        task_t *task = task_get(i);
        uint64_t runtime;
        uint64_t delta;

        if (!task || task->state == UNUSED)
        {
            continue;
        }

        runtime = task->runtime_ticks;
        delta = runtime - last_runtime_ticks[i];
        last_runtime_ticks[i] = runtime;

        total_delta += delta;

        if (str_equals(task->name, "idle"))
        {
            idle_delta += delta;
        }

        if (task_count < MAX_TASKS)
        {
            diag_task_info_t *info = &snapshot->tasks[task_count];

            info->id = task->id;
            info->state = task->state;
            info->flags = task->flag;
            info->runtime_ticks = runtime;
            info->runtime_delta_ticks = delta;
            info->switch_count = task->switch_count;
            info->stack_used_bytes = task_get_stack_used_bytes(i);
            info->stack_free_bytes = task_get_stack_free_bytes(i);
            info->name = task->name;

            task_count++;
        }
    }

    irq_enable();

    snapshot->task_count = task_count;
    snapshot->total_runtime_delta_ticks = total_delta;
    snapshot->idle_runtime_delta_ticks = idle_delta;
    snapshot->cpu_busy_percent = diag_compute_busy_percent(total_delta, idle_delta);
}