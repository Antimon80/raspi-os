#ifndef RPI4_DRIVERS_UART_H
#define RPI4_DRIVERS_UART_H

#include <stdint.h>

void uart_init(void);
void uart_init_tx_lock(void);

void uart_set_rx_task(int task_id);
int uart_get_rx_task(void);
void uart_flush_rx(void);

void uart_putc(char c);
void uart_puts(const char *s);
void uart_put_uint(unsigned int value);
void uart_put_u64(uint64_t value);
void uart_put_hex_uintptr(uintptr_t value);
void uart_put_hex8(uint8_t value);

int uart_read_char(char *c);
int uart_try_read_char(char *c);

void uart_handle_irq(void);

#endif
