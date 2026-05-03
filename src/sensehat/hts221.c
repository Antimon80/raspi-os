#include "sensehat/hts221.h"
#include "rpi4/i2c.h"

#define HTS221_ADDR 0x5f      // I2C address of the HTS221 sensor
#define HTS221_WHO_AMI_I 0x0f // Device ID register
#define HTS221_WHO_VALUE 0xbc // Expected WHO_AM_I register value

#define HTS221_AV_CONF 0x10   // Averaging configuration register
#define HTS221_CTRL_REG1 0x20 // Control register (power, ODR, BDU)
#define HTS221_AUTO_INC 0x80  // Auto-increment flag for multi-byte reads

#define HTS221_HUMIDITY_OUT_L 0x28 // Humidity output (LSB, auto-increment for 16 bit)
#define HTS221_H0_RH_X2 0x30       // Calibration: humidity at point H0 (value * 2)
#define HTS221_H1_RH_X2 0x31       // Calibration: humidity at pont H1 (value * 2)
#define HTS221_H0_T0_OUT_L 0x36    // Calibration: raw output for H0 (LSB)
#define HTS221_H0_T0_OUT_H 0x37    // Calibration: raw output for H0 (MSB)
#define HTS221_H1_T0_OUT_L 0x3a    // Calibration: raw output for H1 (LSB)
#define HTS221_H1_T0_OUT_H 0x3b    // Calibration: raw output for H1 (MSB)

#define HTS221_TEMP_OUT_L 0x2A // Temperature output (LSB, auto-increment for 16 bit)
#define HTS221_T0_DEGC_X8 0x32 // Calibration: temperature T0 (value * 8, LSB)
#define HTS221_T1_DEGC_X8 0x33 // Calibration: temperature T1 (value * 8, LSB)
#define HTS221_T1_T0_MSB 0x35  // Calibration: MSB bits for T0 and T1 values
#define HTS221_T0_OUT_L 0x3C   // Calibration: raw output for T0 (LSB)
#define HTS221_T0_OUT_H 0x3D   // Calibration: raw output for T0 (MSB)
#define HTS221_T1_OUT_L 0x3E   // Calibration: raw output for T1 (LSB)
#define HTS221_T1_OUT_H 0x3F   // Calibration: raw output for T1 (MSB)

/*
 * Cached calibration data read from the sensor.
 *
 * Contains both humidity and temperature calibration parameters
 * required for linear interpolation of raw measurements.
 */
typedef struct
{
    int32_t h0_centi_percent;
    int32_t h1_centi_percent;
    int16_t h0_out;
    int16_t h1_out;

    int32_t t0_centi_c;
    int32_t t1_centi_c;
    int16_t t0_out;
    int16_t t1_out;

    int valid;
} hts221_calib_t;

static hts221_calib_t calib;

/*
 * Read a 16-bit little-endian register value using auto-increment.
 *
 * Returns 0 on success, -1 on I2C failure or invalid argument.
 */
static int hts221_read_reg16(uint8_t reg, int16_t *value)
{
    uint8_t addr = reg | HTS221_AUTO_INC;
    uint8_t data[2];

    if (!value)
    {
        return -1;
    }

    if (i2c_write_read(HTS221_ADDR, &addr, 1, data, 2) < 0)
    {
        return -1;
    }

    *value = (int16_t)(((uint16_t)data[1] << 8) | data[0]);
    return 0;
}

/*
 * Read and cache calibration data from the HTS221 sensor.
 *
 * This includes:
 *  - Humidity calibration points (H0, H1)
 *  - Temperature calibration points (T0, T1)
 *
 * The raw sensor output is later converted using linear interpolation
 * between these calibration points.
 *
 * Returns 0 on success, -1 on failure.
 */
static int hts221_read_calibration(void)
{
    // humidity calibration
    uint8_t h0_x2;
    uint8_t h1_x2;

    if (i2c_read_reg8(HTS221_ADDR, HTS221_H0_RH_X2, &h0_x2) < 0)
    {
        return -1;
    }

    if (i2c_read_reg8(HTS221_ADDR, HTS221_H1_RH_X2, &h1_x2) < 0)
    {
        return -1;
    }

    calib.h0_centi_percent = ((int32_t)h0_x2 * 100) / 2;
    calib.h1_centi_percent = ((int32_t)h1_x2 * 100) / 2;

    if (hts221_read_reg16(HTS221_H0_T0_OUT_L, &calib.h0_out) < 0)
    {
        return -1;
    }

    if (hts221_read_reg16(HTS221_H1_T0_OUT_L, &calib.h1_out) < 0)
    {
        return -1;
    }

    if (calib.h1_out == calib.h0_out)
    {
        return -1;
    }

    // temperature calibration
    uint8_t t0_x8_lsb;
    uint8_t t1_x8_lsb;
    uint8_t t1_t0_msb;
    uint16_t t0_x8;
    uint16_t t1_x8;

    if (i2c_read_reg8(HTS221_ADDR, HTS221_T0_DEGC_X8, &t0_x8_lsb) < 0)
    {
        return -1;
    }

    if (i2c_read_reg8(HTS221_ADDR, HTS221_T1_DEGC_X8, &t1_x8_lsb) < 0)
    {
        return -1;
    }

    if (i2c_read_reg8(HTS221_ADDR, HTS221_T1_T0_MSB, &t1_t0_msb) < 0)
    {
        return -1;
    }

    t0_x8 = ((uint16_t)(t1_t0_msb & 0x03) << 8) | t0_x8_lsb;
    t1_x8 = ((uint16_t)(t1_t0_msb & 0x0C) << 6) | t1_x8_lsb;

    calib.t0_centi_c = ((int32_t)t0_x8 * 100) / 8;
    calib.t1_centi_c = ((int32_t)t1_x8 * 100) / 8;

    if (hts221_read_reg16(HTS221_T0_OUT_L, &calib.t0_out) < 0)
    {
        return -1;
    }

    if (hts221_read_reg16(HTS221_T1_OUT_L, &calib.t1_out) < 0)
    {
        return -1;
    }

    if (calib.t1_out == calib.t0_out)
    {
        return -1;
    }

    calib.valid = 1;
    return 0;
}

/*
 * Initialize the HTS221 sensor.
 *
 * Verifies device identity, configures averaging and output data rate,
 * and reads calibration data required for value conversion.
 *
 * Returns 0 on success, -1 on failure.
 */
int hts221_init(void)
{
    uint8_t who = 0;

    calib.valid = 0;

    if (i2c_read_reg8(HTS221_ADDR, HTS221_WHO_AMI_I, &who) < 0)
    {
        return -1;
    }

    if (who != HTS221_WHO_VALUE)
    {
        return -1;
    }

    if (i2c_write_reg8(HTS221_ADDR, HTS221_AV_CONF, 0x1b) < 0)
    {
        return -1;
    }

    if (i2c_write_reg8(HTS221_ADDR, HTS221_CTRL_REG1, 0x85) < 0)
    {
        return -1;
    }

    return hts221_read_calibration();
}

/*
 * Read raw humidity value from the sensor.
 *
 * The returned value is the unprocessed ADC output and must be converted
 * using calibration data to obtain relative humidity.
 *
 * Returns 0 on success, -1 on failure.
 */
int hts221_read_humidity_raw(int16_t *humidity_raw)
{
    if (!humidity_raw)
    {
        return -1;
    }

    return hts221_read_reg16(HTS221_HUMIDITY_OUT_L, humidity_raw);
}

/*
 * Read humidity and convert to centi-percent (0.01% resolution).
 *
 * Uses linear interpolation between calibration points:
 *
 *   H = H0 + (raw - H0_out) * (H1 - H0) / (H1_out - H0_out)
 *
 * The result is clipped to the range 0..100%.
 *
 * Returns 0 on success, -1 on failure.
 */
int hts221_read_humidity_centi_percent(int32_t *humidity_centi_percent, int16_t *humidity_raw)
{
    int16_t raw;
    int32_t numerator;
    int32_t denominator;
    int32_t result;

    if (!humidity_centi_percent || !calib.valid)
    {
        return -1;
    }

    if (hts221_read_humidity_raw(&raw) < 0)
    {
        return -1;
    }

    numerator = ((int32_t)raw - calib.h0_out) * (calib.h1_centi_percent - calib.h0_centi_percent);
    denominator = (int32_t)calib.h1_out - calib.h0_out;
    result = calib.h0_centi_percent + numerator / denominator;

    if (result < 0)
    {
        result = 0;
    }
    else if (result > 10000)
    {
        result = 10000;
    }

    *humidity_centi_percent = result;

    if (humidity_raw)
    {
        *humidity_raw = raw;
    }

    return 0;
}

/*
 * Read raw temperature value from the sensor.
 *
 * The returned value is the unprocessed ADC output and must be converted
 * using calibration data to obtain temperature.
 *
 * Returns 0 on success, -1 on failure.
 */
int hts221_read_temperature_raw(int16_t *temperature_raw)
{
    if (!temperature_raw)
    {
        return -1;
    }

    return hts221_read_reg16(HTS221_TEMP_OUT_L, temperature_raw);
}

/*
 * Read temperature and convert to centi-degrees Celsius (0.01 °C resolution).
 *
 * Uses linear interpolation between calibration points:
 *
 *   T = T0 + (raw - T0_out) * (T1 - T0) / (T1_out - T0_out)
 *
 * Returns 0 on success, -1 on failure.
 */
int hts221_read_temperature_centi_c(int32_t *temperature_centi_c, int16_t *temperature_raw)
{
    int16_t raw;
    int32_t numerator;
    int32_t denominator;
    int32_t result;

    if (!temperature_centi_c || !calib.valid)
    {
        return -1;
    }

    if (hts221_read_temperature_raw(&raw) < 0)
    {
        return -1;
    }

    numerator = ((int32_t)raw - calib.t0_out) * (calib.t1_centi_c - calib.t0_centi_c);
    denominator = (int32_t)calib.t1_out - calib.t0_out;
    result = calib.t0_centi_c + numerator / denominator;

    *temperature_centi_c = result;

    if (temperature_raw)
    {
        *temperature_raw = raw;
    }

    return 0;
}