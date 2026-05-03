#ifndef SENSEHAT_HTS221_H
#define SENSEHAT_HTS221_H

#include <stdint.h>

int hts221_init(void);
int hts221_read_humidity_raw(int16_t *humidity_raw);
int hts221_read_humidity_centi_percent(int32_t *humidity_centi_percent, int16_t *humidity_raw);
int hts221_read_temperature_raw(int16_t *temperature_raw);
int hts221_read_temperature_centi_c(int32_t *temperature_centi_c, int16_t *temperature_raw);

#endif