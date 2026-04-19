#include "kernel/debug/panic.h"
#include "rpi4/uart.h"
#include "kernel/irq.h"

/*
 * Halt the system permanently.
 *
 * Disables interrupts to prevent any further asynchronous activity
 * and then enters a low-power wait loop. The CPU will remain here
 * indefinitely.
 */
void kernel_halt(void)
{
    irq_disable();

    while (1)
    {
        asm volatile("wfe");
    }
}

/*
 * Kernel panic handler.
 *
 * Prints an error message to the UART and then halts the system.
 * This function is used for unrecoverable errors where continuing
 * execution would lead to undefined behavior.
 */
void kernel_panic(const char *msg)
{
    uart_puts("KERNEL PANIC: ");
    uart_puts(msg);
    uart_puts("\n");

    kernel_halt();
}