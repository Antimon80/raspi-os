#include "kernel/tasks/led_task.h"
#include "kernel/sched/scheduler.h"
#include "kernel/sched/task.h"
#include "kernel/irq.h"
#include "sensehat/led_matrix.h"
#include "rpi4/uart.h"

static int led_task_id = -1;

/*
 * Single-slot frame buffer used for producer → consumer handoff.
 *
 * Only one frame can be pending at a time. A newly submitted frame
 * overwrites the previous one if it has not yet been consumed.
 */
static led_frame_t pending_frame;
static int frame_pending = 0;

/*
 * Register the LED task ID.
 *
 * Called once after task creation so producers can wake the renderer.
 */
void led_register_task_id(int id)
{
    led_task_id = id;
}

/*
 * Return the registered LED task ID, or -1 if none exists.
 */
int led_get_task_id(void)
{
    return led_task_id;
}

/*
 * Submit a new frame to the LED task.
 *
 * This function is called by producer tasks (e.g. Game of Life).
 * The frame is copied into a single-slot buffer and replaces any
 * previously pending frame.
 *
 * The LED task is then woken so it can render the new frame.
 *
 * Returns 0 on success and -1 on invalid input or if the LED task
 * is not yet registered.
 */
int led_submit_frame(const led_frame_t *frame)
{
    if (!frame || led_task_id < 0)
    {
        return -1;
    }

    irq_disable();

    pending_frame = *frame;
    frame_pending = 1;

    irq_enable();

    task_wakeup(led_task_id);

    return 0;
}

/*
 * Submit a frame that clears the display.
 *
 * This builds a black frame and forwards it to led_submit_frame().
 * Used for example when stopping a demo task.
 *
 * Returns 0 on success and -1 on failure.
 */
int led_submit_clear_frame(void)
{
    led_frame_t frame;
    led_matrix_color_t black = {0, 0, 0};

    for (int y = 0; y < MATRIX_HEIGHT; y++)
    {
        for (int x = 0; x < MATRIX_WIDTH; x++)
        {
            frame.pixels[y][x] = black;
        }
    }

    return led_submit_frame(&frame);
}

/*
 * LED render task.
 *
 * This task owns the LED matrix hardware. It initializes the device
 * once at startup and then waits for incoming frames from producer
 * tasks.
 *
 * The task blocks when no frame is pending and is woken via
 * task_wakeup() when a new frame is submitted.
 *
 * Rendering is performed via led_matrix_render_frame(), which updates
 * the hardware in a single operation.
 */
void led_task(void)
{
    led_frame_t frame;

    if (led_task_id < 0)
    {
        led_task_id = scheduler_current_task_id();
    }

    if (led_matrix_init() < 0)
    {
        uart_puts("led task: matrix init failed\n");

        while (1)
        {
            task_block_current();
        }
    }

    uart_puts("led task: matrix init OK\n");

    while (1)
    {
        irq_disable();

        if (!frame_pending)
        {
            task_t *current = task_get(scheduler_current_task_id());

            if (!current)
            {
                irq_enable();
                continue;
            }

            current->state = BLOCKED;
            irq_enable();

            scheduler_yield();
            continue;
        }

        frame = pending_frame;
        frame_pending = 0;

        irq_enable();

        led_matrix_render_frame(&frame);
    }
}