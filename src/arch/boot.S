.section ".text.boot"

.global _start

/* Constants for EL transition */
.equ SCTLR_VALUE_MMU_DISABLED, 0x30D00800
.equ HCR_VALUE,                (1 << 31)
.equ SCR_VALUE,                ((1 << 10) | (1 << 8) | (1 << 0))
.equ SPSR_MASKED_EL1H,         0x3C5

_start:
    /* Only core 0 continues, park the others */
    mrs     x1, mpidr_el1
    and     x1, x1, #3
    cbz     x1, 2f
1:
    wfe
    b       1b

2:
    /* Temporary stack */
    ldr     x1, =__stack_top
    mov     sp, x1

    /* Determine current exception level */
    mrs     x0, CurrentEL
    lsr     x0, x0, #2

    cmp     x0, #3
    b.eq    from_el3

    cmp     x0, #2
    b.eq    from_el2

    cmp     x0, #1
    b.eq    el1_entry

    /* Unsupported start level: hang */
hang:
    wfe
    b       hang

from_el3:
    /* Prepare EL1 state */
    ldr     x0, =SCTLR_VALUE_MMU_DISABLED
    msr     sctlr_el1, x0

    /* EL1 executes in AArch64 */
    ldr     x0, =HCR_VALUE
    msr     hcr_el2, x0

    /* Non-secure, next lower EL in AArch64 */
    ldr     x0, =SCR_VALUE
    msr     scr_el3, x0

    /* Return to EL1h with DAIF masked */
    ldr     x0, =SPSR_MASKED_EL1H
    msr     spsr_el3, x0

    adr     x0, el1_entry
    msr     elr_el3, x0

    eret

from_el2:
    /* Prepare EL1 state */
    ldr     x0, =SCTLR_VALUE_MMU_DISABLED
    msr     sctlr_el1, x0

    /* EL1 executes in AArch64 */
    ldr     x0, =HCR_VALUE
    msr     hcr_el2, x0

    /* Return to EL1h with DAIF masked */
    ldr     x0, =SPSR_MASKED_EL1H
    msr     spsr_el2, x0

    adr     x0, el1_entry
    msr     elr_el2, x0

    eret

el1_entry:
    /* Establish kernel stack in EL1 */
    ldr     x1, =__stack_top
    mov     sp, x1

    /* Clear .bss
       __bss_size is the number of 8-byte words (qwords) to clear.
     */
    ldr     x1, =__bss_start
    ldr     w2, =__bss_size
3:
    cbz     w2, 4f
    str     xzr, [x1], #8
    sub     w2, w2, #1
    b       3b

4:
    /* Call C entry point */
    bl      main

    /* main must not return */
    b       hang