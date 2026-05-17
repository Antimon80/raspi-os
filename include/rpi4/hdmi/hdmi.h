#ifndef RPI4_HDMI_HDMI_H
#define RPI4_HDMI_HDMI_H

#include <stdint.h>

typedef enum {
    HDMI_PANE_MAIN = 0,
    HDMI_PANE_MENU = 1,
    HDMI_PANE_COUNT
} hdmi_pane_id_t;

typedef enum {
    HDMI_PANE_MODE_CONSOLE = 0,
    HDMI_PANE_MODE_APP = 1
} hdmi_pane_mode_t;

int hdmi_init(void);
int hdmi_is_available(void);

int hdmi_acquire_pane(hdmi_pane_id_t pane_id, int task_id);
void hdmi_release_pane(hdmi_pane_id_t pane_id, int task_id);
void hdmi_pane_is_console_writable(hdmi_pane_id_t pane_id);
void hdmi_set_pane_mode(hdmi_pane_id_t pane_id, hdmi_pane_mode_t mode);
hdmi_pane_mode_t hdmi_get_pane_mode(hdmi_pane_id_t pane_id);

void hdmi_set_text_colors(hdmi_pane_id_t pane_id, uint32_t fg, uint32_t bg);
void hdmi_reset_text_colors(hdmi_pane_id_t pane_id);

void hdmi_set_cursor(hdmi_pane_id_t pane_id, uint32_t column, uint32_t row);
void hdmi_putc(hdmi_pane_id_t pane_id, char c);
void hdmi_puts(hdmi_pane_id_t pane_id, const char *s);
void hdmi_clear_pane(hdmi_pane_id_t pane_id);
void hdmi_reset_panes(void);

void hdmi_show_bootscreen(void);
int hdmi_present(uint32_t max_cells);
void hdmi_wait_ms(uint32_t ms);


#endif
