#include "kernel/io/hdmi_console.h"
#include "kernel/irq.h"
#include "kernel/sched/scheduler.h"
#include "kernel/sched/task.h"
#include "rpi4/hdmi/hdmi.h"

#define HDMI_CONSOLE_BUFFER_SIZE 4096

#define HDMI_INPUT_CHARS_PER_SLICE 128
#define HDMI_DIRTY_CELLS_PER_SLICE 16

/*
 * Software queue for asynchronous HDMI console mirroring.
 *
 * UART output is written immediately by console.c. HDMI output is queued here
 * and consumed by hdmi_console_task(), so large shell outputs cannot directly
 * block the calling task in the slow framebuffer renderer.
 */
static char hdmi_buffer[HDMI_CONSOLE_BUFFER_SIZE];

/*
 * Ring-buffer indices and current fill level.
 *
 * Access is protected by disabling IRQs because producers may enqueue from
 * normal task context while the HDMI console task concurrently dequeues.
 */
static unsigned int hdmi_head = 0;
static unsigned int hdmi_tail = 0;
static unsigned int hdmi_count = 0;

/*
 * Task ID of the dedicated HDMI console renderer.
 *
 * The task is woken whenever new characters are queued or mirroring is
 * enabled. A value below zero means the renderer task has not been registered.
 */
static int hdmi_console_task_id = -1;

/*
 * Global HDMI console mirror switch.
 *
 * When disabled, console output still goes to UART, but characters are not
 * queued for HDMI. This is used when an application owns HDMI directly, for
 * example Tic-Tac-Toe.
 */
static int hdmi_console_enabled = 0;

/*
 * Remove one character from the HDMI console queue.
 *
 * IRQs must already be disabled by the caller. This function intentionally
 * does not perform locking itself so the renderer can dequeue multiple
 * characters in one protected section.
 */
static int hdmi_console_dequeue_irq_disabled(char *out)
{
    if (!out || hdmi_count == 0)
    {
        return -1;
    }

    *out = hdmi_buffer[hdmi_tail];
    hdmi_tail = (hdmi_tail + 1u) % HDMI_CONSOLE_BUFFER_SIZE;
    hdmi_count--;

    return 0;
}

/*
 * Wake the HDMI console renderer if it is currently blocked.
 *
 * IRQs must already be disabled by the caller. This avoids using task_wakeup()
 * from an already IRQ-disabled section and prevents accidentally re-enabling
 * IRQs inside such a critical section.
 */
static void hdmi_console_wake_task_irq_disabled(void)
{
    task_t *task;

    if (hdmi_console_task_id < 0)
    {
        return;
    }

    task = task_get(hdmi_console_task_id);
    if (task && task->state == BLOCKED)
    {
        task->state = READY;
    }
}

/*
 * Initialize the HDMI console mirror state.
 *
 * Called during console initialization before the renderer task is expected
 * to run. The mirror starts disabled; enabling is done explicitly after HDMI
 * setup and task registration.
 */
void hdmi_console_init(void)
{
    irq_disable();

    hdmi_head = 0;
    hdmi_tail = 0;
    hdmi_count = 0;
    hdmi_console_task_id = -1;
    hdmi_console_enabled = 0;

    irq_enable();
}

/*
 * Register the task ID of the dedicated HDMI console renderer task.
 */
void hdmi_console_register_task_id(int id)
{
    irq_disable();
    hdmi_console_task_id = id;
    irq_enable();
}

/*
 * Enable or disable HDMI console mirroring.
 *
 * Disabling does not affect UART output and does not clear queued characters.
 * Enabling wakes the renderer so already queued output can be processed.
 */
void hdmi_console_enable(int enabled)
{
    irq_disable();
    hdmi_console_enabled = enabled ? 1 : 0;

    if (hdmi_console_enabled)
    {
        hdmi_console_wake_task_irq_disabled();
    }

    irq_enable();
}

/*
 * Return non-zero if HDMI console mirroring is currently enabled.
 */
int hdmi_console_is_enabled(void)
{
    int enabled;

    irq_disable();
    enabled = hdmi_console_enabled;
    irq_enable();

    return enabled;
}

/*
 * Queue one character for HDMI output.
 *
 * This function must never call hdmi_putc().
 * It only appends to the software buffer and wakes the HDMI renderer task.
 *
 * On overflow, the oldest HDMI-only character is dropped. UART output is
 * unaffected, so the serial console remains complete and responsive.
 */
int hdmi_console_enqueue(char c)
{
    if (!hdmi_console_enabled || hdmi_console_task_id < 0)
    {
        return -1;
    }

    irq_disable();

    if (!hdmi_console_enabled || hdmi_console_task_id < 0)
    {
        irq_enable();
        return -1;
    }

    if (hdmi_count == HDMI_CONSOLE_BUFFER_SIZE)
    {
        hdmi_tail = (hdmi_tail + 1u) % HDMI_CONSOLE_BUFFER_SIZE;
        hdmi_count--;
    }

    hdmi_buffer[hdmi_head] = c;
    hdmi_head = (hdmi_head + 1u) % HDMI_CONSOLE_BUFFER_SIZE;
    hdmi_count++;

    hdmi_console_wake_task_irq_disabled();

    irq_enable();

    return 0;
}

/*
 * Dedicated HDMI console renderer.
 *
 * This task is the only place where console mirroring calls hdmi_putc().
 * Rendering is intentionally sliced so large text dumps cannot monopolize
 * the CPU in the cooperative scheduler.
 */
void hdmi_console_task(void)
{
    char c;

    if (hdmi_console_task_id < 0)
    {
        hdmi_console_task_id = scheduler_current_task_id();
    }

    while (1)
    {
        int processed_chars = 0;
        int has_more_dirty = 0;

        irq_disable();

        if (!hdmi_console_enabled)
        {
            task_t *current = task_get(scheduler_current_task_id());

            if (!current)
            {
                irq_enable();
                scheduler_yield();
                continue;
            }

            current->state = BLOCKED;
            irq_enable();
            scheduler_yield();
            continue;
        }

        while (processed_chars < HDMI_INPUT_CHARS_PER_SLICE && hdmi_console_dequeue_irq_disabled(&c) == 0)
        {
            irq_enable();

            hdmi_putc(c);
            processed_chars++;

            irq_disable();
        }

        irq_enable();

        has_more_dirty = hdmi_present(HDMI_DIRTY_CELLS_PER_SLICE);

        irq_disable();

        if (hdmi_count == 0 && !has_more_dirty)
        {
            task_t *current = task_get(scheduler_current_task_id());

            if (current)
            {
                current->state = BLOCKED;
            }
        }

        irq_enable();

        scheduler_yield();
    }
}
