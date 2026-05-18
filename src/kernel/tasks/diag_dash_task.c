#include "kernel/tasks/diag_dash_task.h"
#include "kernel/debug/diag.h"
#include "kernel/io/console.h"
#include "kernel/io/hdmi_console.h"
#include "kernel/sched/scheduler.h"
#include "kernel/timer.h"
#include "rpi4/hdmi/hdmi.h"
#include "util/format.h"
#include "util/string.h"

#define DIAG_DASH_REFRESH_TICKS 50u

/* Dashboard color palette. */
#define DIAG_DASH_BG 0x00161F2Au
#define DIAG_DASH_TEXT 0x00F2F6F8u
#define DIAG_DASH_MUTED 0x0087A3B7u
#define DIAG_DASH_TITLE 0x0098F4FFu
#define DIAG_DASH_GOOD 0x0048E27Bu
#define DIAG_DASH_WARN 0x00FFCE54u
#define DIAG_DASH_BAD 0x00FF6B6Bu

#define DIAG_DASH_STACK_BAR_WIDTH 8u

#define DIAG_DASH_BUFFER_SIZE 96u

/* Currently running diagnostics dashboard task. */
static int diag_dashboard_task_id = -1;

/*
 * Register the diagnostics dashboard task ID.
 */
void diag_dash_set_task_id(int id)
{
    diag_dashboard_task_id = id;
}

/*
 * Return the registered diagnostics dashboard task ID, or -1 if inactive.
 */
int diag_dash_get_task_id(void)
{
    return diag_dashboard_task_id;
}

static void diag_dash_append_bar(format_buffer_t *fmt, unsigned int percent)
{
    unsigned int filled;

    if (!fmt)
    {
        return;
    }

    if (percent > 100u)
    {
        percent = 100u;
    }

    filled = (percent * DIAG_DASH_STACK_BAR_WIDTH + 99u) / 100u;

    format_append_char(fmt, '[');

    for (unsigned int i = 0u; i < DIAG_DASH_STACK_BAR_WIDTH; i++)
    {
        if (i < filled)
        {
            format_append_char(fmt, '#');
        }
        else
        {
            format_append_char(fmt, '-');
        }
    }

    format_append_char(fmt, ']');
}

/*
 * Write one full line to the HDMI main pane using the dashboard background.
 */
static void diag_dash_write_line(uint32_t row, uint32_t fg, const char *s)
{
    hdmi_write_line(HDMI_PANE_MAIN, row, fg, DIAG_DASH_BG, s);
}

/*
 * Clear all remaining dashboard rows from the given row downwards.
 */
static void diag_dash_clear_from(uint32_t row)
{
    hdmi_clear_lines_from(HDMI_PANE_MAIN, row, DIAG_DASH_TEXT, DIAG_DASH_BG);
}

/*
 * Build a line with an unsigned integer value and optional unit.
 *
 * Example:
 *   Heap free: 123456 bytes
 */
static void diag_dash_make_uint_line(char *out, unsigned int size, const char *label, uint64_t value, const char *unit)
{
    format_buffer_t fmt;

    format_buffer_init(&fmt, out, size);
    format_append_string(&fmt, label);
    format_append_string(&fmt, ": ");
    format_append_uint(&fmt, value);

    if (unit && *unit)
    {
        format_append_char(&fmt, ' ');
        format_append_string(&fmt, unit);
    }
}

/*
 * Build a line with a percentage value.
 *
 * Example:
 *   CPU busy: 42%
 */
static void diag_dash_make_percent_line(char *out, unsigned int size, const char *label, unsigned int percent)
{
    format_buffer_t fmt;

    format_buffer_init(&fmt, out, size);
    format_append_string(&fmt, label);
    format_append_string(&fmt, ": ");
    format_append_uint(&fmt, percent);
    format_append_char(&fmt, '%');
}

/*
 * Build a compact timestamp line from scheduler ticks.
 */
static void diag_dash_make_uptime_line(char *out, unsigned int size, uint64_t tick)
{
    format_buffer_t fmt;

    format_buffer_init(&fmt, out, size);
    format_append_string(&fmt, "uptime: ");
    format_append_timestamp(&fmt, tick);
}

/*
 * Return a status color for percentage based resource usage.
 */
static uint32_t diag_dash_percent_color(unsigned int percent)
{
    if (percent < 60u)
    {
        return DIAG_DASH_GOOD;
    }

    if (percent < 85u)
    {
        return DIAG_DASH_WARN;
    }

    return DIAG_DASH_BAD;
}

/*
 * Return heap usage in percent.
 */
static unsigned int diag_dash_heap_percent(const heap_stats_t *heap)
{
    if (!heap || heap->total_bytes == 0u)
    {
        return 0u;
    }

    return (unsigned int)((heap->used_bytes * 100u) / heap->total_bytes);
}

/*
 * Return stack high-water usage in percent.
 */
static unsigned int diag_dash_stack_percent(const diag_task_info_t *task)
{
    if (!task || TASK_STACK_SIZE == 0u)
    {
        return 0u;
    }

    return (unsigned int)((task->stack_used_bytes * 100u) / TASK_STACK_SIZE);
}

static void diag_dash_make_task_line(char *out, unsigned int size, const diag_task_info_t *task)
{
    format_buffer_t fmt;
    unsigned int stack_percent;

    if (!task)
    {
        if (out && size > 0u)
        {
            out[0] = 0;
        }
        return;
    }

    stack_percent = diag_dash_stack_percent(task);

    format_buffer_init(&fmt, out, size);

    if (task->id < 10)
    {
        format_append_char(&fmt, '0');
    }

    format_append_uint(&fmt, (uint64_t)task->id);
    format_append_char(&fmt, ' ');

    if (task->name)
    {
        format_append_string(&fmt, task->name);
    }
    else
    {
        format_append_string(&fmt, "?");
    }

    {
        unsigned int name_len = task->name ? (unsigned int)str_length(task->name) : 1u;

        while (name_len < 12u)
        {
            format_append_char(&fmt, ' ');
            name_len++;
        }
    }

    format_append_char(&fmt, ' ');
    diag_dash_append_bar(&fmt, stack_percent);
    format_append_char(&fmt, ' ');

    if (stack_percent < 10u)
    {
        format_append_char(&fmt, ' ');
    }

    format_append_uint(&fmt, (uint64_t)stack_percent);
    format_append_char(&fmt, '%');
    format_append_char(&fmt, ' ');

    format_append_uint(&fmt, (uint64_t)task->stack_used_bytes);
}

/*
 * Render one diagnostics snapshot to the HDMI main pane.
 */
static void diag_dash_render(const diag_snapshot_t *snap)
{
    char line[DIAG_DASH_BUFFER_SIZE];
    unsigned int heap_percent;
    uint32_t row;

    if (!snap)
    {
        return;
    }

    hdmi_clear_pane(HDMI_PANE_MAIN);

    diag_dash_write_line(0u, DIAG_DASH_TITLE, "SYSTEM DIAGNOSTICS");
    diag_dash_write_line(1u, DIAG_DASH_TITLE, "==================");

    diag_dash_make_uptime_line(line, sizeof(line), snap->tick);
    diag_dash_write_line(3u, DIAG_DASH_MUTED, line);

    diag_dash_make_percent_line(line, sizeof(line), "CPU busy", snap->cpu_busy_percent);
    diag_dash_write_line(5u, diag_dash_percent_color(snap->cpu_busy_percent), line);

    heap_percent = diag_dash_heap_percent(&snap->heap);
    diag_dash_make_percent_line(line, sizeof(line), "Heap used", heap_percent);
    diag_dash_write_line(6u, diag_dash_percent_color(heap_percent), line);

    diag_dash_make_uint_line(line, sizeof(line), "Heap used", (uint64_t)snap->heap.used_bytes, "bytes");
    diag_dash_write_line(7u, DIAG_DASH_MUTED, line);

    diag_dash_make_uint_line(line, sizeof(line), "Heap free", (uint64_t)snap->heap.free_bytes, "bytes");
    diag_dash_write_line(8u, DIAG_DASH_MUTED, line);

    diag_dash_make_uint_line(line, sizeof(line), "Heap total", (uint64_t)snap->heap.total_bytes, "bytes");
    diag_dash_write_line(9u, DIAG_DASH_MUTED, line);

    diag_dash_write_line(11u, DIAG_DASH_TEXT, "TASK STACKS");
    diag_dash_write_line(12u, DIAG_DASH_TEXT, "-----------");

    row = 14u;

    for (unsigned int i = 0u; i < snap->task_count && row < 29u; i++)
    {
        const diag_task_info_t *task = &snap->tasks[i];
        unsigned int stack_percent = diag_dash_stack_percent(task);

        diag_dash_make_task_line(line, sizeof(line), task);
        diag_dash_write_line(row, diag_dash_percent_color(stack_percent), line);

        row++;
    }

    diag_dash_clear_from(row);

    while (hdmi_present(32u))
    {
        scheduler_yield();
    }
}

/*
 * Release all resources owned by the diagnostics dashboard.
 *
 * Clears pending HDMI console output, clears the main pane, flushes the
 * rendered state, releases pane ownership and marks the task as inactive.
 */
void diag_dash_cleanup_resources(void)
{
    if (diag_dashboard_task_id >= 0)
    {
        hdmi_console_clear_queue();
        hdmi_clear_pane(HDMI_PANE_MAIN);

        while (hdmi_present(32u))
        {
            scheduler_yield();
        }

        hdmi_release_pane(HDMI_PANE_MAIN, diag_dashboard_task_id);
        hdmi_console_clear_queue();

        diag_dashboard_task_id = -1;
    }
}

/*
 * HDMI diagnostics dashboard task.
 *
 * The task owns HDMI_PANE_MAIN while running and periodically renders CPU,
 * heap and per-task stack diagnostics. Normal console mirroring is prevented
 * by pane ownership.
 */
void diag_dash_task(void)
{
    diag_snapshot_t snapshot;

    diag_dashboard_task_id = scheduler_current_task_id();

    if (hdmi_acquire_pane(HDMI_PANE_MAIN, diag_dashboard_task_id) < 0)
    {
        console_puts("diagdash: HDMI main pane already in use\n");
        diag_dashboard_task_id = -1;
        return;
    }

    hdmi_console_clear_queue();
    hdmi_clear_pane(HDMI_PANE_MAIN);

    console_puts("diagdash: started\n");

    while (1)
    {
        diag_get_snapshot(&snapshot);
        diag_dash_render(&snapshot);

        task_sleep(DIAG_DASH_REFRESH_TICKS);
    }
}