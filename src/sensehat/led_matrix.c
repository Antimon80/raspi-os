#include "sensehat/led_matrix.h"
#include "rpi4/i2c.h"
#include "rpi4/i2c_bus.h"
#include "rpi4/uart.h"

/*
 * Sense HAT I2C device address.
 *
 * The joystick code already talks to the same device at address 0x46.
 */
#define SENSEHAT_ADDR 0x46

/*
 * The Sense HAT firmware exposes 192 pixel bytes at register range
 * 0x00..0xBF (8 * 8 * 3 channels).
 */
#define LED_MATRIX_PIXEL_BYTES (MATRIX_WIDTH * MATRIX_HEIGHT * 3)

/*
 * Local shadow framebuffer in 8-bit RGB.
 *
 * The hardware ultimately uses 5 bits per color channel, but the public
 * API keeps the more convenient 0..255 range and converts on present().
 */
static led_matrix_color_t led_matrix_fb[MATRIX_HEIGHT][MATRIX_WIDTH];

/*
 * Return non-zero if the coordinates are inside the 8x8 matrix.
 */
static int led_matrix_valid_coords(int x, int y)
{
    return (x >= 0 && x < MATRIX_WIDTH && y >= 0 && y < MATRIX_HEIGHT);
}

/*
 * Convert an 8-bit channel value to the 5-bit representation used by
 * the Sense HAT firmware.
 */
static uint8_t led_matrix_to_hw_channel(uint8_t value)
{
    return (uint8_t)(value >> 3);
}

/*
 * Convert x/y coordinates to the linear framebuffer index used by the
 * Sense HAT firmware.
 *
 * Layout on the I2C side:
 *   pixel 0: R,G,B
 *   pixel 1: R,G,B
 *   ...
 * for 64 pixels total.
 */
static int led_matrix_pixel_base_index(int x, int y)
{
    return ((y * MATRIX_WIDTH) + x) * 3;
}

/*
 * Initialize the LED matrix driver and clear the display.
 *
 * Returns 0 on success and -1 on I2C failure.
 */
int led_matrix_init(void)
{
    led_matrix_clear();
    return i2c_write_quiet(SENSEHAT_ADDR, (const uint8_t[]){0x00}, 1);
}

/*
 * Clear the shadow framebuffer.
 */
void led_matrix_clear(void)
{
    led_matrix_color_t black = {0, 0, 0};
    led_matrix_fill(black);
}

/*
 * Fill the entire shadow framebuffer with one color.
 */
void led_matrix_fill(led_matrix_color_t color)
{
    int y;
    int x;

    for (y = 0; y < MATRIX_HEIGHT; y++)
    {
        for (x = 0; x < MATRIX_WIDTH; x++)
        {
            led_matrix_fb[y][x] = color;
        }
    }
}

/*
 * Set one pixel in the shadow framebuffer.
 *
 * Returns 0 on success and -1 if the coordinates are invalid.
 */
int led_matrix_set_pixel(int x, int y, led_matrix_color_t color)
{
    if (!led_matrix_valid_coords(x, y))
    {
        return -1;
    }

    led_matrix_fb[y][x] = color;

    return 0;
}

/*
 * Read one pixel back from the shadow framebuffer.
 *
 * Returns 0 on success and -1 on invalid arguments.
 */
int led_matrix_get_pixel(int x, int y, led_matrix_color_t *color)
{
    if (!color || !led_matrix_valid_coords(x, y))
    {
        return -1;
    }

    *color = led_matrix_fb[y][x];
    return 0;
}

/*
 * Push the current shadow framebuffer to the Sense HAT over I2C.
 *
 * The device accepts pixel bytes in register range 0x00..0xBF.
 * We therefore build one transfer buffer:
 *
 *   [0]   = start register (0x00)
 *   [1:]  = 192 pixel bytes in RGB order
 *
 * Returns 0 on success and -1 on I2C failure.
 */
int led_matrix_present(void)
{
    uint8_t tx[1 + LED_MATRIX_PIXEL_BYTES];
    int x;
    int y;
    int base;
    int rc;

    tx[0] = 0x00;

    for (y = 0; y < MATRIX_HEIGHT; y++)
    {
        for (x = 0; x < MATRIX_WIDTH; x++)
        {
            base = 1 + led_matrix_pixel_base_index(x, y);

            tx[base + 0] = led_matrix_to_hw_channel(led_matrix_fb[y][x].r);
            tx[base + 1] = led_matrix_to_hw_channel(led_matrix_fb[y][x].g);
            tx[base + 2] = led_matrix_to_hw_channel(led_matrix_fb[y][x].b);
        }
    }

    i2c_bus_lock();
    rc = i2c_write_quiet(SENSEHAT_ADDR, tx, sizeof(tx));
    i2c_bus_unlock();

    if (rc < 0)
    {
        return -1;
    }

    return 0;
}
