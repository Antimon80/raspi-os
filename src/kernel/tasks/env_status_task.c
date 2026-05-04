#include "kernel/tasks/env_status_task.h"
#include "kernel/tasks/env_task.h"
#include "kernel/tasks/led_task.h"
#include "kernel/sched/scheduler.h"
#include "kernel/timer.h"
#include "kernel/io/console.h"
#include "sensehat/led_matrix.h"

#define ENV_STATUS_REFRESH_TICKS 50

/*
 * Temperature thresholds.
 *
 * The Sense HAT sits directly on the Raspberry Pi, so this does not represent
 * normal room temperature. The range is intentionally shifted upwards.
 */
#define TEMP_GREEN_LIMIT_CENTI_C 3500
#define TEMP_YELLOW_LIMIT_CENTI_C 4000
#define TEMP_ORANGE_LIMIT_CENTI_C 4500

/*
 * Humidity thresholds.
 */
#define HUM_DRY_LIMIT_CENTI_PERCENT 3000
#define HUM_LOW_LIMIT_CENTI_PERCENT 4500
#define HUM_NORMAL_LIMIT_CENTI_PERCENT 6000

static int env_status_task_id = -1;

/*
 * Register the environment LED status task ID.
 */
void env_status_register_task_id(int id)
{
    env_status_task_id = id;
}

/*
 * Return the registered environment LED status task ID, or -1 if none exists.
 */
int env_status_get_task_id(void)
{
    return env_status_task_id;
}

/*
 * Return the display color for the current board/Sense-HAT temperature.
 */
static led_matrix_color_t env_status_temperature_color(int32_t temperature_centi_c)
{
    if (temperature_centi_c < TEMP_GREEN_LIMIT_CENTI_C)
    {
        return (led_matrix_color_t){0, 180, 0};
    }

    if (temperature_centi_c < TEMP_YELLOW_LIMIT_CENTI_C)
    {
        return (led_matrix_color_t){180, 180, 0};
    }

    if (temperature_centi_c < TEMP_ORANGE_LIMIT_CENTI_C)
    {
        return (led_matrix_color_t){220, 80, 0};
    }

    return (led_matrix_color_t){220, 0, 0};
}

/*
 * Return the display color for the current humidity.
 */
static led_matrix_color_t env_status_humidity_color(int32_t humidity_centi_percent)
{
    if (humidity_centi_percent < HUM_DRY_LIMIT_CENTI_PERCENT)
    {
        return (led_matrix_color_t){180, 0, 180};
    }

    if (humidity_centi_percent < HUM_LOW_LIMIT_CENTI_PERCENT)
    {
        return (led_matrix_color_t){60, 160, 255};
    }

    if (humidity_centi_percent < HUM_NORMAL_LIMIT_CENTI_PERCENT)
    {
        return (led_matrix_color_t){0, 80, 220};
    }

    return (led_matrix_color_t){0, 220, 220};
}

/*
 * Fill an inclusive vertical row range with a single color.
 */
static void env_status_fill_rows(led_frame_t *frame, int y_start, int y_end, led_matrix_color_t color)
{
    if (!frame)
    {
        return;
    }

    for (int y = y_start; y <= y_end; y++)
    {
        if (y < 0 || y >= MATRIX_HEIGHT)
        {
            continue;
        }

        for (int x = 0; x < MATRIX_WIDTH; x++)
        {
            frame->pixels[y][x] = color;
        }
    }
}

/*
 * Render one environment sample into an 8x8 LED frame.
 *
 * Layout:
 *
 * rows 0..3: temperature status color
 * row  4   : black separator
 * rows 5..7: humidity status color
 */
static void env_status_render_frame(led_frame_t *frame, const env_sample_t *sample)
{
    led_matrix_color_t temp_color;
    led_matrix_color_t humidity_color;

    if (!frame || !sample)
    {
        return;
    }

    led_frame_clear(frame);

    temp_color = env_status_temperature_color(sample->temperature_centi_c);
    humidity_color = env_status_humidity_color(sample->humidity_centi_percent);

    env_status_fill_rows(frame, 0, 3, temp_color);
    env_status_fill_rows(frame, 5, 7, humidity_color);
}

/*
 * LED matrix environment status task.
 *
 * This task does not read the sensors directly. It only consumes the latest
 * sample from env_store and displays a compact status visualization.
 */
void env_status_task(void)
{
    int task_id = scheduler_current_task_id();

    env_status_register_task_id(task_id);

    console_puts("envled: started\n");

    if (led_acquire(task_id) < 0)
    {
        console_puts("envled: LED matrix already in use\n");
        env_status_register_task_id(-1);
        return;
    }

    while (1)
    {
        env_sample_t sample;
        led_frame_t frame;

        if (env_get_latest(&sample) == 0)
        {
            env_status_render_frame(&frame, &sample);
            led_submit_frame(task_id, &frame);
        }

        task_sleep(ENV_STATUS_REFRESH_TICKS);
    }

    led_release(task_id);
    env_status_register_task_id(-1);
}