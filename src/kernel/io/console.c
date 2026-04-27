#include "kernel/io/console.h"
#include "kernel/io/hdmi_console.h"
#include "rpi4/uart.h"
#include "util/convert.h"

/*
 * Shared kernel console output path.
 *
 * UART remains the primary, immediate backend.
 * HDMI is only fed through its asynchronous queue.
 *
 */
void console_init(void)
{
    hdmi_console_init();
}

void console_putc(char c)
{
    if (c == '\n')
    {
        uart_putc('\r');
        uart_putc('\n');

        hdmi_console_enqueue('\n');
        return;
    }

    uart_putc(c);
    hdmi_console_enqueue(c);
}

void console_puts(const char *s)
{
    if (!s)
    {
        return;
    }

    uart_puts(s);

    while (*s)
    {
        hdmi_console_enqueue(*s++);
    }
}

void console_put_uint(unsigned int value)
{
    char buffer[16];
    int i = 0;

    if (value == 0)
    {
        console_putc('0');
        return;
    }

    while (value > 0)
    {
        buffer[i++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (i > 0)
    {
        console_putc(buffer[--i]);
    }
}

void console_put_u64(uint64_t value)
{
    char buffer[32];

    utoa_dec(value, buffer, sizeof(buffer));
    console_puts(buffer);
}

void console_put_hex_uintptr(uintptr_t value)
{
    char buffer[32];

    utoa_hex((uint64_t)value, buffer, sizeof(buffer));

    console_puts("0x");
    console_puts(buffer);
}

void console_put_hex8(uint8_t value)
{
    const char *hex = "0123456789ABCDEF";

    console_puts("0x");
    console_putc(hex[(value >> 4) & 0xF]);
    console_putc(hex[value & 0xF]);
}