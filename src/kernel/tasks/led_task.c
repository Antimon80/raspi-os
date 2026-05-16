#include "kernel/tasks/led_task.h"
#include "kernel/sched/scheduler.h"
#include "kernel/sched/task.h"
#include "kernel/io/console.h"
#include "kernel/irq.h"
#include "sensehat/led_matrix.h"
#include "rpi4/drivers/uart.h"

static int led_task_id = -1;
static int led_owner_task_id = -1;

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
int led_submit_frame(int task_id, const led_frame_t *frame)
{
    if (!frame || led_task_id < 0)
    {
        return -1;
    }

    irq_disable();

    if (led_owner_task_id != task_id)
    {
        irq_enable();
        return -1;
    }

    pending_frame = *frame;
    frame_pending = 1;

    irq_enable();

    task_wakeup(led_task_id);

    return 0;
}

void led_frame_clear(led_frame_t *frame)
{
    led_matrix_color_t black = {0, 0, 0};

    if (!frame)
    {
        return;
    }

    for (int y = 0; y < MATRIX_HEIGHT; y++)
    {
        for (int x = 0; x < MATRIX_WIDTH; x++)
        {
            frame->pixels[y][x] = black;
        }
    }
}

int led_submit_clear_frame(int task_id)
{
    led_frame_t frame;

    led_frame_clear(&frame);

    return led_submit_frame(task_id, &frame);
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
        console_puts("led task: matrix init failed\n");

        while (1)
        {
            task_block_current();
        }
    }

    console_puts("led task: matrix init OK\n");

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

        if (led_matrix_render_frame(&frame) < 0)
        {
            task_sleep(2);
        }
    }
}

/*
 * Try to acquire exclusive ownership of the LED matrix for a producer task.
 *
 * Only the current owner is allowed to submit frames. If no owner is set,
 * the given task becomes the owner. Re-acquiring by the same task is allowed.
 *
 * Returns 0 on success and -1 if another task already owns the matrix.
 */
int led_acquire(int task_id)
{
    int ok = 0;

    irq_disable();

    if (led_owner_task_id < 0 || led_owner_task_id == task_id)
    {
        led_owner_task_id = task_id;
        ok = 1;
    }

    irq_enable();

    return ok ? 0 : -1;
}

/*
 * Release LED matrix ownership for a producer task.
 *
 * Before ownership is released, the display is cleared through the normal
 * frame submission path. If the given task is not the current owner, the
 * function leaves the owner unchanged.
 */
void led_release(int task_id)
{
    if (task_id < 0)
    {
        return;
    }

    led_submit_clear_frame(task_id);

    irq_disable();

    if (led_owner_task_id == task_id)
    {
        led_owner_task_id = -1;
    }

    irq_enable();
}