#include "rpi4/i2c.h"
#include "rpi4/mmio.h"
#include "rpi4/gpio.h"

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
#define BSC_C_CLEAR (1u << 4) // with bit 5 = FIFO clear
#define BSC_C_READ (1u << 0)

/* Status register bits */
#define BSC_S_CLKT (1u << 9)
#define BSC_S_ERR (1u << 8)
#define BSC_S_RXF (1u << 7)
#define BSC_S_TXE (1u << 6)
#define BSC_S_RXD (1u << 5)
#define BSC_S_TXD (1u << 4)
#define BSC_S_RXR (1u << 3)
#define BSC_S_TXW (1u << 2)
#define BSC_S_DONE (1u << 1)
#define BSC_S_TA (1u << 0)

/* Clear-on-write status bits */
#define BSC_S_CLEAR (BSC_S_CLKT | BSC_S_ERR | BSC_S_DONE)

#define I2C_DEFAULT_DIVIDER 1500u
#define I2C_WAIT_LIMIT 1000000u

/*
 * Wait until the current I2C transfer has completed.
 *
 * The transfer is considered successful when the DONE bit is set
 * without an accompanying error or clock timeout condition.
 *
 * Returns:
 *   0  on success
 *  -1  on timeout or transfer error
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
                mmio_write(BSC_S, BSC_S_CLEAR);
                return -1;
            }

            mmio_write(BSC_S, BSC_S_CLEAR);
            return 0;
        }
    }

    return -1;
}

/*
 * Prepare the BSC controller for a new transfer.
 *
 * This resets pending status flags, clears the FIFO, and configures
 * the target slave address and transfer length.
 */
static void i2c_prepare_transfer(uint8_t addr, uint32_t dlen)
{
    // clear status flags
    mmio_write(BSC_S, BSC_S_CLEAR);

    // clear FIFO
    mmio_write(BSC_C, BSC_C_I2CEN | (2u << 4));

    // configure slave address and data length
    mmio_write(BSC_A, addr);
    mmio_write(BSC_DLEN, dlen);
}

/*
 * Initialize the I2C1 controller.
 *
 * This configures the GPIO pins for SDA/SCL, resets the controller state,
 * sets a default clock divider, configures timing parameters, and enables
 * the BSC peripheral.
 */
void i2c_init(void)
{
    gpio_use_as_alt0(2);
    gpio_use_as_alt0(3);

    mmio_write(BSC_C, 0);
    mmio_write(BSC_S, BSC_S_CLEAR);

    mmio_write(BSC_DIV, I2C_DEFAULT_DIVIDER);

    mmio_write(BSC_DEL, 0x00300030);
    mmio_write(BSC_CLKT, 0x40);

    mmio_write(BSC_C, BSC_C_I2CEN);
}

/*
 * Write a sequence of bytes to an I2C slave.
 *
 * The bytes are placed into the transmit FIFO and then the transfer
 * is started. The function blocks until the write has completed or
 * an error occurs.
 *
 * Returns:
 *   0  on success
 *  -1  on invalid parameters, timeout, or bus error
 */
int i2c_write(uint8_t addr, const uint8_t *data, size_t len)
{
    if (!data || len == 0)
    {
        return -1;
    }

    i2c_prepare_transfer(addr, (uint32_t)len);

    size_t written = 0;

    while (written < len)
    {
        uint32_t timeout = I2C_WAIT_LIMIT;

        while (!(mmio_read(BSC_S) & BSC_S_TXD))
        {
            uint32_t status = mmio_read(BSC_S);

            if (status & (BSC_S_ERR | BSC_S_CLKT))
            {
                mmio_write(BSC_S, BSC_S_CLEAR);
                return -1;
            }

            if (timeout-- == 0)
            {
                return -1;
            }
        }

        mmio_write(BSC_FIFO, data[written++]);
    }

    mmio_write(BSC_C, BSC_C_I2CEN | BSC_C_ST);

    return i2c_wait_for_done();
}

/*
 * Read a sequence of bytes from an I2C slave.
 *
 * The controller is switched into read mode and the function collects
 * bytes from the receive FIFO until the requested length has been read
 * or an error occurs.
 *
 * Returns:
 *   0  on success
 *  -1  on invalid parameters, timeout, or bus error
 */
int i2c_read(uint8_t addr, uint8_t *data, size_t len)
{
    if (!data || len == 0)
    {
        return -1;
    }

    i2c_prepare_transfer(addr, (uint32_t)len);

    mmio_write(BSC_C, BSC_C_I2CEN | BSC_C_ST | BSC_C_READ);

    size_t received = 0;
    uint32_t timeout = I2C_WAIT_LIMIT;

    while (received < len)
    {
        uint32_t status = mmio_read(BSC_S);

        if (status & (BSC_S_ERR | BSC_S_CLKT))
        {
            mmio_write(BSC_S, BSC_S_CLEAR);
            return -1;
        }

        while ((mmio_read(BSC_S) & BSC_S_RXD) && received < len)
        {
            data[received++] = (uint8_t)mmio_read(BSC_FIFO);
            timeout = I2C_WAIT_LIMIT;
        }

        if (timeout-- == 0)
        {
            return -1;
        }
    }

    return i2c_wait_for_done();
}

/*
 * Perform a write followed by a read on the same slave device.
 *
 * This is commonly used for register-based I2C devices:
 * first a register address is written, then data is read back.
 *
 * Note:
 * This implementation performs two separate transfers and does not use
 * a repeated-start condition.
 *
 * Returns:
 *   0  on success
 *  -1  on invalid parameters or transfer failure
 */
int i2c_write_read(uint8_t addr, const uint8_t *tx_data, size_t tx_len, uint8_t *rx_data, size_t rx_len)
{
    if (!tx_data || tx_len == 0 || !rx_data || rx_len == 0)
    {
        return -1;
    }

    if (i2c_write(addr, tx_data, tx_len) < 0)
    {
        return -1;
    }

    return i2c_read(addr, rx_data, rx_len);
}

/*
 * Write a single 8-bit value to an 8-bit register of an I2C device.
 *
 * The transfer format is:
 *   [register][value]
 */
int i2c_write_reg8(uint8_t addr, uint8_t reg, uint8_t value)
{
    uint8_t buffer[2];
    buffer[0] = reg;
    buffer[1] = value;
    return i2c_write(addr, buffer, 2);
}

/*
 * Read a single 8-bit value from an 8-bit register of an I2C device.
 *
 * The register address is written first, then one byte is read back.
 */
int i2c_read_reg8(uint8_t addr, uint8_t reg, uint8_t *value)
{
    if (!value)
    {
        return -1;
    }

    return i2c_write_read(addr, &reg, 1, value, 1);
}