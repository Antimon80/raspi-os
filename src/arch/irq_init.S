.section .text

.global irq_init
irq_init:
    adr x0, vectors
    msr vbar_el1, x0
    ret

.global irq_enable
irq_enable:
    msr daifclr, #2   // enable IRQ
    ret

.global irq_disable
irq_disable:
    msr daifset, #2   // disable IRQ
    ret

.global irq_barrier
irq_barrier:
    dsb sy
    isb
    ret