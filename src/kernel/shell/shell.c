#include "kernel/shell/shell.h"
#include "kernel/sched/task.h"
#include "kernel/sched/scheduler.h"
#include "kernel/memory/heap.h"
#include "kernel/memory/log.h"
#include "kernel/tasks/demo_task.h"
#include "kernel/trace.h"
#include "rpi4/uart.h"
#include "util/string.h"
#include "util/convert.h"

#define SHELL_BUFFER_SIZE 64

typedef struct
{
    const char *name;
    void (*entry)(void);
} startable_task_t;

/*
 * List of task types that may be started from the shell.
 */
static const startable_task_t startable_tasks[] = {
    {"heart", heartbeat_task},
    {"fast", worker_fast_task},
    {"slow", worker_slow_task},
    {"burst", burst_task}};

/*
 * Print a textual representation of a task state.
 */
static void shell_print_task_state(task_state_t state)
{
    switch (state)
    {
    case UNUSED:
        uart_puts("UNUSED");
        break;
    case READY:
        uart_puts("READY");
        break;
    case RUNNING:
        uart_puts("RUNNING");
        break;
    case BLOCKED:
        uart_puts("BLOCKED");
        break;
    case DYING:
        uart_puts("DYING");
        break;
    case SLEEPING:
        uart_puts("SLEEPING");
        break;
    default:
        uart_puts("UNKNOWN");
        break;
    }
}

/*
 * Find a startable task type by name.
 *
 * Returns a pointer to the registry entry on success, NULL otherwise.
 */
static const startable_task_t *shell_find_startable_task(const char *name)
{
    int count = (int)(sizeof(startable_tasks) / sizeof(startable_tasks[0]));

    for (int i = 0; i < count; i++)
    {
        if (str_equals(startable_tasks[i].name, name))
        {
            return &startable_tasks[i];
        }
    }

    return 0;
}

/*
 * Resolve a task argument that may either be a numeric task ID or a task name.
 *
 * Returns 0 on success and stores the resolved ID in *task_id.
 * Returns -1 on failure.
 */
static int shell_resolve_task_arg(const char *arg, int *task_id)
{
    int id;

    if (!arg || !*arg || !task_id)
    {
        return -1;
    }

    if (parse_uint(arg, &id) == 0)
    {
        *task_id = id;
        return 0;
    }

    id = shell_find_task_by_name(arg);

    if (id < 0)
    {
        return -1;
    }

    *task_id = id;

    return 0;
}

/*
 * Find a non-unused task by name.
 *
 * Returns the task ID on success, -1 if not found.
 */
int shell_find_task_by_name(const char *name)
{
    for (int i = 0; i < MAX_TASKS; i++)
    {
        task_t *task = task_get(i);

        if (!task)
        {
            continue;
        }

        if (task->state == UNUSED)
        {
            continue;
        }

        if (str_equals(task->name, name))
        {
            return task->id;
        }
    }

    return -1;
}

/*
 * Print a short help text.
 */
void shell_cmd_help(void)
{
    uart_puts("Commands: \n");
    uart_puts("  help\n");
    uart_puts("  heap dump\n");
    uart_puts("  heap stats\n");
    uart_puts("  log <id|name>\n");
    uart_puts("  log clear <id|name>\n");
    uart_puts("  ps\n");
    uart_puts("  startable\n");
    uart_puts("  start <name>\n");
    uart_puts("  stop <id|name>\n");
    uart_puts("  trace dump\n");
    uart_puts("  trace clear\n");
}

/*
 * Print all non-unused tasks.
 */
void shell_cmd_ps(void)
{
    uart_puts("ID  STATE     NAME\n");

    for (int i = 0; i < MAX_TASKS; i++)
    {
        task_t *task = task_get(i);

        if (!task)
        {
            continue;
        }

        if (task->state == UNUSED)
        {
            continue;
        }

        uart_put_uint((unsigned int)task->id);
        uart_puts("   ");
        shell_print_task_state(task->state);
        uart_puts("   ");
        uart_puts(task->name);
        uart_puts("\n");
    }
}

/*
 * Print all startable tasks.
 */
static void shell_cmd_startable(void)
{
    int count = (int)(sizeof(startable_tasks) / sizeof(startable_tasks[0]));

    uart_puts("Startable tasks:\n");

    for (int i = 0; i < count; i++)
    {
        uart_puts("  ");
        uart_puts(startable_tasks[i].name);
        uart_puts("\n");
    }
}

/*
 * Start a task by its registered shell name.
 */
void shell_cmd_start_arg(const char *name)
{
    const startable_task_t *entry;
    int existing;
    int id;

    if (!name || !*name)
    {
        uart_puts("missing task name\n");
        return;
    }

    entry = shell_find_startable_task(name);

    if (!entry)
    {
        uart_puts("unknown task name\n");
        return;
    }

    existing = shell_find_task_by_name(name);

    if (existing >= 0)
    {
        uart_puts("task already exists with id ");
        uart_put_uint((unsigned int)existing);
        uart_puts("\n");
        return;
    }

    id = task_create(entry->entry, entry->name, 0);

    if (id < 0)
    {
        uart_puts("failed to create task\n");
        return;
    }

    uart_puts("task started with id ");
    uart_put_uint((unsigned int)id);
    uart_puts("\n");
}

/*
 * Reusable command implementation that can be called both from the
 * UART shell and from other frontends such as a joystick-controlled menu.
 */
void shell_cmd_stop_id(int id)
{
    task_t *task = task_get(id);

    if (!task || task->state == UNUSED)
    {
        uart_puts("task not found\n");
        return;
    }

    if (task->flag & TASK_FLAG_SYSTEM)
    {
        uart_puts("refusing to stop system task\n");
        return;
    }

    if (task_request_stop(id) < 0)
    {
        uart_puts("failed to stop task\n");
        return;
    }

    uart_puts("stop requested for task ");
    uart_put_uint((unsigned int)id);
    uart_puts("\n");
}

/*
 * Parse and execute 'stop <id|name>' from UART shell input.
 */
void shell_cmd_stop_arg(const char *arg)
{
    int id;

    if (shell_resolve_task_arg(arg, &id) < 0)
    {
        uart_puts("task not found\n");
        return;
    }

    shell_cmd_stop_id(id);
}

/*
 * Parse and execute 'log <id|name>' from UART shell input.
 */
static void shell_cmd_log_arg(const char *arg)
{
    int id;

    if (shell_resolve_task_arg(arg, &id) < 0)
    {
        uart_puts("task not found\n");
        return;
    }

    log_dump_task_id(id);
}

/*
 * Parse and execute 'log clear <id|name>' from UART shell input.
 */
static void shell_cmd_log_clear_arg(const char *arg)
{
    int id;

    if (shell_resolve_task_arg(arg, &id) < 0)
    {
        uart_puts("task not found\n");
        return;
    }

    log_clear_task_id(id);
    uart_puts("log cleared\n");
}

/*
 * Dump all trace events to UART.
 *
 * Consumes the trace buffer and prints each event in order.
 */
void shell_cmd_trace_dump(void)
{
    trace_event_t ev;

    while (trace_pop(&ev) == 0)
    {
        uart_puts("[t=");
        uart_put_uint((unsigned int)ev.tick);
        uart_puts("] ");

        switch (ev.type)
        {
        case TRACE_CTX_SWITCH:
            uart_puts("switch ");
            uart_put_uint((unsigned int)ev.from_task);
            uart_puts(" -> ");
            uart_put_uint((unsigned int)ev.to_task);
            break;
        case TRACE_TASK_SLEEP:
            uart_puts("sleep ");
            uart_put_uint((unsigned int)ev.from_task);
            uart_puts(" ticks=");
            uart_put_uint((unsigned int)ev.arg);
            break;
        case TRACE_TASK_WAKE:
            uart_puts("wake ");
            uart_put_uint((unsigned int)ev.from_task);
            break;
        case TRACE_TASK_STOP:
            uart_puts("stop ");
            uart_put_uint((unsigned int)ev.from_task);
            uart_puts(" -> ");
            uart_put_uint((unsigned int)ev.to_task);
            break;
        case TRACE_TASK_EXIT:
            uart_puts("exit ");
            uart_put_uint((unsigned int)ev.from_task);
            break;
        default:
            uart_puts("unknown");
            break;
        }

        uart_puts("\n");
    }
}

/*
 * Clear the trace buffer.
 */
static void shell_cmd_trace_clear(void)
{
    trace_clear();
    uart_puts("trace cleared\n");
}

/*
 * Execute one shell command line.
 */
static void shell_execute_command(char *cmd)
{
    if (str_equals(cmd, "help"))
    {
        shell_cmd_help();
    }
    else if (str_equals(cmd, "ps"))
    {
        shell_cmd_ps();
    }
    else if (str_equals(cmd, "heap dump"))
    {
        heap_dump();
    }
    else if (str_equals(cmd, "heap stats"))
    {
        heap_stats();
    }
    else if (str_equals(cmd, "startable"))
    {
        shell_cmd_startable();
    }
    else if (str_starts_with(cmd, "start "))
    {
        shell_cmd_start_arg(cmd + 6);
    }
    else if (str_starts_with(cmd, "stop "))
    {
        shell_cmd_stop_arg(cmd + 5);
    }
    else if (str_starts_with(cmd, "log clear "))
    {
        shell_cmd_log_clear_arg(cmd + 10);
    }
    else if (str_starts_with(cmd, "log "))
    {
        shell_cmd_log_arg(cmd + 4);
    }
    else if (str_equals(cmd, "trace dump"))
    {
        shell_cmd_trace_dump();
    }
    else if (str_equals(cmd, "trace clear"))
    {
        shell_cmd_trace_clear();
    }
    else
    {
        uart_puts("unknown command: ");
        uart_puts(cmd);
        uart_puts("\n");
    }
}

/*
 * Simple UART shell task.
 *
 * Reads characters from the UART software RX buffer, builds a line,
 * and executes it when ENTER is pressed.
 */
void shell_task(void)
{
    char buffer[SHELL_BUFFER_SIZE];
    int len = 0;

    uart_puts("> ");

    while (1)
    {
        char c;

        uart_read_char_blocking(&c);

        if (c == '\r' || c == '\n')
        {
            uart_puts("\n");
            buffer[len] = '\0';

            if (len > 0)
            {
                shell_execute_command(buffer);
            }

            len = 0;
            uart_puts("> ");
        }
        else if (c == '\b' || c == 127)
        {
            if (len > 0)
            {
                len--;
                uart_puts("\b \b");
            }
        }
        else
        {
            if (len < (SHELL_BUFFER_SIZE - 1))
            {
                buffer[len++] = c;
                uart_putc(c);
            }
        }
    }
}
