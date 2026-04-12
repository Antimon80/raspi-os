#include "kernel/memory/log.h"
#include "kernel/memory/heap.h"
#include "kernel/sched/scheduler.h"
#include "kernel/sched/task.h"
#include "rpi4/uart.h"

typedef struct log_entry
{
    char *message;
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
 * Return the length of a zero-terminated string.
 */
static int str_length(const char *s)
{
    int len = 0;

    if (!s)
    {
        return 0;
    }

    while (s[len] != '\0')
    {
        len++;
    }

    return len;
}

/*
 * Copy a zero-terminated string into the destination buffer.
 *
 * The caller must ensure that dst is large enough.
 */
static void str_copy(char *dst, const char *src)
{
    int i = 0;

    if (!dst)
    {
        return;
    }

    if (!src)
    {
        dst[0] = '\0';
        return;
    }

    while (src[i] != '\0')
    {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

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
 * Append a message to the log of the given task ID.
 */
int log_append_task_id(int task_id, const char *message)
{
    task_log_t *log;
    log_entry_t *entry;
    char *copy;
    int len;

    if (!message)
    {
        return -1;
    }

    log = log_get_task_log(task_id);

    if (!log)
    {
        return -1;
    }

    len = str_length(message);

    entry = (log_entry_t *)kmalloc_zero(sizeof(log_entry_t));
    if (!entry)
    {
        return -1;
    }

    copy = (char *)kmalloc((size_t)len + 1);
    if (!copy)
    {
        kfree(entry);
        return -1;
    }

    str_copy(copy, message);

    entry->message = copy;
    entry->next = 0;

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
 * Append a message to the log of the given task.
 */
int log_append_task(task_t *task, const char *message)
{
    if (!task)
    {
        return -1;
    }

    return log_append_task_id(task->id, message);
}

/*
 * Append a message to the log of the currently running task.
 */
int log_append_current_task(const char *message)
{
    int id = scheduler_current_task_id();

    if (id < 0)
    {
        return -1;
    }

    return log_append_task_id(id, message);
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
        uart_puts("invalid task id\n");
        return;
    }

    if (!log->head)
    {
        uart_puts("log is empty\n");
        return;
    }

    current = log->head;

    while (current)
    {
        if (current->message)
        {
            uart_puts(current->message);
        }
        else
        {
            uart_puts("(null)");
        }

        uart_puts("\n");
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