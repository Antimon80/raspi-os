#include "rpi4/gpio.h"
#include "rpi4/mmio.h"

/*
 * Base address of the peripheral register space on Raspberry Pi 4 (BCM2711).
 *
 * In a bare-metal system, hardware devices are accessed via
 * memory-mapped I/O (MMIO). Each device exposes control registers
 * at fixed physical addresses in the CPU's address space.
 *
 * For the BCM2711 SoC used in the Raspberry Pi 4, the peripheral
 * register block begins at address 0xFE000000.
 */
#define PERIPHERAL_BASE ((uintptr_t)0xFE000000)

/*
 * Base address of the GPIO controller registers.
 *
 * The GPIO controller is located at an offset of 0x200000 within the
 * peripheral register space.
 */
#define GPIO_BASE (PERIPHERAL_BASE + 0x200000)

/*
 * GPIO register offsets.
 *
 * These registers control the configuration and state of the GPIO pins.
 *
 * GPFSEL0   - Function select register (selects input/output/alternate function)
 * GPSET0    - Output set register (sets GPIO pin to HIGH)
 * GPCLR0    - Output clear register (sets GPIO pin to LOW)
 * GPPUPPDN0 - Pull-up / pull-down configuration register
 */
#define GPFSEL0 (GPIO_BASE + 0x00)
#define GPSET0 (GPIO_BASE + 0x1C)
#define GPCLR0 (GPIO_BASE + 0x28)
#define GPEDS0 (GPIO_BASE + 0x40)
#define GPREN0 (GPIO_BASE + 0x4C)
#define GPFEN0 (GPIO_BASE + 0x58)
#define GPPUPPDN0 (GPIO_BASE + 0xE4)

/*
 * Generic helper function used to modify a bitfield inside a GPIO register.
 *
 * This function computes:
 *   1. Which register controls the requested pin
 *   2. The bit position inside that register
 *   3. Updates only that bitfield (read-modify-write)
 *
 * Parameters:
 *   pin        - GPIO pin number
 *   value      - value to write into the field
 *   base       - base register address
 *   field_size - number of bits used per pin
 *   field_max  - maximum valid pin number
 *
 * Returns:
 *   1 on success, 0 if parameters are invalid.
 */
static uint32_t gpio_call(uint32_t pin,
                          uint32_t value,
                          uintptr_t base,
                          uint32_t field_size,
                          uint32_t field_max)
{
    uint32_t field_mask;
    uint32_t num_fields;
    uintptr_t reg;
    uint32_t shift;
    uint32_t current_val;

    /* Reject invalid pin numbers */
    if (pin > field_max)
    {
        return 0;
    }

    /* Bitmask covering one field (e.g. 3 bits → 0b111) */
    field_mask = (1u << field_size) - 1u;

    /* Reject values that do not fit into the field */
    if (value > field_mask)
    {
        return 0;
    }

    /*
     * Number of pin fields that fit into one 32-bit register.
     *
     * Example:
     *   field_size = 3 bits → 32 / 3 ≈ 10 pins per register
     */
    num_fields = 32u / field_size;

    /* Determine which register controls this pin */
    reg = base + ((pin / num_fields) * 4u);

    /* Determine bit position inside register */
    shift = (pin % num_fields) * field_size;

    /*
     * Read-modify-write sequence:
     *
     * 1. Read current register value
     * 2. Clear the bits corresponding to the target field
     * 3. Insert the new value
     * 4. Write the result back
     */
    current_val = mmio_read(reg);

    current_val &= ~(field_mask << shift);
    current_val |= (value << shift);

    mmio_write(reg, current_val);

    return 1;
}

/*
 * Helper for 1-bit GPIO registers such as:
 * - rising edge detect enable
 * - falling edge detect enable
 * - event detect status
 */
static uintptr_t gpio_reg_for_pin(uint32_t pin, uintptr_t base)
{
    return base + ((pin / 32u) * 4u);
}

static uint32_t gpio_mask_for_pin(uint32_t pin)
{
    return 1u << (pin % 32u);
}

/*
 * Configure the function of a GPIO pin.
 *
 * Each pin can operate in different modes:
 *   - input
 *   - output
 *   - alternate function (UART, SPI, I2C, etc.)
 *
 * The function selection registers (GPFSELx) allocate
 * 3 bits per pin.
 */
void gpio_set_function(uint32_t pin, gpio_function_t func)
{
    (void)gpio_call(pin, (uint32_t)func, GPFSEL0, 3u, GPIO_MAX_PIN);
}

/*
 * Configure the internal pull-up or pull-down resistor of a GPIO pin.
 *
 * Pull resistors ensure that input pins do not "float" when no
 * external signal is present.
 *
 * The pull configuration registers allocate 2 bits per pin.
 */
void gpio_set_pull(uint32_t pin, gpio_pull_t pull)
{
    (void)gpio_call(pin, (uint32_t)pull, GPPUPPDN0, 2u, GPIO_MAX_PIN);
}

/*
 * Configure a GPIO pin to use Alternate Function 5 (ALT5).
 *
 * On the Raspberry Pi, GPIO pins can be multiplexed to connect
 * to internal peripherals. For example:
 *
 *   GPIO14 → UART TX
 *   GPIO15 → UART RX
 *
 * Both require ALT5 mode when using the mini UART.
 *
 * This helper disables pull resistors and switches the pin
 * to ALT5 mode.
 */
void gpio_use_as_alt5(uint32_t pin)
{
    gpio_set_pull(pin, GPIO_PULL_NONE);
    gpio_set_function(pin, GPIO_FUNC_ALT5);
}

void gpio_use_as_input(uint32_t pin)
{
    gpio_set_pull(pin, GPIO_PULL_NONE);
    gpio_set_function(pin, GPIO_FUNC_INPUT);
}

void gpio_enable_rising_edge(uint32_t pin)
{
    if (pin > GPIO_MAX_PIN)
    {
        return;
    }

    uintptr_t reg = gpio_reg_for_pin(pin, GPREN0);
    uint32_t value = mmio_read(reg);
    value |= gpio_mask_for_pin(pin);
    mmio_write(reg, value);
}

void gpio_enable_falling_edge(uint32_t pin)
{
    if (pin > GPIO_MAX_PIN)
    {
        return;
    }

    uintptr_t reg = gpio_reg_for_pin(pin, GPFEN0);
    uint32_t value = mmio_read(reg);
    value |= gpio_mask_for_pin(pin);
    mmio_write(reg, value);
}

void gpio_disable_rising_edge(uint32_t pin)
{
    if (pin > GPIO_MAX_PIN)
    {
        return;
    }

    uintptr_t reg = gpio_reg_for_pin(pin, GPREN0);
    uint32_t value = mmio_read(reg);
    value &= ~gpio_mask_for_pin(pin);
    mmio_write(reg, value);
}

void gpio_disable_falling_edge(uint32_t pin)
{
    if (pin > GPIO_MAX_PIN)
    {
        return;
    }

    uintptr_t reg = gpio_reg_for_pin(pin, GPFEN0);
    uint32_t value = mmio_read(reg);
    value &= ~gpio_mask_for_pin(pin);
    mmio_write(reg, value);
}

int gpio_event_detected(uint32_t pin)
{
    if (pin > GPIO_MAX_PIN)
    {
        return 0;
    }

    uintptr_t reg = gpio_reg_for_pin(pin, GPEDS0);
    uint32_t value = mmio_read(reg);

    return (value & gpio_mask_for_pin(pin)) != 0;
}

void gpio_clear_event(uint32_t pin)
{
    if (pin > GPIO_MAX_PIN)
    {
        return;
    }

    uintptr_t reg = gpio_reg_for_pin(pin, GPEDS0);
    mmio_write(reg, gpio_mask_for_pin(pin));
}