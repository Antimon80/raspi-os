.section .text
.global context_switch

/*
 * void context_switch(uint64_t **old_sp, uint64_t *new_sp)
 *
 * x0 = &old_sp
 * x1 = new_sp
 */
context_switch:
    /* Save callee-saved registers of the currently running task. */
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    stp x27, x28, [sp, #-16]!
    stp x29, x30, [sp, #-16]!

    /* Store the resulting stack pointer into *old_sp. */
    str sp, [x0]

    /* Switch to the new task stack. */
    mov sp, x1

    /* Restore callee-saved registers of the next task. */
    ldp x29, x30, [sp], #16
    ldp x27, x28, [sp], #16
    ldp x25, x26, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16

    ret