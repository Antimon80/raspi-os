#ifndef RPI4_HDMI_H
#define RPI4_HDMI_H

#include <stdint.h>

int hdmi_init(void);
int hdmi_is_available(void);

int hdmi_acquire(int task_id);
void hdmi_release(int task_id);

void hdmi_set_text_colors(uint32_t fg, uint32_t bg);
void hdmi_reset_text_colors(void);
void hdmi_set_cursor(uint32_t column, uint32_t row);

void hdmi_putc(char c);
void hdmi_puts(const char *s);

void hdmi_show_bootscreen(void);
void hdmi_clear_console(void);
void hdmi_reset_console(void);
int hdmi_flush_dirty(uint32_t max_cells);

void hdmi_wait_ms(uint32_t ms);


#endif
