#ifndef RPI4_I2C_BUS_H
#define RPI4_I2C_BUS_H

void i2c_bus_init(void);
void i2c_bus_lock(void);
int i2c_bus_try_lock(void);
void i2c_bus_unlock(void);

#endif