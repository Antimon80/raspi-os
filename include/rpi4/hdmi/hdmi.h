#ifndef RPI4_HDMI_HDMI_H
#define RPI4_HDMI_HDMI_H

#include <stdint.h>

/* Maximum size of the logical HDMI text model */
#define HDMI_MAX_COLUMNS 80u
#define HDMI_MAX_ROWS 32u

typedef enum
{
    HDMI_PANE_MAIN = 0,
    HDMI_PANE_MENU = 1,
    HDMI_PANE_COUNT
} hdmi_pane_id_t;

typedef enum
{
    HDMI_PANE_MODE_CONSOLE = 0,
    HDMI_PANE_MODE_APP = 1
} hdmi_pane_mode_t;

/*
 * One logical HDMI text cell.
 *
 * The text model stores the desired cell state. drawn_cells stores the last
 * state that was flushed to the framebuffer. The dirty flag marks cells that
 * must be repainted by hdmi_present().
 */
typedef struct
{
    char ch;
    uint32_t fg;
    uint32_t bg;
    uint8_t dirty;
} hdmi_cell_t;

/*
 * Runtime state of one HDMI pane.
 *
 * Each pane owns its own frame geometry, text area, cursor state, color state,
 * ANSI parser state, ownership state and logical/drawn text model.
 */
typedef struct
{
    uint32_t frame_x;
    uint32_t frame_y;
    uint32_t frame_w;
    uint32_t frame_h;

    uint32_t content_x;
    uint32_t content_y;
    uint32_t content_w;
    uint32_t content_h;

    uint32_t columns;
    uint32_t rows;

    uint32_t cursor_x;
    uint32_t cursor_y;

    uint32_t fg;
    uint32_t bg;

    int cursor_visible;

    int ansi_state;
    uint32_t ansi_value;
    int ansi_has_value;

    int owner_task_id;
    hdmi_pane_mode_t mode;

    const char *title;

    hdmi_cell_t cells[HDMI_MAX_ROWS][HDMI_MAX_COLUMNS];
    hdmi_cell_t draw[HDMI_MAX_ROWS][HDMI_MAX_COLUMNS];
} hdmi_pane_t;

int hdmi_init(void);
int hdmi_is_available(void);

int hdmi_acquire_pane(hdmi_pane_id_t pane_id, int task_id);
void hdmi_release_pane(hdmi_pane_id_t pane_id, int task_id);
int hdmi_pane_is_console_writable(hdmi_pane_id_t pane_id);
void hdmi_set_pane_mode(hdmi_pane_id_t pane_id, hdmi_pane_mode_t mode);
hdmi_pane_mode_t hdmi_get_pane_mode(hdmi_pane_id_t pane_id);
uint32_t hdmi_get_pane_columns(hdmi_pane_id_t pane_id);
uint32_t hdmi_get_pane_rows(hdmi_pane_id_t pane_id);

void hdmi_set_text_colors(hdmi_pane_id_t pane_id, uint32_t fg, uint32_t bg);
void hdmi_reset_text_colors(hdmi_pane_id_t pane_id);

void hdmi_set_cursor(hdmi_pane_id_t pane_id, uint32_t column, uint32_t row);
void hdmi_putc(hdmi_pane_id_t pane_id, char c);
void hdmi_puts(hdmi_pane_id_t pane_id, const char *s);
void hdmi_clear_pane(hdmi_pane_id_t pane_id);
void hdmi_reset_panes(void);

void hdmi_pad_current_line(hdmi_pane_id_t pane_id, uint32_t fg, uint32_t bg, unsigned int used);
void hdmi_write_line(hdmi_pane_id_t pane_id, uint32_t row, uint32_t fg, uint32_t bg, const char *s);
void hdmi_clear_lines_from(hdmi_pane_id_t pane_id, uint32_t row, uint32_t fg, uint32_t bg);

void hdmi_show_bootscreen(void);
hdmi_pane_t *hdmi_get_pane(hdmi_pane_id_t pane_id);
int hdmi_present(uint32_t max_cells);
int hdmi_present_pane(hdmi_pane_t *pane, uint32_t *rendered, uint32_t max_cells);
void hdmi_wait_ms(uint32_t ms);

#endif
