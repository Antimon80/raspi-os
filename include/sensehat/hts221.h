#ifndef SENSEHAT_HTS221_H
#define SENSEHAT_HTS221_H

#include <stdint.h>

int hts221_init(void);
int hts221_read_humidity_raw(int16_t *humidity_raw);
int hts221_read_humidity_centi_percent(int32_t *humidity_centi_percent, int16_t *humidity_raw);

#endif