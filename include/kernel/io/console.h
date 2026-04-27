#ifndef KERNEL_IO_CONSOLE_H
#define KERNEL_IO_CONSOLE_H

void console_init(void);
void console_putc(char c);
void console_puts(const char *s);

#endif