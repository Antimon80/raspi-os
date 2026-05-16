#include "kernel/timer.h"
#include "kernel/debug/trace.h"
#include "kernel/sched/task.h"
#include "kernel/io/console.h"

static volatile uint64_t system_ticks = 0;
static uint32_t timer_interval = 0;
static uint32_t timer_tick_hz = 0;

/*
 * Wake all sleeping task whose wakeup time has reached.
 */
static void timer_wake_sleeping_tasks(uint64_t now)
{
    for (int i = 0; i < MAX_TASKS; i++)
    {
        task_t *task = task_get(i);

        if (!task)
        {
            continue;
        }

        if (task->state == SLEEPING && task->wakeup_tick <= now)
        {
            task->state = READY;
            trace_record_irq_disabled(TRACE_TASK_WAKE, task->id, -1, 0);
        }
    }
}

static void timer_print_two_digits(unsigned int value)
{
    if (value < 10u)
    {
        console_puts("0");
    }

    console_put_uint(value);
}

/*
 * Return the current system tick counter.
 */
uint64_t timer_get_ticks(void)
{
    return system_ticks;
}

uint32_t timer_get_tick_hz(void)
{
    return timer_tick_hz;
}

uint64_t timer_ticks_to_seconds(uint64_t ticks)
{
    if (timer_tick_hz == 0)
    {
        return 0;
    }

    return ticks / timer_tick_hz;
}

void timer_print_timestamp(uint64_t tick)
{
    uint64_t seconds = timer_ticks_to_seconds(tick);
    unsigned int hours = (unsigned int)(seconds / 3600u);
    unsigned int minutes = (unsigned int)((seconds / 60u) % 60u);
    unsigned int secs = (unsigned int)(seconds % 60u);

    console_puts("[");
    timer_print_two_digits(hours);
    console_puts(":");
    timer_print_two_digits(minutes);
    console_puts(":");
    timer_print_two_digits(secs);
    console_puts("] ");
}

/*
 * Initialize the generic timer to generate periodic tick interrupts.
 */
void timer_init(uint32_t tick_hz)
{
    uint64_t cntfrq;

    if (tick_hz == 0)
    {
        return;
    }

    timer_tick_hz = tick_hz;

    asm volatile("mrs %0, cntfrq_el0" : "=r"(cntfrq));

    timer_interval = (uint32_t)(cntfrq / tick_hz);

    if (timer_interval == 0)
    {
        timer_interval = 1;
    }

    system_ticks = 0;

    asm volatile("msr cntp_tval_el0, %0" ::"r"((uint64_t)timer_interval));
    asm volatile("msr cntp_ctl_el0, %0" ::"r"(1ull));
}

/*
 * Handle one timer tick:
 * - increment software tick counter
 * - wake sleeping tasks
 * - rearm timer for next interrupt
 */
void timer_handle_tick(void)
{
    system_ticks++;
    timer_wake_sleeping_tasks(system_ticks);

    asm volatile("msr cntp_tval_el0, %0" ::"r"((uint64_t)timer_interval));
}
