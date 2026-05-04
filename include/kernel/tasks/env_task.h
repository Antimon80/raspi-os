#ifndef KERNEL_TASKS_ENV_TASK_H
#define KERNEL_TASKS_ENV_TASK_H

#include <stdint.h>

#define ENV_SAMPLE_INTERVAL_TICKS 100
#define ENV_HISTORY_INTERVAL_TICKS 3000

#define ENV_HISTORY_SIZE 64

typedef struct{
    int32_t pressure_raw;
    int16_t humidity_raw;
    int16_t temperature_raw;

    int32_t pressure_centi_hpa;
    int32_t humidity_centi_percent;
    int32_t temperature_centi_c;

    uint64_t tick;
    int valid;
}env_sample_t;

void env_init(void);
void env_push(const env_sample_t *sample);
int env_get_latest(env_sample_t *out);
unsigned int env_get_history(env_sample_t *out, unsigned int max);
void env_task(void);

#endif