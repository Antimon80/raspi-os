#include "kernel/io/console.h"
#include "kernel/io/hdmi_console.h"
#include "rpi4/uart.h"

/*
 * Shared kernel console output path.
 *
 * UART remains the primary, immediate backend.
 * HDMI is only fed through its asynchronous queue.
 *
 * Nothing in the UART driver knows about HDMI.
 */
void console_init(void)
{
    hdmi_console_init();
}

void console_putc(char c)
{
    uart_putc(c);
    hdmi_console_enqueue(c);
}

void console_puts(const char *s)
{
    if (!s)
    {
        return;
    }

    while (*s)
    {
        console_putc(*s++);
    }
}