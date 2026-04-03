#ifndef IRQ_H
#define IRQ_H

#include <stdint.h>

/*
 * Public IRQ control API
 * Used by kernel_main() and other kernel components.
 */
void irq_init(void);
void irq_enable(void);
void irq_disable(void);
void irq_barrier(void);

/* GIC setup for BCM2711 */
void gic_init(void);

/*
 * Public UART IRQ buffer API
 * Used by the main loop / later by tasks.
 */
int uart_read_char(char *c);

/*
 * Internal IRQ entry points
 */
void handle_irq(void);
void exception_debug(void);

#endif