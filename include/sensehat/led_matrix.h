#ifndef SENSEHAT_LED_MATRIX_H
#define SENSEHAT_LED_MATRIX_H

#include <stdint.h>

#define MATRIX_WIDTH 8
#define MATRIX_HEIGHT 8

typedef struct
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
} led_matrix_color_t;

int led_matrix_init(void);

void led_matrix_clear(void);
void led_matrix_fill(led_matrix_color_t color);
int led_matrix_set_pixel(int x, int y, led_matrix_color_t color);
int led_matrix_get_pixel(int x, int y, led_matrix_color_t *color);
int led_matrix_present(void);

int led_matrix_acquire(void);
void led_matrix_release(int task_id);
int led_matrix_is_owned(void);
int led_matrix_get_owner(void);

#endif