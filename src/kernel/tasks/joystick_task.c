#include "kernel/shell/joy_menu.h"
#include "kernel/sched/scheduler.h"
#include "kernel/irq.h"
#include "sensehat/joystick.h"
#include "rpi4/uart.h"
#include "rpi4/i2c.h"

#define PERIPHERAL_BASE ((uintptr_t)0xFE000000)
#define GPIO_BASE (PERIPHERAL_BASE + 0x200000)
#define GPEDS0 (GPIO_BASE + 0x40)
#define GPLEV0 (GPIO_BASE + 0x34)

extern volatile uint32_t joystick_irq_count;

static int joystick_task_id = -1;

void joystick_register_task_id(int id)
{
    joystick_task_id = id;
}

int joystick_get_task_id(void)
{
    return joystick_task_id;
}

void joystick_task(void)
{
    uint8_t value = 0xFF;
    uint8_t last = 0xFF;

    i2c_init();

    while (1)
    {
        if (i2c_read_reg8(0x46, 0xF2, &value) < 0)
        {
            uart_puts("read 0x46:0xF2 failed\n");
        }
        else if (value != last)
        {
            uart_puts("joy raw = 0x");
            uart_put_hex8(value);
            uart_puts("\n");
            last = value;
        }

        task_sleep(10);
    }
}