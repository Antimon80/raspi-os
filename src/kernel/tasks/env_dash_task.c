#include "kernel/tasks/env_task.h"
#include "kernel/tasks/env_status_task.h"
#include "kernel/tasks/env_dash_task.h"
#include "kernel/io/console.h"
#include "kernel/io/hdmi_console.h"
#include "kernel/sched/scheduler.h"
#include "kernel/timer.h"
#include "rpi4/hdmi/hdmi.h"
#include "util/format.h"

#define ENV_DASH_REFRESH_TICKS 25

/* Dashboard color palette. */
#define ENV_DASH_BG 0x00161F2Au
#define ENV_DASH_TEXT 0x00F2F6F8u
#define ENV_DASH_MUTED 0x0087A3B7u
#define ENV_DASH_TEMP 0x00FFCE54u
#define ENV_DASH_HUMIDITY 0x0041E4FFu
#define ENV_DASH_WARN 0x00FF6B6Bu
#define ENV_DASH_GOOD 0x0048E27Bu

/* Fixed dashboard text area. */
#define ENV_DASH_BUFFER_SIZE 96u

/* Currently running envdash task. */
static int env_dash_task_id = -1;

/*
 * Register the env dashboard task ID.
 */
void env_dash_set_task_id(int id)
{
    env_dash_task_id = id;
}

/*
 * Return the registered env dashboard task ID, or -1 if inactive.
 */
int env_dash_get_task_id(void)
{
    return env_dash_task_id;
}

/*
 * Write one full dashboard line to the HDMI main pane.
 */
static void env_dash_write_line(uint32_t row, uint32_t fg, const char *s)
{
    hdmi_write_line(HDMI_PANE_MAIN, row, fg, ENV_DASH_BG, s);
}

/*
 * Clear all dashboard rows from the given row downwards.
 */
static void env_dash_clear_from(uint32_t row)
{
    hdmi_clear_lines_from(HDMI_PANE_MAIN, row, ENV_DASH_TEXT, ENV_DASH_BG);
}

/*
 * Build one measurement line using a centi-unit value.
 *
 * Example:
 *   temperature: 35.42 deg C
 */
static void env_dash_make_centi_line(char *out, unsigned int size, const char *label, int32_t value, const char *unit)
{
    if (!out || size == 0u)
    {
        return;
    }

    format_buffer_t fmt;

    format_buffer_init(&fmt, out, size);
    format_append_string(&fmt, label);
    format_append_string(&fmt, ": ");
    format_append_centi(&fmt, value);
    format_append_char(&fmt, ' ');
    format_append_string(&fmt, unit);
}

/*
 * Convert a scheduler tick value into a compact uptime string.
 *
 * The current timer configuration uses 100 ticks per second.
 */
static void env_dash_make_tick_line(char *out, unsigned int size, uint64_t tick)
{
    format_buffer_t fmt;
    uint64_t seconds = tick / 100u;
    uint64_t minutes = seconds / 60u;
    uint64_t hours = minutes / 60u;

    seconds %= 60u;
    minutes %= 60u;

    format_buffer_init(&fmt, out, size);

    format_append_string(&fmt, "uptime: ");

    if (hours < 10u)
    {
        format_append_char(&fmt, '0');
    }
    format_append_uint(&fmt, hours);
    format_append_char(&fmt, ':');

    if (minutes < 10u)
    {
        format_append_char(&fmt, '0');
    }
    format_append_uint(&fmt, minutes);
    format_append_char(&fmt, ':');

    if (seconds < 10u)
    {
        format_append_char(&fmt, '0');
    }
    format_append_uint(&fmt, seconds);
}

/*
 * Map the measured temperature to the same status colors used by the
 * environment LED status display.
 */
static uint32_t env_dash_temperature_color(int32_t temperature_centi_c)
{
    if (temperature_centi_c < TEMP_GREEN_LIMIT_CENTI_C)
    {
        return ENV_DASH_GOOD;
    }

    if (temperature_centi_c < TEMP_YELLOW_LIMIT_CENTI_C)
    {
        return ENV_DASH_TEMP;
    }

    if (temperature_centi_c < TEMP_ORANGE_LIMIT_CENTI_C)
    {
        return 0x00DC5000u;
    }

    return ENV_DASH_WARN;
}

/*
 * Map the measured humidity to the same status colors used by the
 * environment LED status display.
 */
static uint32_t env_dash_humidity_color(int32_t humidity_centi_percent)
{
    if (humidity_centi_percent < HUM_DRY_LIMIT_CENTI_PERCENT)
    {
        return 0x0033FFFFu;
    }

    if (humidity_centi_percent < HUM_LOW_LIMIT_CENTI_PERCENT)
    {
        return 0x003399FFu;
    }

    if (humidity_centi_percent < HUM_NORMAL_LIMIT_CENTI_PERCENT)
    {
        return 0x000000FFu;
    }

    return 0x006600CCu;
}

/*
 * Render the initial dashboard state while no environment sample is available.
 */
static void env_dash_render_waiting(void)
{
    hdmi_clear_pane(HDMI_PANE_MAIN);

    env_dash_write_line(0u, ENV_DASH_TEXT, "ENVIRONMENT DASHBOARD");
    env_dash_write_line(1u, ENV_DASH_TEXT, "=====================");
    env_dash_write_line(3u, ENV_DASH_MUTED, "waiting for env sample ...");
    env_dash_write_line(5u, ENV_DASH_MUTED, "start env first if needed");
    env_dash_clear_from(7u);

    while (hdmi_present(32u))
    {
        scheduler_yield();
    }
}

/*
 * Render the latest environment sample and the LED matrix color legend.
 *
 * The dashboard owns the HDMI main pane while this task is running.
 */
static void env_dash_render_sample(const env_sample_t *sample)
{
    char line[ENV_DASH_BUFFER_SIZE];

    if (!sample)
    {
        return;
    }

    hdmi_clear_pane(HDMI_PANE_MAIN);

    env_dash_write_line(0u, ENV_DASH_TEXT, "ENVIRONMENT DASHBOARD");
    env_dash_write_line(1u, ENV_DASH_TEXT, "=====================");

    env_dash_make_centi_line(line, sizeof(line), "temperature", sample->temperature_centi_c, "deg C");
    env_dash_write_line(3u, env_dash_temperature_color(sample->temperature_centi_c), line);

    env_dash_make_centi_line(line, sizeof(line), "humidity", sample->humidity_centi_percent, "%");
    env_dash_write_line(4u, env_dash_humidity_color(sample->humidity_centi_percent), line);

    env_dash_make_centi_line(line, sizeof(line), "pressure", sample->pressure_centi_hpa, "hPa");
    env_dash_write_line(5u, ENV_DASH_MUTED, line);

    env_dash_make_tick_line(line, sizeof(line), sample->tick);
    env_dash_write_line(6u, ENV_DASH_MUTED, line);

    env_dash_write_line(8u, ENV_DASH_TEXT, "LED MATRIX LEGEND");
    env_dash_write_line(9u, ENV_DASH_TEXT, "-----------------");

    env_dash_write_line(11u, ENV_DASH_TEXT, "rows 0..3: temperature");
    env_dash_write_line(12u, ENV_DASH_GOOD, "  green  < 35.00 deg C");
    env_dash_write_line(13u, ENV_DASH_TEMP, "  yellow < 40.00 deg C");
    env_dash_write_line(14u, 0x00DC5000u, "  orange < 45.00 deg C");
    env_dash_write_line(15u, ENV_DASH_WARN, "  red    >= 45.00 deg C");

    env_dash_write_line(18u, ENV_DASH_TEXT, "rows 5..7: humidity");
    env_dash_write_line(19u, 0x0033FFFFu, "  cyan       < 30.00 %");
    env_dash_write_line(20u, 0x003399FFu, "  light blue < 45.00 %");
    env_dash_write_line(21u, 0x000000FFu, "  blue       < 60.00 %");
    env_dash_write_line(22u, 0x006600CCu, "  purple     >= 60.00 %");

    env_dash_clear_from(23u);

    while (hdmi_present(32u))
    {
        scheduler_yield();
    }
}

/*
 * Release all resources owned by the environment dashboard.
 *
 * Clears pending HDMI console output, clears the main pane, flushes the
 * rendered state, releases pane ownership and marks the task as inactive.
 */
void env_dash_cleanup_resources(void)
{
    if (env_dash_task_id >= 0)
    {
        hdmi_console_clear_queue();
        hdmi_clear_pane(HDMI_PANE_MAIN);

        while (hdmi_present(32u))
        {
            scheduler_yield();
        }

        hdmi_release_pane(HDMI_PANE_MAIN, env_dash_task_id);
        hdmi_console_clear_queue();
        env_dash_task_id = -1;
    }
}

/*
 * HDMI environment dashboard task.
 *
 * The task requires env_task to be running, then acquires the HDMI main pane
 * and periodically renders the latest environment sample. If env_task stops,
 * the dashboard cleans up its pane ownership and exits.
 */
void env_dash_task(void)
{
    env_sample_t sample;
    int had_sample = 0;

    env_dash_task_id = scheduler_current_task_id();

    if (!env_is_running())
    {
        console_puts("envdash: env_task is not running\n");
        env_dash_task_id = -1;
        return;
    }

    if (hdmi_acquire_pane(HDMI_PANE_MAIN, env_dash_task_id) < 0)
    {
        console_puts("envdash: HDMI main pane already in use\n");
        env_dash_task_id = -1;
        return;
    }

    hdmi_console_clear_queue();
    hdmi_clear_pane(HDMI_PANE_MAIN);

    console_puts("envdash: started\n");

    while (1)
    {
        if (!env_is_running())
        {
            console_puts("envdash: stopping because env_task is not running\n");
            env_dash_cleanup_resources();
            return;
        }

        if (env_get_latest(&sample) == 0)
        {
            had_sample = 1;
            env_dash_render_sample(&sample);
        }
        else if (!had_sample)
        {
            env_dash_render_waiting();
        }

        task_sleep(ENV_DASH_REFRESH_TICKS);
    }
}