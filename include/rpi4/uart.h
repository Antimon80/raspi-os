#ifndef RPI4_UART_H
#define RPI4_UART_H

#include <stdint.h>

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);
void uart_put_uint(unsigned int value);
void uart_put_u64(uint64_t value);
void uart_put_hex_uintptr(uintptr_t value);
int uart_read_char(char *c);
void uart_handle_irq(void);
void uart_set_rx_task(int task_id);
int uart_read_char_blocking(char *c);

#endif