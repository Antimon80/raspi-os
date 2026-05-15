#include "kernel/io/shell.h"
#include "kernel/io/console.h"
#include "kernel/sched/task.h"
#include "kernel/sched/scheduler.h"
#include "kernel/memory/heap.h"
#include "kernel/memory/log.h"
#include "kernel/tasks/demo_task.h"
#include "kernel/tasks/tictactoe_task.h"
#include "kernel/tasks/gol_task.h"
#include "kernel/tasks/led_task.h"
#include "kernel/tasks/env_task.h"
#include "kernel/tasks/env_status_task.h"
#include "kernel/debug/trace.h"
#include "sensehat/led_matrix.h"
#include "rpi4/uart.h"
#include "util/string.h"
#include "util/convert.h"

#include <stdint.h>

#define SHELL_BUFFER_SIZE 64
#define ENV_HISTORY_DUMP_MAX 10

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
    {"burst", burst_task},
    {"tictactoe", tictactoe_task},
    {"gol", gol_task},
    {"env", env_task},
    {"envled", env_status_task}};

/*
 * Print a textual representation of a task state.
 */
static void shell_print_task_state(task_state_t state)
{
    switch (state)
    {
    case UNUSED:
        console_puts("UNUSED");
        break;
    case READY:
        console_puts("READY");
        break;
    case RUNNING:
        console_puts("RUNNING");
        break;
    case BLOCKED:
        console_puts("BLOCKED");
        break;
    case DYING:
        console_puts("DYING");
        break;
    case SLEEPING:
        console_puts("SLEEPING");
        break;
    default:
        console_puts("UNKNOWN");
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
    console_puts("Commands: \n");
    console_puts("  env\n");
    console_puts("  env history\n");
    console_puts("  help\n");
    console_puts("  heap dump\n");
    console_puts("  heap stats\n");
    console_puts("  log <id|name>\n");
    console_puts("  log clear <id|name>\n");
    console_puts("  ps\n");
    console_puts("  startable\n");
    console_puts("  start <name>\n");
    console_puts("  stop <id|name>\n");
    console_puts("  trace dump\n");
    console_puts("  trace clear\n");
}

/*
 * Print all non-unused tasks.
 */
void shell_cmd_ps(void)
{
    console_puts("ID  STATE     NAME\n");

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

        console_put_uint((unsigned int)task->id);
        console_puts("   ");
        shell_print_task_state(task->state);
        console_puts("   ");
        console_puts(task->name);
        console_puts("\n");
    }
}

/*
 * Print all startable tasks.
 */
static void shell_cmd_startable(void)
{
    int count = (int)(sizeof(startable_tasks) / sizeof(startable_tasks[0]));

    console_puts("Startable tasks:\n");

    for (int i = 0; i < count; i++)
    {
        console_puts("  ");
        console_puts(startable_tasks[i].name);
        console_puts("\n");
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
        console_puts("missing task name\n");
        return;
    }

    entry = shell_find_startable_task(name);

    if (!entry)
    {
        console_puts("unknown task name\n");
        return;
    }

    existing = shell_find_task_by_name(name);

    if (existing >= 0)
    {
        console_puts("task already exists with id ");
        console_put_uint((unsigned int)existing);
        console_puts("\n");
        return;
    }

    id = task_create(entry->entry, entry->name, 0);

    if (id < 0)
    {
        console_puts("failed to create task\n");
        return;
    }

    if (str_equals(name, "gol"))
    {
        gol_register_task_id(id);
    }

    if (str_equals(name, "envled"))
    {
        env_status_register_task_id(id);
    }

    console_puts("task started with id ");
    console_put_uint((unsigned int)id);
    console_puts("\n");
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
        console_puts("task not found\n");
        return;
    }

    if (task->flag & TASK_FLAG_SYSTEM)
    {
        console_puts("refusing to stop system task\n");
        return;
    }

    if (task_request_stop(id) < 0)
    {
        console_puts("failed to stop task\n");
        return;
    }

    if (id == gol_get_task_id())
    {
        led_release(id);
        gol_register_task_id(-1);
    }

    if (id == env_status_get_task_id())
    {
        led_release(id);
        env_status_register_task_id(-1);
    }

    console_puts("stop requested for task ");
    console_put_uint((unsigned int)id);
    console_puts("\n");
}

/*
 * Parse and execute 'stop <id|name>' from UART shell input.
 */
void shell_cmd_stop_arg(const char *arg)
{
    int id;

    if (shell_resolve_task_arg(arg, &id) < 0)
    {
        console_puts("task not found\n");
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
        console_puts("task not found\n");
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
        console_puts("task not found\n");
        return;
    }

    log_clear_task_id(id);
    console_puts("log cleared\n");
}

/*
 * Dump all trace events to UART.
 *
 * Consumes the trace buffer and prints each event in order.
 */
void shell_cmd_trace_dump(void)
{
    trace_event_t ev;
    int count = trace_count();

    for (int i = 0; i < count; i++)
    {
        if (trace_pop(&ev) < 0)
        {
            break;
        }

        console_puts("[t=");
        console_put_uint((unsigned int)ev.tick);
        console_puts("] ");

        switch (ev.type)
        {
        case TRACE_CTX_SWITCH:
            console_puts("switch ");
            console_put_uint((unsigned int)ev.from_task);
            console_puts(" -> ");
            console_put_uint((unsigned int)ev.to_task);
            break;
        case TRACE_TASK_SLEEP:
            console_puts("sleep ");
            console_put_uint((unsigned int)ev.from_task);
            console_puts(" ticks=");
            console_put_uint((unsigned int)ev.arg);
            break;
        case TRACE_TASK_WAKE:
            console_puts("wake ");
            console_put_uint((unsigned int)ev.from_task);
            break;
        case TRACE_TASK_STOP:
            console_puts("stop ");
            console_put_uint((unsigned int)ev.from_task);
            console_puts(" -> ");
            console_put_uint((unsigned int)ev.to_task);
            break;
        case TRACE_TASK_EXIT:
            console_puts("exit ");
            console_put_uint((unsigned int)ev.from_task);
            break;
        default:
            console_puts("unknown");
            break;
        }

        console_puts("\n");
    }
}

/*
 * Clear the trace buffer.
 */
static void shell_cmd_trace_clear(void)
{
    trace_clear();
    console_puts("trace cleared\n");
}

static void shell_print_centi(int32_t value)
{
    if (value < 0)
    {
        console_puts("-");
        value = -value;
    }

    console_put_uint((unsigned int)(value / 100));
    console_puts(".");

    if ((value % 100) < 10)
    {
        console_puts("0");
    }

    console_put_uint((unsigned int)(value % 100));
}

static void shell_cmd_env(void)
{
    env_sample_t sample;

    if (env_get_latest(&sample) < 0)
    {
        console_puts("no env sample available\n");
        return;
    }

    console_puts("Environment sample:\n");

    console_puts(" pressure: ");
    shell_print_centi(sample.pressure_centi_hpa);
    console_puts(" hPa\n");

    console_puts(" humidity: ");
    shell_print_centi(sample.humidity_centi_percent);
    console_puts(" %\n");

    console_puts(" temperature: ");
    shell_print_centi(sample.temperature_centi_c);
    console_puts(" C\n");

    console_puts(" tick: ");
    console_put_u64(sample.tick);
    console_puts("\n");
}

static void shell_cmd_env_history(void)
{
    env_sample_t samples[ENV_HISTORY_DUMP_MAX];
    unsigned int count;

    count = env_get_history(samples, ENV_HISTORY_DUMP_MAX);

    if (count == 0)
    {
        console_puts("no env history available\n");
        return;
    }

    console_puts("Environment history:\n");

    for (unsigned int i = 0; i < count; i++)
    {
        console_puts("[");
        console_put_uint(i);
        console_puts("] tick=");
        console_put_u64(samples[i].tick);

        console_puts(" pressure=");
        shell_print_centi(samples[i].pressure_centi_hpa);
        console_puts("hPa");

        console_puts(" humidity=");
        shell_print_centi(samples[i].humidity_centi_percent);
        console_puts("%");

        console_puts(" temperature=");
        shell_print_centi(samples[i].temperature_centi_c);
        console_puts("C\n");
    }
}

/*
 * Execute one shell command line.
 */
void shell_execute_command(const char *cmd)
{
    if (str_equals(cmd, "help"))
    {
        shell_cmd_help();
    }
    else if (str_equals(cmd, "env history"))
    {
        shell_cmd_env_history();
    }
    else if (str_equals(cmd, "env"))
    {
        shell_cmd_env();
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
        console_puts("unknown command: ");
        console_puts(cmd);
        console_puts("\n");
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

    console_puts("> ");

    while (1)
    {
        char c;

        uart_read_char(&c);

        if (c == '\r' || c == '\n')
        {
            console_puts("\n");
            buffer[len] = '\0';

            if (len > 0)
            {
                shell_execute_command(buffer);
            }

            len = 0;
            console_puts("> ");
        }
        else if (c == '\b' || c == 127)
        {
            if (len > 0)
            {
                len--;
                console_puts("\b \b");
            }
        }
        else if (c >= ' ' && c <= '~')
        {
            if (len < (SHELL_BUFFER_SIZE - 1))
            {
                buffer[len++] = c;
                console_putc(c);
            }
        }
    }
}
