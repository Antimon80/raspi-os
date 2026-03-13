#include "rpi4/uart.h"

void main(void) {
    uart_init();
    uart_puts("Boot OK\n");
    uart_puts("UART OK\n");

    while (1) {
    }
}
