#include "rpi4/i2c_bus.h"
#include "kernel/sync/mutex.h"

static mutex_t i2c_bus_mutex;

/*
 * Initialize the global I2C bus lock.
 */
void i2c_bus_init(void)
{
    mutex_init(&i2c_bus_mutex);
}

/*
 * Acquire exclusive access to the I2C bus.
 */
void i2c_bus_lock(void)
{
    mutex_lock(&i2c_bus_mutex);
}

/*
 * Try to acquire exclusive access to the I2C bus.
 *
 * Returns 1 on success, 0 otherwise.
 */
int i2c_bus_try_lock(void)
{
    return mutex_try_lock(&i2c_bus_mutex);
}

/*
 * Release exclusive access to the I2C bus.
 */
void i2c_bus_unlock(void)
{
    mutex_unlock(&i2c_bus_mutex);
}