#include "kernel/memory/log.h"
#include "kernel/memory/heap.h"
#include "kernel/sched/scheduler.h"
#include "kernel/sched/task.h"
#include "kernel/io/console.h"
#include "rpi4/drivers/uart.h"
#include "util/string.h"

#define LOG_MAX_ENTRIES_PER_TASK 32

typedef enum
{
    LOG_ENTRY_TEXT = 0,
    LOG_ENTRY_INT
} log_entry_type_t;

typedef struct log_entry
{
    log_entry_type_t type;
    char *message;
    int value;
    struct log_entry *next;
} log_entry_t;

typedef struct task_log
{
    log_entry_t *head;
    log_entry_t *tail;
    int count;
} task_log_t;

/* One log list per task slot. */
static task_log_t task_logs[MAX_TASKS];

/*
 * Return the per-task log structure for a valid task ID.
 *
 * Returns NULL if the ID is out of range.
 */
static task_log_t *log_get_task_log(int task_id)
{
    if (task_id < 0 || task_id >= MAX_TASKS)
    {
        return 0;
    }

    return &task_logs[task_id];
}

/*
 * Remove the oldest log entry from a task log.
 */
static void log_drop_oldest(task_log_t *log)
{
    log_entry_t *old;

    if (!log || !log->head)
    {
        return;
    }

    old = log->head;
    log->head = old->next;

    if (!log->head)
    {
        log->tail = 0;
    }

    if (old->message)
    {
        kfree(old->message);
    }

    kfree(old);

    if (log->count > 0)
    {
        log->count--;
    }
}

/*
 * Initialize all task log lists.
 */
void log_init(void)
{
    for (int i = 0; i < MAX_TASKS; i++)
    {
        task_logs[i].head = 0;
        task_logs[i].tail = 0;
        task_logs[i].count = 0;
    }
}

/*
 * Append a log entry to the log of the given task ID.
 *
 * The message may be NULL. The integer value is stored directly
 * in the log entry.
 */
int log_append_task_id(int task_id, const char *message, int value)
{
    task_log_t *log;
    log_entry_t *entry;
    char *copy = 0;
    int len = 0;

    log = log_get_task_log(task_id);

    if (!log)
    {
        return -1;
    }

    if (message)
    {
        len = str_length(message);

        copy = (char *)kmalloc((size_t)len + 1);
        if (!copy)
        {
            return -1;
        }

        str_copy(copy, message, len + 1);
    }

    entry = (log_entry_t *)kmalloc_zero(sizeof(log_entry_t));
    if (!entry)
    {
        if (copy)
        {
            kfree(copy);
        }

        return -1;
    }

    entry->message = copy;
    entry->value = value;
    entry->next = 0;

    if (log->count >= LOG_MAX_ENTRIES_PER_TASK)
    {
        log_drop_oldest(log);
    }

    if (!log->head)
    {
        log->head = entry;
        log->tail = entry;
    }
    else
    {
        log->tail->next = entry;
        log->tail = entry;
    }

    log->count++;

    return 0;
}

/*
 * Append a log entry to the log of the given task.
 */
int log_append_task(task_t *task, const char *message, int value)
{
    if (!task)
    {
        return -1;
    }

    return log_append_task_id(task->id, message, value);
}

/*
 * Append a log entry to the log of the currently running task.
 */
int log_append_current_task(const char *message, int value)
{
    int id = scheduler_current_task_id();

    if (id < 0)
    {
        return -1;
    }

    return log_append_task_id(id, message, value);
}

/*
 * Print the log of the given task ID to UART.
 */
void log_dump_task_id(int task_id)
{
    task_log_t *log;
    log_entry_t *current;

    log = log_get_task_log(task_id);

    if (!log)
    {
        console_puts("invalid task id\n");
        return;
    }

    if (!log->head)
    {
        console_puts("log is empty\n");
        return;
    }

    current = log->head;

    while (current)
    {
        if (current->message)
        {
            console_puts(current->message);
        }

        if (current->value < 0)
        {
            console_putc('-');
            console_put_uint((unsigned int)(-current->value));
        }
        else if (current->value > 0)
        {
            console_put_uint((unsigned int)current->value);
        }

        if (!current->message && current->value == 0)
        {
            console_puts("(empty)");
        }

        console_puts("\n");
        current = current->next;
    }
}

/*
 * Clear the log of the given task ID and free all associated memory.
 */
void log_clear_task_id(int task_id)
{
    task_log_t *log;
    log_entry_t *current;
    log_entry_t *next;

    log = log_get_task_log(task_id);

    if (!log)
    {
        return;
    }

    current = log->head;

    while (current)
    {
        next = current->next;

        if (current->message)
        {
            kfree(current->message);
        }

        kfree(current);
        current = next;
    }

    log->head = 0;
    log->tail = 0;
    log->count = 0;
}