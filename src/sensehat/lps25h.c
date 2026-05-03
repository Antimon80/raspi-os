#include "sensehat/lps25h.h"
#include "rpi4/i2c.h"

#define LPS25H_ADDR 0x5c         // I2C address of the LPS25H pressure sensor
#define LPS25H_WHO_AM_I 0x0f     // Device ID register
#define LPS25H_WHO_VALUE 0xbd    // Expected WHO_AM_I value
#define LPS25H_CTRL_REG1 0x20    // Control register for power mode, ODR and BDU
#define LPS25H_PRESS_OUT_XL 0x28 // Pressure output register, lowest byte
#define LPS25H_AUTO_INC 0x80     // Auto-increment flag for multi-byte reads

/*
 * Initialize the LPS25H pressure sensor.
 *
 * Verifies the device identity and enables the sensor with 1 Hz output data
 * rate and block data update.
 *
 * Returns 0 on success, -1 on failure.
 */
int lps25h_init(void)
{
    uint8_t who = 0;
    if (i2c_read_reg8(LPS25H_ADDR, LPS25H_WHO_AM_I, &who) < 0)
    {
        return -1;
    }

    if (who != LPS25H_WHO_VALUE)
    {
        return -1;
    }

    if (i2c_write_reg8(LPS25H_ADDR, LPS25H_CTRL_REG1, 0x90) < 0)
    {
        return -1;
    }

    return 0;
}

/*
 * Read the raw 24-bit pressure value from the sensor.
 *
 * The pressure registers are read as XL, L and H bytes using auto-increment.
 * The result is sign-extended to a 32-bit integer.
 *
 * Returns 0 on success, -1 on failure.
 */
int lps25h_read_pressure_raw(int32_t *pressure_raw)
{
    uint8_t reg = LPS25H_PRESS_OUT_XL | LPS25H_AUTO_INC;
    uint8_t data[3];
    int32_t raw;

    if (!pressure_raw)
    {
        return -1;
    }

    if (i2c_write_read(LPS25H_ADDR, &reg, 1, data, 3) < 0)
    {
        return -1;
    }

    raw = ((int32_t)data[2] << 16) | ((int32_t)data[1] << 8) | ((int32_t)data[0]);

    if (raw & 0x00800000)
    {
        raw |= 0xff000000;
    }

    *pressure_raw = raw;
    return 0;
}

/*
 * Read pressure and convert it to centi-hPa.
 *
 * The LPS25H pressure sensitivity is 4096 LSB/hPa, so the converted value is
 * stored as hPa * 100 to avoid floating point arithmetic.
 *
 * Returns 0 on success, -1 on failure.
 */
int lps25h_read_pressure_centi_hpa(int32_t *pressure_centi_hpa, int32_t *pressure_raw)
{
    int32_t raw;

    if (!pressure_centi_hpa)
    {
        return -1;
    }

    if (lps25h_read_pressure_raw(&raw) < 0)
    {
        return -1;
    }

    *pressure_centi_hpa = (raw * 100) / 4096;

    if (pressure_raw)
    {
        *pressure_raw = raw;
    }

    return 0;
}