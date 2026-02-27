/* src/arch/boot.S */
.section ".text.boot"

.global _start

_start:
    /* Only core 0 continues, park the others */
    mrs     x1, mpidr_el1
    and     x1, x1, #3
    cbz     x1, 2f
1:  wfe
    b       1b

2:
    /* Set up stack (defined in link.ld) */
    ldr     x1, =__stack_top
    mov     sp, x1

    /* Clear .bss
       __bss_size is the number of 8-byte words (qwords) to clear.
     */
    ldr     x1, =__bss_start
    ldr     w2, =__bss_size
3:  cbz     w2, 4f
    str     xzr, [x1], #8
    sub     w2, w2, #1
    b       3b

4:
    /* Call C entry point (must not return) */
    bl      main

    /* If main returns, park core 0 as well */
    b       1b