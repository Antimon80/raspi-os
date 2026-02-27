/* link.ld */
SECTIONS
{
    /* Raspberry Pi 4 (AArch64) kernel load address */
    . = 0x80000;

    /* Boot code must be first */
    .text : {
        KEEP(*(.text.boot))
        *(.text .text.* .gnu.linkonce.t*)
    }

    .rodata : {
        *(.rodata .rodata.* .gnu.linkonce.r*)
    }

    /* Data */
    PROVIDE(_data = .);
    .data : {
        *(.data .data.* .gnu.linkonce.d*)
    }

    /* BSS (zeroed by boot.S) */
    .bss (NOLOAD) : {
        . = ALIGN(16);
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    }

    /* Simple stack directly after BSS (16 KiB). Stack grows downwards. */
    . = ALIGN(16);
    __stack_top = . + 0x4000;

    _end = .;

    /DISCARD/ : {
        *(.comment)
        *(.gnu*)
        *(.note*)
        *(.eh_frame*)
    }
}

/* Number of 8-byte words in BSS (matches the boot.S loop) */
__bss_size = (__bss_end - __bss_start) >> 3;