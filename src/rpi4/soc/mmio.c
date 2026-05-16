#include "rpi4/soc/mmio.h"

/*
 * Write a 32-bit value to a memory-mapped hardware register.
 *
 * In MMIO, device registers are mapped into the CPU's physical address
 * space. This means that reading or writing a specific memory address
 * actually communicates with a hardware device instead of normal RAM.
 *
 * Example:
 *   Writing to a UART register may transmit a character.
 *   Writing to a GPIO register may change the state of a pin.
 *
 * Parameters:
 *   reg - physical address of the hardware register
 *   val - value to write into the register
 *
 * The pointer is declared as "volatile" because the value stored at
 * this address may change independently of the program (due to hardware).
 * This prevents the compiler from optimizing away the memory access.
 */
void mmio_write(uintptr_t reg, uint32_t val){
    *(volatile uint32_t *)reg = val;
}

/*
 * Read a 32-bit value from a memory-mapped hardware register.
 *
 * This performs the inverse operation of mmio_write(): it reads the
 * current value stored in a device register.
 *
 * Typical uses:
 *   - Checking device status flags
 *   - Reading received UART characters
 *   - Reading sensor data
 *
 * Parameters:
 *   reg - physical address of the hardware register
 *
 * Returns:
 *   The current 32-bit value stored in the register.
 */
uint32_t mmio_read(uintptr_t reg){
    return *(volatile uint32_t *)reg;
}