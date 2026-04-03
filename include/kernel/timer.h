#ifndef KERNEL_TIMER_H
#define KERNEL_TIMER_H

#include <stdint.h>

void timer_init(uint32_t tick_hz);
uint64_t timer_get_ticks(void);
void timer_handle_tick(void);

#endif