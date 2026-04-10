#ifndef UART_H
#define UART_H

#include <stdint.h>

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);
int uart_read_char(char *c);
void uart_handle_irq(void);

#endif