.section .text
.global vectors

/* Size of one saved register frame */
#define S_FRAME_SIZE 256

/* Save general-purpose registers on exception entry */
.macro kernel_entry
    sub sp, sp, #S_FRAME_SIZE

    stp x0,  x1,  [sp, #16 * 0]
    stp x2,  x3,  [sp, #16 * 1]
    stp x4,  x5,  [sp, #16 * 2]
    stp x6,  x7,  [sp, #16 * 3]
    stp x8,  x9,  [sp, #16 * 4]
    stp x10, x11, [sp, #16 * 5]
    stp x12, x13, [sp, #16 * 6]
    stp x14, x15, [sp, #16 * 7]
    stp x16, x17, [sp, #16 * 8]
    stp x18, x19, [sp, #16 * 9]
    stp x20, x21, [sp, #16 * 10]
    stp x22, x23, [sp, #16 * 11]
    stp x24, x25, [sp, #16 * 12]
    stp x26, x27, [sp, #16 * 13]
    stp x28, x29, [sp, #16 * 14]
    str x30,      [sp, #16 * 15]
.endm

/* Restore registers and return from exception */
.macro kernel_exit
    ldp x0,  x1,  [sp, #16 * 0]
    ldp x2,  x3,  [sp, #16 * 1]
    ldp x4,  x5,  [sp, #16 * 2]
    ldp x6,  x7,  [sp, #16 * 3]
    ldp x8,  x9,  [sp, #16 * 4]
    ldp x10, x11, [sp, #16 * 5]
    ldp x12, x13, [sp, #16 * 6]
    ldp x14, x15, [sp, #16 * 7]
    ldp x16, x17, [sp, #16 * 8]
    ldp x18, x19, [sp, #16 * 9]
    ldp x20, x21, [sp, #16 * 10]
    ldp x22, x23, [sp, #16 * 11]
    ldp x24, x25, [sp, #16 * 12]
    ldp x26, x27, [sp, #16 * 13]
    ldp x28, x29, [sp, #16 * 14]
    ldr x30,      [sp, #16 * 15]

    add sp, sp, #S_FRAME_SIZE
    eret
.endm

/* One vector entry occupies 128 bytes */
.macro ventry label
    .align 7
    b \label
.endm

/* Exception Vector Table */
.align 11
vectors:
    /* Current EL with SP_EL0 (EL1t) */
    ventry unexpected_exception   /* synchronous */
    ventry handle_el1_irq         /* IRQ */
    ventry unexpected_exception   /* FIQ */
    ventry unexpected_exception   /* SError */

    /* Current EL with SP_EL1 (EL1h) */
    ventry unexpected_exception   /* synchronous */
    ventry handle_el1_irq         /* IRQ */
    ventry unexpected_exception   /* FIQ */
    ventry unexpected_exception   /* SError */

    /* Lower EL using AArch64 */
    ventry unexpected_exception   /* synchronous */
    ventry unexpected_exception   /* IRQ */
    ventry unexpected_exception   /* FIQ */
    ventry unexpected_exception   /* SError */

    /* Lower EL using AArch32 */
    ventry unexpected_exception   /* synchronous */
    ventry unexpected_exception   /* IRQ */
    ventry unexpected_exception   /* FIQ */
    ventry unexpected_exception   /* SError */

/* Common IRQ entry */
handle_el1_irq:
    kernel_entry
    bl handle_irq
    kernel_exit

/* Fallback for all unexpected exceptions */
unexpected_exception:
    kernel_entry
    bl exception_debug
1:
    b 1b