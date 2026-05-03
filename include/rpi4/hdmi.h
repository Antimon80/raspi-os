#ifndef RPI4_HDMI_H
#define RPI4_HDMI_H

#include <stdint.h>

int hdmi_init(void);
void hdmi_putc(char c);
void hdmi_puts(const char *s);
void hdmi_set_text_colors(uint32_t fg, uint32_t bg);
void hdmi_reset_text_colors(void);
void hdmi_set_cursor(uint32_t column, uint32_t row);
void hdmi_show_bootscreen(void);
void hdmi_clear_console(void);
void hdmi_wait_ms(uint32_t ms);
int hdmi_acquire(int task_id);
void hdmi_release(int task_id);
int hdmi_is_owned_by(int task_id);
int hdmi_is_available(void);

#endif
