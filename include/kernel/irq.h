#ifndef KERNEL_IRQ_H
#define KERNEL_IRQ_H

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
 * Central IRQ handler called from the exception vector.
 */
void handle_irq(void);


void exception_debug(void);

#endif