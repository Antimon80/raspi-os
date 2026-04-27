#ifndef SENSEHAT_LPS25H_H
#define SENSEHAT_LPS25H_H

#include <stdint.h>

int lps25h_init(void);
int lps25h_read_pressure_raw(int32_t *pressure_raw);
int lps25h_read_pressure_centi_hpa(int32_t *pressure_centi_hpa, int32_t *pressure_raw);

#endif