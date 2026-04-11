#ifndef GPIO_H
#define GPIO_H

#include <stdint.h>

#define GPIO_MAX_PIN 53

typedef enum {
    GPIO_FUNC_INPUT = 0,
    GPIO_FUNC_OUTPUT = 1,
    GPIO_FUNC_ALT5 = 2
} gpio_function_t;

typedef enum {
    GPIO_PULL_NONE = 0,
    GPIO_PULL_UP = 1,
    GPIO_PULL_DOWN = 2
} gpio_pull_t;

void gpio_set_function(uint32_t pin, gpio_function_t func);
void gpio_set_pull(uint32_t pin, gpio_pull_t pull);
void gpio_use_as_alt5(uint32_t pin);

/* Optional convenience helper */
void gpio_use_as_input(uint32_t pin);

/* GPIO edge/event support */
void gpio_enable_rising_edge(uint32_t pin);
void gpio_enable_falling_edge(uint32_t pin);
void gpio_disable_rising_edge(uint32_t pin);
void gpio_disable_falling_edge(uint32_t pin);

int gpio_event_detected(uint32_t pin);
void gpio_clear_event(uint32_t pin);

#endif