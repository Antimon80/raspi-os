#include "rpi4/i2c.h"
#include "rpi4/mmio.h"
#include "rpi4/gpio.h"
#include "rpi4/uart.h"

#define PERIPHERAL_BASE ((uintptr_t)0xFE000000)

/*
 * BSC1 / I2C1 on Raspberry Pi GPIO header:
 *   GPIO2  = SDA1
 *   GPIO3  = SCL1
 */
#define BSC1_BASE (PERIPHERAL_BASE + 0x804000)

#define BSC_C (BSC1_BASE + 0x00)
#define BSC_S (BSC1_BASE + 0x04)
#define BSC_DLEN (BSC1_BASE + 0x08)
#define BSC_A (BSC1_BASE + 0x0C)
#define BSC_FIFO (BSC1_BASE + 0x10)
#define BSC_DIV (BSC1_BASE + 0x14)
#define BSC_DEL (BSC1_BASE + 0x18)
#define BSC_CLKT (BSC1_BASE + 0x1C)

/* Control register bits */
#define BSC_C_I2CEN (1u << 15)
#define BSC_C_ST (1u << 7)
#define BSC_C_READ (1u << 0)

/* Status register bits */
#define BSC_S_CLKT (1u << 9)
#define BSC_S_ERR (1u << 8)
#define BSC_S_RXD (1u << 5)
#define BSC_S_TXD (1u << 4)
#define BSC_S_DONE (1u << 1)
#define BSC_S_TA (1u << 0)

/* Clear-on-write status bits */
#define BSC_S_CLEAR (BSC_S_CLKT | BSC_S_ERR | BSC_S_DONE)

#define I2C_DEFAULT_DIVIDER 1500u
#define I2C_WAIT_LIMIT 1000000u

static void i2c_debug_status(const char *where, uint32_t status)
{
    uart_puts("i2c ");
    uart_puts(where);
    uart_puts(" status=");
    uart_put_uint(status);
    uart_puts(" [");

    if (status & BSC_S_CLKT)
        uart_puts("CLKT ");
    if (status & BSC_S_ERR)
        uart_puts("ERR ");
    if (status & BSC_S_RXD)
        uart_puts("RXD ");
    if (status & BSC_S_TXD)
        uart_puts("TXD ");
    if (status & BSC_S_DONE)
        uart_puts("DONE ");
    if (status & BSC_S_TA)
        uart_puts("TA ");

    uart_puts("]\n");
}

/*
 * Reset the controller into a known idle state.
 */
static void i2c_reset_controller(void)
{
    mmio_write(BSC_C, 0);
    mmio_write(BSC_S, BSC_S_CLEAR);

    /* clear FIFO */
    mmio_write(BSC_C, BSC_C_I2CEN | (2u << 4));

    mmio_write(BSC_DIV, I2C_DEFAULT_DIVIDER);
    mmio_write(BSC_DEL, 0x00300030);

    /* disable clock-stretch timeout for now */
    mmio_write(BSC_CLKT, 0);

    mmio_write(BSC_C, BSC_C_I2CEN);
    mmio_write(BSC_S, BSC_S_CLEAR);
}

/*
 * Prepare a fresh transfer.
 */
static void i2c_prepare_transfer(uint8_t addr, uint32_t dlen)
{
    mmio_write(BSC_C, 0);
    mmio_write(BSC_S, BSC_S_CLEAR);

    /* clear FIFO */
    mmio_write(BSC_C, BSC_C_I2CEN | (2u << 4));

    mmio_write(BSC_A, addr);
    mmio_write(BSC_DLEN, dlen);
}

/*
 * Wait until DONE is set or an error/timeout occurs.
 */
static int i2c_wait_for_done(void)
{
    uint32_t timeout = I2C_WAIT_LIMIT;

    while (timeout-- > 0)
    {
        uint32_t status = mmio_read(BSC_S);

        if (status & BSC_S_DONE)
        {
            if (status & (BSC_S_ERR | BSC_S_CLKT))
            {
                i2c_debug_status("done-error", status);
                i2c_reset_controller();
                return -1;
            }

            mmio_write(BSC_S, BSC_S_CLEAR);
            return 0;
        }

        if (status & (BSC_S_ERR | BSC_S_CLKT))
        {
            i2c_debug_status("wait-error", status);
            i2c_reset_controller();
            return -1;
        }
    }

    i2c_debug_status("wait-timeout", mmio_read(BSC_S));
    i2c_reset_controller();
    return -1;
}

void i2c_init(void)
{
    gpio_use_as_alt0(2);
    gpio_use_as_alt0(3);

    i2c_reset_controller();
}

int i2c_write(uint8_t addr, const uint8_t *data, size_t len)
{
    size_t written = 0;
    uint32_t timeout = I2C_WAIT_LIMIT;

    if (!data || len == 0)
    {
        return -1;
    }

    i2c_prepare_transfer(addr, (uint32_t)len);

    while (written < len)
    {
        uint32_t status = mmio_read(BSC_S);

        if (status & (BSC_S_ERR | BSC_S_CLKT))
        {
            i2c_debug_status("write-loop-error", status);
            i2c_reset_controller();
            return -1;
        }

        if (status & BSC_S_TXD)
        {
            mmio_write(BSC_FIFO, data[written++]);
            timeout = I2C_WAIT_LIMIT;
            continue;
        }

        if (timeout-- == 0)
        {
            i2c_debug_status("write-loop-timeout", mmio_read(BSC_S));
            i2c_reset_controller();
            return -1;
        }
    }

    mmio_write(BSC_C, BSC_C_I2CEN | BSC_C_ST);

    return i2c_wait_for_done();
}

int i2c_read(uint8_t addr, uint8_t *data, size_t len)
{
    size_t received = 0;
    uint32_t timeout = I2C_WAIT_LIMIT;

    if (!data || len == 0)
    {
        return -1;
    }

    i2c_prepare_transfer(addr, (uint32_t)len);
    mmio_write(BSC_C, BSC_C_I2CEN | BSC_C_ST | BSC_C_READ);

    while (received < len)
    {
        uint32_t status = mmio_read(BSC_S);

        if (status & (BSC_S_ERR | BSC_S_CLKT))
        {
            i2c_debug_status("read-loop-error", status);
            i2c_reset_controller();
            return -1;
        }

        if (status & BSC_S_RXD)
        {
            data[received++] = (uint8_t)mmio_read(BSC_FIFO);
            timeout = I2C_WAIT_LIMIT;
            continue;
        }

        if (timeout-- == 0)
        {
            i2c_debug_status("read-loop-timeout", mmio_read(BSC_S));
            i2c_reset_controller();
            return -1;
        }
    }

    return i2c_wait_for_done();
}

int i2c_write_read(uint8_t addr,
                   const uint8_t *tx_data,
                   size_t tx_len,
                   uint8_t *rx_data,
                   size_t rx_len)
{

    if (!tx_data || tx_len == 0 || !rx_data || rx_len == 0)
    {
        return -1;
    }

    if (i2c_write(addr, tx_data, tx_len) < 0)
    {
        uart_puts("i2c_write_read: write failed\n");
        return -1;
    }
    
    for(volatile int i = 0; i < 1000; i++)
    i2c_reset_controller();
    
    if (i2c_read(addr, rx_data, rx_len) < 0)
    {
        uart_puts("i2c_write_reas: rad failed\n");
        return -1;
    }

    return 0;
}

int i2c_write_reg8(uint8_t addr, uint8_t reg, uint8_t value)
{
    uint8_t buffer[2];

    buffer[0] = reg;
    buffer[1] = value;

    return i2c_write(addr, buffer, 2);
}

int i2c_read_reg8(uint8_t addr, uint8_t reg, uint8_t *value)
{
    if (!value)
    {
        return -1;
    }

    return i2c_write_read(addr, &reg, 1, value, 1);
}