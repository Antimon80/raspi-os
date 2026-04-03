#include "kernel/shell.h"
#include "kernel/task.h"
#include "kernel/scheduler.h"
#include "rpi4/uart.h"

#define SHELL_BUFFER_SIZE 64

/*
 * Simple demo task.
 * Periodically prints a message so that task switching becomes visible
 * on the UART output.
 */
static void demo_task(void)
{
    while (1)
    {
        uart_puts("[demo]\n");

        for (volatile unsigned long i = 0; i < 1000000UL; i++)
        {
        }

        scheduler_yield();
    }
}

/*
 * Compare two zero-terminated strings.
 * Returns 2 if equal, 0 otherwise.
 */
static int str_equals(const char *a, const char *b)
{
    while (*a && *b)
    {
        if (*a != *b)
        {
            return 0;
        }

        a++;
        b++;
    }

    return (*a == '\0' && *b == '\0');
}

/*
 * Check whether 'str' starts with 'prefix'.
 *
 * Returns 1 if true, 0 otherwise.
 */
static int str_starts_with(const char *str, const char *prefix)
{
    while (*prefix)
    {
        if (*str != *prefix)
        {
            return 0;
        }

        str++;
        prefix++;
    }

    return 1;
}

/*
 * Parse an unsigned decimal integer.
 *
 * Returns 0 on success, -1 on failure.
 */
static int parse_uint(const char *s, int *value)
{
    int result = 0;

    if (!s || !*s || !value)
    {
        return -1;
    }

    while (*s)
    {
        if (*s < '0' || *s > '9')
        {
            return -1;
        }

        result = result * 10 + (*s - '0');
        s++;
    }

    *value = result;
    return 0;
}

/*
 * Print an unsigned integer to UART.
 */
static void uart_put_uint(unsigned int value)
{
    char buffer[16];
    int i = 0;

    if (value == 0)
    {
        uart_putc('0');
        return;
    }

    while (value > 0)
    {
        buffer[i++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (i > 0)
    {
        uart_putc(buffer[--i]);
    }
}

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
 * Find a non-unused task by name.
 *
 * Returns the task ID on success, -1 if not found.
 */
static int shell_find_task_by_name(const char *name)
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
static void shell_cmd_help(void)
{
    uart_puts("Commands: \n");
    uart_puts("  help\n");
    uart_puts("  ps\n");
    uart_puts("  start demo\n");
    uart_puts("  stop <id>\n");
}

/*
 * Print all non-unused tasks.
 */
static void shell_cmd_ps(void)
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
 * Start the demo task if it does not already exist.
 */
static void shell_cmd_start_demo(void)
{
    int existing = shell_find_task_by_name("demo");

    if (existing >= 0)
    {
        uart_puts("demo task already exists with id ");
        uart_put_uint((unsigned int)existing);
        uart_puts("\n");
        return;
    }

    int id = task_create(demo_task, "demo");

    if (id < 0)
    {
        uart_puts("failed to create demo task\n");
        return;
    }

    uart_puts("demo task started with id ");
    uart_put_uint((unsigned int)id);
    uart_puts("\n");
}

/*
 * Request termination of a task.
 */
static void shell_cmd_stop(const char *arg)
{
    int id;
    task_t *task;

    if (parse_uint(arg, &id) < 0)
    {
        uart_puts("invalid task id\n");
        return;
    }

    task = task_get(id);
    if (!task || task->state == UNUSED)
    {
        uart_puts("task not found\n");
        return;
    }

    if (str_equals(task->name, "shell"))
    {
        uart_puts("refusing to stop shell task\n");
        return;
    }

    if (str_equals(task->name, "idle"))
    {
        uart_puts("refusing to stop idle task\n");
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
    else if (str_equals(cmd, "start demo"))
    {
        shell_cmd_start_demo();
    }
    else if (str_starts_with(cmd, "stop"))
    {
        shell_cmd_stop(cmd + 5);
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

        if (uart_read_char(&c))
        {
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

        scheduler_yield();
    }
}
