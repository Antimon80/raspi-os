#ifndef RPI4_HDMI_H
#define RPI4_HDMI_H

#include <stdint.h>

int hdmi_init(void);
void hdmi_putc(char c);
void hdmi_puts(const char *s);
void hdmi_show_bootscreen(void);
void hdmi_clear_console(void);
void hdmi_wait_ms(uint32_t ms);

#endif
