#include "kernel/sched/task.h"
#include "kernel/memory/log.h"

/*
 * Entry point for newly created tasks.
 *
 * Defined in scheduler.c. A task starts execution here after the
 * first context switch restores its initial stack frame.
 */
extern void task_bootstrap(void);

/* Global task table (statically allocated). */
static task_t tasks[MAX_TASKS];

/* Copy a zero-terminated string into a fixed-size buffer.*/
static void str_copy(char *dst, const char *src, int max_len)
{
    int i = 0;

    if (!dst || max_len < 0)
    {
        return;
    }

    if (!src)
    {
        dst[0] = '\0';
        return;
    }

    while (src[i] != '\0' && i < (max_len - 1))
    {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

/* Reset a task slot to the UNUSED state. */
static void task_clear(task_t *task)
{
    if (!task)
    {
        return;
    }

    task->state = UNUSED;
    task->sp = 0;
    task->entry = 0;
    task->wakeup_tick = 0;
    task->name[0] = '\0';
}

/*
 * Initialize all task slots.
 *
 * Marks every task as UNUSED and clears all fields.
 * Must be called before creating any tasks.
 */
void task_init_system(void)
{
    for (int i = 0; i < MAX_TASKS; i++)
    {
        tasks[i].id = i;
        task_clear(&tasks[i]);
    }
}

/*
 * Return a pointer to the task with the given ID.
 *
 * Returns NULL if the ID is out of range.
 */
task_t *task_get(int id)
{
    if (id < 0 || id >= MAX_TASKS)
    {
        return 0;
    }

    return &tasks[id];
}

/*
 * Create a new task.
 *
 * Initializes a task control block and prepares an artificial
 * initial stack frame so that the task can be started via
 * context_switch().
 *
 * Returns:
 *   task ID on success
 *  -1 if no free slot is available or entry is NULL
 */
int task_create(void (*entry)(void), const char *name)
{
    if (!entry)
    {
        return -1;
    }

    for (int i = 0; i < MAX_TASKS; i++)
    {
        if (tasks[i].state == UNUSED)
        {
            task_t *task = &tasks[i];

            task->entry = entry;
            task->state = READY;
            task->wakeup_tick = 0;
            str_copy(task->name, name, TASK_NAME_LEN);

            // compute top of the task stack
            uint64_t *stack_top = (uint64_t *)(task->stack + TASK_STACK_SIZE);

            // ensure 16-byte alignment as required by AArch64
            stack_top = (uint64_t *)((uintptr_t)stack_top & ~((uintptr_t)0xFUL));

            // Reserve space for callee-saved registers expected by context_switch():
            //
            //  x29, x30
            //  x27, x28
            //  x25, x26
            //  x23, x24
            //  x21, x22
            //  x19, x20
            //
            //  total: 12 x 64-bit values
            stack_top -= 12;

            // initialize all saved register slots to zero
            for (int j = 0; j < 12; j++)
            {
                stack_top[j] = 0;
            }

            // store initial stack pointer in the TCB
            stack_top[1] = (uint64_t)task_bootstrap;

            task->sp = stack_top;

            return task->id;
        }
    }

    return -1;
}

/*
 * Request termination of a task.
 *
 * The task is marked as DYING. It will no longer be scheduled and
 * will be cleaned up later at a scheduler-safe point.
 *
 * Returns 0 on success, -1 on failure.
 */
int task_request_stop(int id)
{
    task_t *task = task_get(id);

    if (!task)
    {
        return -1;
    }

    if (task->state == UNUSED || task->state == DYING)
    {
        return -1;
    }

    task->state = DYING;
    return 0;
}

/*
 * Free all task slots that are marked as DYING.
 *
 * This must only be called from the scheduler at a safe point.
 */
void task_reap_dying(int exclude_id)
{
    for (int i = 0; i < MAX_TASKS; i++)
    {
        if (i == exclude_id)
        {
            continue;
        }

        task_t *task = &tasks[i];

        if (task->state == DYING)
        {
            log_clear_task_id(i);
            task_clear(task);
        }
    }
}