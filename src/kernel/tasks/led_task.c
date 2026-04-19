#include "kernel/tasks/led_task.h"
#include "kernel/irq.h"
#include "kernel/sched/scheduler.h"
#include "kernel/sched/task.h"
#include "sensehat/led_matrix.h"
#include "rpi4/uart.h"

static int led_task_id = -1;

/*
 * Register the task ID of the LED render task.
 */
void led_register_task_id(int id)
{
    led_task_id = id;
}

/*
 * Return the registered LED render task ID, or -1 if none exists.
 */
int led_get_task_id(void)
{
    return led_task_id;
}

/*
 * Minimal LED test task.
 *
 * Initializes the LED matrix, clears it and fills it with a solid color.
 */
void led_task(void)
{
    if (led_matrix_init() < 0)
    {
        uart_puts("led task: matrix init failed\n");
        while (1)
        {
            task_block_current();
        }
    }

    uart_puts("led task: matrix init OK\n");

    // clear display
    led_matrix_clear();
    led_matrix_present();

    // fill with alternating r - g - b
    while (1)
    {
        led_matrix_color_t red = {255, 0, 0};
        led_matrix_color_t green = {0, 255, 0};
        led_matrix_color_t blue = {0, 0, 255};

        led_matrix_fill(red);
        led_matrix_present();
        task_sleep(50);

        led_matrix_fill(green);
        led_matrix_present();
        task_sleep(50);

        led_matrix_fill(blue);
        led_matrix_present();
        task_sleep(50);
    }
}