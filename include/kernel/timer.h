#ifndef KERNEL_TIMER_H
#define KERNEL_TIMER_H

#include <stdint.h>

void timer_init(uint32_t tick_hz);
uint64_t timer_get_ticks(void);
uint32_t timer_get_tick_hz(void);
uint64_t timer_ticks_to_seconds(uint64_t ticks);
void timer_handle_tick(void);

#endif