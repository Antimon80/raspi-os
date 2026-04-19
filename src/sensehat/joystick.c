#include "kernel/irq.h"
#include "kernel/tasks/joystick_task.h"
#include "kernel/sched/scheduler.h"
#include "sensehat/joystick.h"
#include "rpi4/i2c.h"
#include "rpi4/i2c_bus.h"
#include "rpi4/gpio.h"
#include "rpi4/uart.h"

/*
 * Sense HAT controller I2C address and joystick state register.
 */
#define SENSEHAT_ADDR 0x46
#define JOYSTICK_KEYS_REG 0xF2

/*
 * Bit layout of the joystick state register.
 */
#define JOY_BIT_DOWN (1u << 0)
#define JOY_BIT_RIGHT (1u << 1)
#define JOY_BIT_UP (1u << 2)
#define JOY_BIT_CENTER (1u << 3)
#define JOY_BIT_LEFT (1u << 4)

#define JOY_EVENT_QUEUE_SIZE 16

/*
 * Last sampled joystick state.
 *
 * Used to detect state transitions and convert them into
 * discrete joystick events.
 */
static uint8_t joystick_prev_state = 0;

static joy_event_t joystick_event_queue[JOY_EVENT_QUEUE_SIZE];
static volatile uint32_t joystick_event_head = 0;
static volatile uint32_t joystick_event_tail = 0;

/*
 * Return non-zero if the event queue is empty.
 *
 * Used to check whether a task needs to block waiting for input.
 */
static int joystick_queue_is_empty(void)
{
    return joystick_event_head == joystick_event_tail;
}

/*
 * Return non-zero if the event queue is full.
 *
 * Prevents overwriting unread events in the circular buffer.
 */
static int joystick_queue_is_full(void)
{
    return ((joystick_event_head + 1) % JOY_EVENT_QUEUE_SIZE) == joystick_event_tail;
}

/*
 * Enqueue a decoded joystick event.
 *
 * This function:
 * - drops JOY_EVENT_NONE
 * - inserts the event into the circular buffer
 * - wakes up the joystick task if one is registered
 *
 * Must be IRQ-safe because it may be called from interrupt context.
 */
static void joystick_enqueue_event(joy_event_t event)
{
    unsigned int next;
    int id;

    if (event == JOY_EVENT_NONE)
    {
        return;
    }

    irq_disable();

    if (joystick_queue_is_full())
    {
        irq_enable();
        return;
    }

    joystick_event_queue[joystick_event_head] = event;
    next = (joystick_event_head + 1u) % JOY_EVENT_QUEUE_SIZE;
    joystick_event_head = next;

    irq_enable();

    id = joystick_get_task_id();
    if (id >= 0)
    {
        task_wakeup(id);
    }
}

/*
 * Configure GPIO23 as joystick interrupt input.
 *
 * The Sense HAT signals a joystick state change on this pin.
 */
static void joystick_init_interrupt(void)
{
    gpio_use_as_input(JOYSTICK_INT_GPIO);

    gpio_disable_rising_edge(JOYSTICK_INT_GPIO);
    gpio_disable_falling_edge(JOYSTICK_INT_GPIO);
    gpio_clear_event(JOYSTICK_INT_GPIO);

    gpio_enable_rising_edge(JOYSTICK_INT_GPIO);
    gpio_clear_event(JOYSTICK_INT_GPIO);
}

/*
 * Convert a joystick state transition into one menu event.
 *
 * For the center button, both press and release are reported.
 * For direction buttons, only the press edge generates an event.
 */
static joy_event_t joystick_decode_event(uint8_t prev, uint8_t curr)
{
    uint8_t changed = (uint8_t)(prev ^ curr);

    if (changed == 0)
    {
        return JOY_EVENT_NONE;
    }

    // center button:
    // report both press and release because the menu logic
    // distinguishes short and long presses
    if (changed & JOY_BIT_CENTER)
    {
        if (curr & JOY_BIT_CENTER)
        {
            return JOY_EVENT_CENTER_PRESS;
        }
        else
        {
            return JOY_EVENT_CENTER_RELEASE;
        }
    }

    // direction buttons: generate one event on press only
    if ((changed & JOY_BIT_UP) && (curr & JOY_BIT_UP))
    {
        return JOY_EVENT_UP;
    }

    if ((changed & JOY_BIT_DOWN) && (curr & JOY_BIT_DOWN))
    {
        return JOY_EVENT_DOWN;
    }

    if ((changed & JOY_BIT_LEFT) && (curr & JOY_BIT_LEFT))
    {
        return JOY_EVENT_LEFT;
    }

    if ((changed & JOY_BIT_RIGHT) && (curr & JOY_BIT_RIGHT))
    {
        return JOY_EVENT_RIGHT;
    }
    return JOY_EVENT_NONE;
}

/*
 * Poll the joystick state over I2C and generate an event if needed.
 *
 * This reads the current state register, compares it with the previous
 * state, decodes a single event and enqueues it.
 *
 * Called when a GPIO interrupt signals a state change.
 */
void joystick_service_change(void)
{
    int rc;
    uint8_t state;
    joy_event_t event;

    i2c_bus_lock();
    rc = i2c_read_reg8(SENSEHAT_ADDR, JOYSTICK_KEYS_REG, &state);
    i2c_bus_unlock();

    if (rc < 0)
    {
        uart_puts("joystick: i2c read failed\n");
        return;
    }

    event = joystick_decode_event(joystick_prev_state, state);
    joystick_prev_state = state;

    joystick_enqueue_event(event);
}

/*
 * Initialize the joystick driver.
 *
 * This initializes the I2C controller, configures the joystick
 * interrupt pin, and reads the initial joystick state so that
 * the first poll does not generate a fake event.
 */
int joystick_init(void)
{
    int rc;

    joystick_init_interrupt();

    joystick_event_head = 0;
    joystick_event_tail = 0;

    i2c_bus_lock();
    rc = i2c_read_reg8(SENSEHAT_ADDR, JOYSTICK_KEYS_REG, &joystick_prev_state);
    i2c_bus_unlock();
    
    if (rc < 0)
    {
        uart_puts("joystick init: initial read failed\n");
        return -1;
    }

    return 0;
}

/*
 * Return non-zero if at least one joystick event is available.
 *
 * Can be used by tasks to decide whether to read or block.
 */
int joystick_has_event(void)
{
    return !joystick_queue_is_empty();
}

/*
 * Dequeue and return the next joystick event.
 *
 * Returns JOY_EVENT_NONE if the queue is empty.
 *
 * Access to the circular buffer is protected by disabling IRQs
 * to avoid race conditions with interrupt-driven producers.
 */
joy_event_t joystick_read_event(void)
{
    joy_event_t event;

    irq_disable();

    if (joystick_queue_is_empty())
    {
        irq_enable();
        return JOY_EVENT_NONE;
    }

    event = joystick_event_queue[joystick_event_tail];
    joystick_event_tail = (joystick_event_tail + 1u) % JOY_EVENT_QUEUE_SIZE;

    irq_enable();
    return event;
}