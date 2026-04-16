#include "sensehat/joystick.h"
#include "rpi4/i2c.h"
#include "rpi4/gpio.h"

/*
 * Sense HAT controller I2C address and joystick state register.
 */
#define SENSEHAT_ADDR 0x46
#define SENSEHAT_KEYS_REG 0xF2

/*
 * Bit layout of the joystick state register.
 */
#define JOY_BIT_DOWN (1u << 0)
#define JOY_BIT_RIGHT (1u << 1)
#define JOY_BIT_UP (1u << 2)
#define JOY_BIT_CENTER (1u << 3)
#define JOY_BIT_LEFT (1u << 4)

/*
 * Last sampled joystick state.
 *
 * Used to detect state transitions and convert them into
 * discrete joystick events.
 */
static uint8_t joystick_prev_state = 0;

/*
 * Configure GPIO23 as joystick interrupt input.
 *
 * The Sense HAT signals a joystick state change on this pin.
 */
static void joystick_init_interrupt(void){
    gpio_use_as_input(JOYSTICK_INT_GPIO);
    
    gpio_enable_rising_edge(JOYSTICK_INT_GPIO);
    gpio_enable_falling_edge(JOYSTICK_INT_GPIO);

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
 * Initialize the joystick driver.
 *
 * This initializes the I2C controller, configures the joystick
 * interrupt pin, and reads the initial joystick state so that
 * the first poll does not generate a fake event.
 */
int joystick_init(void)
{
    i2c_init();
    joystick_init_interrupt();

    if (i2c_read_reg8(SENSEHAT_ADDR, SENSEHAT_KEYS_REG, &joystick_prev_state) < 0)
    {
        return -1;
    }

    return 0;
}

/*
 * Read the current joystick state and convert it into one event.
 *
 * Returns JOY_EVENT_NONE if no relevant state transition occurred
 * or if the I2C read failed.
 */
joy_event_t joystick_read_event(void)
{
    uint8_t state;
    joy_event_t event;

    if (i2c_read_reg8(SENSEHAT_ADDR, SENSEHAT_KEYS_REG, &state) < 0)
    {
        return JOY_EVENT_NONE;
    }

    event = joystick_decode_event(joystick_prev_state, state);
    joystick_prev_state = state;

    return event;
}