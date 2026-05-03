#include "kernel/tasks/env_task.h"
#include "kernel/sched/scheduler.h"
#include "kernel/memory/log.h"
#include "kernel/io/console.h"
#include "kernel/timer.h"
#include "kernel/irq.h"
#include "sensehat/lps25h.h"
#include "sensehat/hts221.h"
#include "rpi4/i2c_bus.h"

/*
 * Shared environment sample store.
 *
 * The environment task is the single writer. Shell, HDMI and later LED/status
 * code may read the latest sample or a copy of the history.
 */
typedef struct
{
    env_sample_t latest;
    env_sample_t history[ENV_HISTORY_SIZE];
    unsigned int write_index;
    unsigned int count;
} env_store_t;

static env_store_t env_store;

/*
 * Initialize the environment sample store.
 *
 * This clears the latest-sample marker and resets the history ring buffer.
 */
void env_init(void)
{
    irq_disable();

    env_store.write_index = 0;
    env_store.count = 0;
    env_store.latest.valid = 0;

    irq_enable();
}

/*
 * Store a new environment sample.
 *
 * The sample becomes the latest value and is appended to the fixed-size
 * history ring buffer. When the buffer is full, the oldest sample is
 * overwritten.
 */
void env_push(const env_sample_t *sample)
{
    if (!sample)
    {
        return;
    }

    irq_disable();

    env_store.latest = *sample;
    env_store.latest.valid = 1;

    env_store.history[env_store.write_index] = env_store.latest;
    env_store.write_index = (env_store.write_index + 1) % ENV_HISTORY_SIZE;

    if (env_store.count < ENV_HISTORY_SIZE)
    {
        env_store.count++;
    }

    irq_enable();
}

/*
 * Copy the latest environment sample into out.
 *
 * Returns 0 on success and -1 if no valid sample is available yet or if out
 * is NULL.
 */
int env_get_latest(env_sample_t *out)
{
    if (!out)
    {
        return -1;
    }

    irq_disable();

    if (!env_store.latest.valid)
    {
        irq_enable();
        return -1;
    }

    *out = env_store.latest;

    irq_enable();
    return 0;
}

/*
 * Copy environment history samples into out.
 *
 * Samples are copied in chronological order, starting with the oldest
 * buffered sample. At most max entries are copied.
 *
 * Returns the number of copied samples.
 */
unsigned int env_get_history(env_sample_t *out, unsigned int max)
{
    unsigned int copied = 0;
    unsigned int start;

    if (!out || max == 0)
    {
        return 0;
    }

    irq_disable();

    if (env_store.count == 0)
    {
        irq_enable();
        return 0;
    }

    if (max > env_store.count)
    {
        max = env_store.count;
    }

    start = (env_store.write_index + ENV_HISTORY_SIZE - env_store.count) % ENV_HISTORY_SIZE;

    for (unsigned int i = 0; i < max; i++)
    {
        unsigned int index = (start + i) % ENV_HISTORY_SIZE;
        out[i] = env_store.history[index];
        copied++;
    }

    irq_enable();

    return copied;
}

/*
 * Periodic Sense HAT environment sensor task.
 *
 * Initializes the pressure and humidity sensors once, then samples pressure,
 * humidity and derived temperature once per second. All I2C accesses are
 * protected by the shared I2C bus lock because the LED matrix and joystick
 * may use the same bus concurrently.
 *
 * Successful samples are written to env_store. Diagnostic events are written
 * to the task log only on init/read failures, not for every sample.
 */
void env_task(void)
{
    env_sample_t sample;
    env_init();

    i2c_bus_lock();

    int lps_ok = lps25h_init();
    int hts_ok = 0;

    if (lps_ok == 0)
    {
        hts_ok = hts221_init();
    }

    i2c_bus_unlock();

    if (lps_ok < 0)
    {
        console_puts("env: LPS25H init failed\n");
        log_append_current_task("env: LPS25H init failed", 0);
        return;
    }

    if (hts_ok < 0)
    {
        console_puts("env: HTS221 init failed\n");
        log_append_current_task("env: HTS221 init failed", 0);
        return;
    }

    console_puts("env: sensors init OK\n");
    log_append_current_task("env: sensors init OK", 0);

    while (1)
    {
        sample.valid = 0;
        sample.tick = timer_get_ticks();

        i2c_bus_lock();

        int pressure_ok = lps25h_read_pressure_centi_hpa(&sample.pressure_centi_hpa, &sample.pressure_raw);
        int humidity_ok = hts221_read_humidity_centi_percent(&sample.humidity_centi_percent, &sample.humidity_raw);
        int temperature_ok = hts221_read_temperature_centi_c(&sample.temperature_centi_c, &sample.temperature_raw);

        i2c_bus_unlock();

        if (pressure_ok < 0)
        {
            log_append_current_task("env: pressure read failed", 0);
            task_sleep(100);
            continue;
        }

        if (humidity_ok < 0)
        {
            log_append_current_task("env: humidity read failed", 0);
            task_sleep(100);
            continue;
        }

        if (temperature_ok < 0)
        {
            log_append_current_task("env: temperature read failed", 0);
            task_sleep(100);
            continue;
        }

        sample.valid = 1;
        env_push(&sample);

        task_sleep(100);
    }
}