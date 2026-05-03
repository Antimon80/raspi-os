#ifndef KERNEL_IO_CONSOLE_H
#define KERNEL_IO_CONSOLE_H

#include <stdint.h>

void console_init(void);
void console_putc(char c);
void console_puts(const char *s);
void console_put_uint(unsigned int value);
void console_put_int(int value);
void console_put_u64(uint64_t value);
void console_put_hex_uintptr(uintptr_t value);
void console_put_hex8(uint8_t value);

#endif