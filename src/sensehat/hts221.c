#include "sensehat/hts221.h"
#include "rpi4/i2c.h"

#define HTS221_ADDR 0x5f
#define HTS221_WHO_AMI_I 0x0f
#define HTS221_AV_CONF 0x10
#define HTS221_CTRL_REG1 0x20
#define HTS221_HUMIDITY_OUT_L 0x28
#define HTS221_H0_RH_X2 0x30
#define HTS221_H1_RH_X2 0x31
#define HTS221_H0_T0_OUT_L 0x36
#define HTS221_H0_T0_OUT_H 0x37
#define HTS221_H1_T0_OUT_L 0x3a
#define HTS221_H1_T0_OUT_H 0x3b
#define HTS221_WHO_VALUE 0xbc
#define HTS221_AUTO_INC 0x80

typedef struct
{
    int32_t h0_centi_percent;
    int32_t h1_centi_percent;
    int16_t h0_out;
    int16_t h1_out;
    int valid;
} hts221_calib_t;

static hts221_calib_t calib;

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

static int hts221_read_calibration(void)
{
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

    calib.valid = 1;
    return 0;
}

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

int hts221_read_humidity_raw(int16_t *humidity_raw)
{
    if (!humidity_raw)
    {
        return -1;
    }

    return hts221_read_reg16(HTS221_HUMIDITY_OUT_L, humidity_raw);
}

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