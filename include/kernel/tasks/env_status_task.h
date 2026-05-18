#ifndef KERNEL_TASKS_ENV_STATUS_TASK_H
#define KERNEL_TASKS_ENV_STATUS_TASK_H

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

void env_status_set_task_id(int id);
int env_status_get_task_id(void);
void env_status_task(void);

#endif