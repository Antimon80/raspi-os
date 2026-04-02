#include "kernel/task.h"

/*
 * Entry point for newly created tasks.
 *
 * Defined in scheduler.c. A task starts execution here after the
 * first context switch restores its initial stack frame.
 */
extern void task_bootstrap(void);

/* Global task table (statically allocated). */
static task_t tasks[MAX_TASKS];

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
        tasks[i].state = UNUSED;
        tasks[i].sp = 0;
        tasks[i].entry = 0;
        tasks[i].wakeup_tick = 0;
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
int task_create(void (*entry)(void))
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