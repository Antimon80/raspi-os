#include "kernel/sched/scheduler.h"
#include "kernel/sched/task.h"
#include "kernel/panic.h"
#include "kernel/timer.h"
#include "kernel/trace.h"
#include "rpi4/uart.h"

/*
 * Low-level context switch routine implemented in assembly.
 *
 * Saves the current task context, stores the old stack pointer
 * in *old_sp, switches to new_sp, restores the next task context,
 * and returns into the new task.
 */
extern void context_switch(uint64_t **old_sp, uint64_t *new_sp);

/* ID of the currently running task, or -1 if none is active yet. */
static int current_task_id = -1;

/* ID of the idle task created during scheduler initialization. */
static int idle_task_id = -1;

static int scheduler_pick_next(void);
static void scheduler_task_exit(void);
static void idle_task(void);

/*
 * Bootstrap function for newly created tasks.
 *
 * A task starts here after its first context switch. The function
 * looks up the currently selected task and calls its entry function.
 * If the task function returns, the task is terminated.
 */
void task_bootstrap(void)
{
    int id = current_task_id;

    if (id < 0)
    {
        kernel_panic("task_bootstrap: invalid current task\n");
    }

    task_t *task = task_get(id);

    if (!task)
    {
        kernel_panic("task_bootstrap: task lookup failed\n");
    }

    if (!task->entry)
    {
        kernel_panic("task_bootstrap: null task entry\n");
    }

    task->entry();
    scheduler_task_exit();

    while (1)
    {
    }
}

/*
 * Idle task.
 *
 * Runs when no other runnable task is available. For now it simply
 * waits for events in an infinite loop.
 */
static void idle_task(void)
{
    while (1)
    {
        asm volatile("wfe");
        scheduler_yield();
    }
}

/*
 * Idle task.
 *
 * Runs when no other runnable task is available. For now it simply
 * waits for events in an infinite loop.
 */
void scheduler_init(void)
{
    current_task_id = -1;
    idle_task_id = task_create_system(idle_task, "idle");

    if (idle_task_id < 0)
    {
        kernel_panic("scheduler_init: failed to create idle task\n");
    }
}

/*
 * Return the ID of the currently running task.
 *
 * Returns -1 if the scheduler has not started yet.
 */
int scheduler_current_task_id(void)
{
    return current_task_id;
}

/*
 * Mark the current task as BLOCKED without yielding immediately.
 *
 * This is used for atomic check-then-block sequences where interrupts
 * are disabled and the caller will yield only after re-enabling them.
 */
void task_block_current_no_yield(void)
{
    int id = current_task_id;

    if (id < 0)
    {
        kernel_panic("task_block_current_no_yield: invalid current task\n");
    }

    task_t *task = task_get(id);

    if (!task)
    {
        kernel_panic("task_block_current_no_yield: task lookup failed\n");
    }

    task->state = BLOCKED;
}

/*
 * Block the current task until an external event wakes it.
 *
 * The task state is set to BLOCKED and the CPU is yielded so another
 * runnable task can be scheduled.
 */
void task_block_current(void)
{
    task_block_current_no_yield();
    scheduler_yield();
}

/*
 * Wake a task that is blocked on an external event.
 *
 * If the task exists and is currently BLOCKED, it becomes READY again.
 */
void task_wakeup(int id)
{
    task_t *task = task_get(id);

    if (!task)
    {
        return;
    }

    if (task->state == BLOCKED)
    {
        task->state = READY;
    }
}

/*
 * Select the next task to run.
 *
 * Uses simple round-robin scheduling:
 * - first tries all READY tasks except the idle task
 * - falls back to the idle task if no normal task is runnable
 *
 * Returns:
 *   task ID of the selected task
 *  -1 if no runnable task exists
 */
static int scheduler_pick_next(void)
{
    int start = current_task_id;

    if (start < 0)
    {
        start = 0;
    }

    for (int offset = 1; offset <= MAX_TASKS; offset++)
    {
        int id = (start + offset) % MAX_TASKS;
        task_t *task = task_get(id);

        if (!task)
        {
            continue;
        }

        if (id == idle_task_id)
        {
            continue;
        }

        if (task->state == READY)
        {
            return id;
        }
    }

    task_t *idle = task_get(idle_task_id);

    if (idle && idle->state == READY)
    {
        return idle_task_id;
    }

    return -1;
}

static void scheduler_reap_safe(void)
{
    task_reap_dying(current_task_id);
}

/*
 * Cooperative yield.
 *
 * The current task voluntarily gives up the CPU. The scheduler selects
 * the next runnable task and performs a context switch.
 */
void scheduler_yield(void)
{
    int prev_id = current_task_id;

    if (prev_id >= 0)
    {
        task_t *prev = task_get(prev_id);

        if (!prev)
        {
            kernel_panic("scheduler_yield: previous task lookup failed\n");
        }

        if (prev->state == RUNNING)
        {
            prev->state = READY;
        }
    }

    scheduler_reap_safe();

    int next_id = scheduler_pick_next();

    if (next_id < 0)
    {
        kernel_panic("scheduler_yield: no runnable task\n");
    }

    if (prev_id == next_id)
    {
        task_t *same = task_get(next_id);

        if (!same)
        {
            kernel_panic("scheduler_yield: same task lookup failed\n");
        }

        same->state = RUNNING;
        return;
    }

    task_t *next = task_get(next_id);

    if (!next)
    {
        kernel_panic("scheduler_yield: next task lookup failed\n");
    }

    next->state = RUNNING;

    // record context switch for diagnostics
    trace_record(TRACE_CTX_SWITCH, prev_id, next_id, 0);
    current_task_id = next_id;

    if (prev_id >= 0)
    {
        task_t *prev = task_get(prev_id);

        if (!prev)
        {
            kernel_panic("scheduler_yield: previous task lookup failed\n");
        }

        context_switch(&prev->sp, next->sp);
    }
    else
    {
        uint64_t *unused_old_sp = 0;
        context_switch(&unused_old_sp, next->sp);
    }
}

/*
 * Terminate the currently running task.
 *
 * Clears the task slot and immediately yields to another runnable task.
 */
static void scheduler_task_exit(void)
{
    int id = current_task_id;

    if (id < 0)
    {
        kernel_panic("scheduler_task_exit: invalid current task\n");
    }

    task_t *task = task_get(id);

    if (!task)
    {
        kernel_panic("scheduler_task_exit: task lookup failed\n");
    }

    task->state = DYING;
    trace_record(TRACE_TASK_EXIT, id, -1, 0);
    scheduler_yield();

    kernel_panic("scheduler_task_exit: returned unexpectedly\n");
}

/*
 * Put the current task to sleep for a given number of ticks.
 *
 * The task is marked as SLEEPING and its wakeup_tick is set
 * relative to the current system tick. It will not be scheduled
 * again until the timer interrupt marks it as READY.
 *
 * The function then yields the CPU so another runnable task
 * can be scheduled.
 *
 * Panics if no valid current task exists.
 */
void task_sleep(uint64_t ticks)
{
    int id = current_task_id;

    if (id < 0)
    {
        kernel_panic("task_sleep: invalid current task\n");
    }

    task_t *task = task_get(id);

    if (!task)
    {
        kernel_panic("task_sleep: task lookup failed\n");
    }

    task->wakeup_tick = timer_get_ticks() + ticks;
    task->state = SLEEPING;
    trace_record(TRACE_TASK_SLEEP, id, -1, (int)ticks);

    scheduler_yield();
}

/*
 * Start the scheduler.
 *
 * Switches to the first runnable task. This function should never return.
 */
void scheduler_start(void)
{
    scheduler_yield();

    kernel_panic("scheduler_start: returned unexpectedly\n");
}