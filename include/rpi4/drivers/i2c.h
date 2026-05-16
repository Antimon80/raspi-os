#ifndef RPI4_DRIVERS_I2C_H
#define RPI4_DRIVERS_I2C_H

#include <stdint.h>
#include <stddef.h>

void i2c_init(void);

int i2c_write(uint8_t addr, const uint8_t *data, size_t len);
int i2c_read(uint8_t addr, uint8_t *data, size_t len);
int i2c_write_read(uint8_t addr, const uint8_t *tx_data, size_t tx_len, uint8_t *rx_data, size_t rx_len);
int i2c_write_reg8(uint8_t addr, uint8_t reg, uint8_t value);
int i2c_read_reg8(uint8_t addr, uint8_t reg, uint8_t *value);

void i2c_handle_irq(void);
void i2c_recover(void);

#endif
