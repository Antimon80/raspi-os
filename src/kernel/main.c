#include "rpi4/uart.h"
#include "rpi4/mmio.h"
#include "kernel/irq.h"

#define PERIPHERAL_BASE ((uintptr_t)0xFE000000)
#define IRQ_ENABLE1 (PERIPHERAL_BASE + 0xB210)

void main(void)
{
    uart_init();
    uart_puts("Boot OK\n");
    uart_puts("UART OK\n");

    irq_init();
    mmio_write(IRQ_ENABLE1, (1 << 29));
    irq_enable();

    uart_puts("IRQ ready - type something\n");

    while (1)
    {
        char c;
        if (uart_read_char(&c))
        {
            uart_putc(c);
        }
    }
}
