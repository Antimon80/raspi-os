#include "kernel/tasks/demo_task.h"
#include "rpi4/uart.h"

/*
 * Simple demo task.
 * Periodically prints a message so that task switching becomes visible
 * on the UART output.
 */
void demo_task(void)
{
    while (1)
    {
        uart_puts("[demo]\n");

        for (volatile unsigned long i = 0; i < 1000000UL; i++)
        {
        }

        scheduler_yield();
    }
}