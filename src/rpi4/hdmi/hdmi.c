#include "rpi4/hdmi/hdmi.h"
#include "rpi4/hdmi/hdmi_draw.h"
#include "rpi4/soc/mmio.h"
#include "kernel/irq.h"

/*
 * HDMI output is implemented via the firmware mailbox property channel.
 * The firmware allocates a framebuffer, and this module draws a minimal
 * bootscreen plus a simple text console into that framebuffer.
 */

/* Base address of the peripheral MMIO region on Raspberry Pi 4 (BCM2711). */
#define PERIPHERAL_BASE ((uintptr_t)0xFE000000)

/* Mailbox controller register block. */
#define MAILBOX_BASE (PERIPHERAL_BASE + 0xB880)
#define MAILBOX_READ (MAILBOX_BASE + 0x00)
#define MAILBOX_STATUS (MAILBOX_BASE + 0x18)
#define MAILBOX_WRITE (MAILBOX_BASE + 0x20)

/* Mailbox status and response flags. */
#define MAILBOX_STATUS_FULL 0x80000000u
#define MAILBOX_STATUS_EMPTY 0x40000000u
#define MAILBOX_RESPONSE_OK 0x80000000u
#define MAILBOX_TAG_RESPONSE 0x80000000u
#define MAILBOX_CHANNEL_PROPERTY 8u

/* Mailbox property tags used to configure the framebuffer. */
#define TAG_SET_PHYSICAL_SIZE 0x00048003u
#define TAG_SET_VIRTUAL_SIZE 0x00048004u
#define TAG_SET_VIRTUAL_OFFSET 0x00048009u
#define TAG_SET_DEPTH 0x00048005u
#define TAG_SET_PIXEL_ORDER 0x00048006u
#define TAG_ALLOCATE_BUFFER 0x00040001u
#define TAG_GET_PITCH 0x00040008u

/* Requested framebuffer format. */
#define SCREEN_WIDTH 1280u
#define SCREEN_HEIGHT 720u
#define SCREEN_DEPTH 32u
#define PIXEL_ORDER_RGB 1u

/* Maximum size of the logical HDMI text model */
#define HDMI_MAX_COLUMNS 80u
#define HDMI_MAX_ROWS 32u

/* Shared color palette for the console chrome, text renderer and bootscreen. */
#define CONSOLE_BG 0x00101820u
#define CONSOLE_FG 0x00F2F6F8u
#define CONSOLE_SHADOW 0x003A5366u
#define CONSOLE_PANEL 0x00161F2Au
#define CONSOLE_PANEL_ALT 0x001B2633u
#define CONSOLE_BORDER 0x002C3E50u
#define CONSOLE_ACCENT 0x0028C7FAu
#define CONSOLE_MUTED 0x007D93A6u
#define BOOT_BG 0x00060B16u
#define BOOT_ACCENT 0x001D7FDBu
#define BOOT_MUTED 0x0087A3B7u
#define BOOT_CARD 0x000E1623u
#define BOOT_CARD_EDGE 0x001B2A3Eu
#define BOOT_GLOW 0x000C3D69u

/* Fixed HDMI pane layout. */
#define HDMI_MARGIN_X 10u
#define HDMI_MARGIN_Y 10u
#define HDMI_PANEL_GAP 10u
#define HDMI_HEADER_H 42u
#define HDMI_PADDING_X 20u
#define HDMI_CONTENT_PADDING_TOP 18u
#define HDMI_CONTENT_PADDING_BOTTOM 18u
#define HDMI_MENU_WIDTH 360u

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

/*
 * Mailbox property messages must be 16-byte aligned.
 * The current framebuffer setup uses 35 32-bit words.
 */
static volatile uint32_t mailbox[36] __attribute__((aligned(16)));

/* Framebuffer and console state. */
static uint32_t *framebuffer = 0;
static uint32_t framebuffer_width = 0;
static uint32_t framebuffer_height = 0;
static uint32_t framebuffer_pitch = 0;

static uint32_t status_dot_x = 0;
static uint32_t status_dot_y = 0;

static hdmi_pane_t panes[HDMI_PANE_COUNT];

static int hdmi_ready = 0;

/*
 * Return the pane state for a pane ID, or 0 for invalid IDs.
 */
static hdmi_pane_t *hdmi_get_pane(hdmi_pane_id_t pane_id)
{
    if (pane_id < 0 || pane_id >= HDMI_PANE_COUNT)
    {
        return 0;
    }

    return &panes[pane_id];
}

/*
 * Mark one logical cell dirty.
 */
static void hdmi_mark_cell_dirty(hdmi_pane_t *pane, uint32_t column, uint32_t row)
{
    if (!pane || row >= pane->rows || column >= pane->columns)
    {
        return;
    }

    pane->cells[row][column].dirty = 1u;
}

/*
 * Mark every visible cell in a pane dirty.
 *
 * Used after clears, layout resets or full pane redraws.
 */
static void hdmi_mark_pane_dirty(hdmi_pane_t *pane)
{
    uint32_t row;
    uint32_t col;

    if (!pane)
    {
        return;
    }

    for (row = 0u; row < pane->rows; row++)
    {
        for (col = 0u; col < pane->columns; col++)
        {
            pane->cells[row][col].dirty = 1u;
        }
    }
}

/*
 * Initialize both logical and drawn-cell state.
 *
 * drawn_cells intentionally starts different from text_cells so the first flush
 * paints the complete text area.
 */
static void hdmi_text_model_init(hdmi_pane_t *pane, const char *title)
{
    uint32_t row;
    uint32_t col;

    if (!pane)
    {
        return;
    }

    pane->cursor_x = 0u;
    pane->cursor_y = 0u;
    pane->fg = CONSOLE_FG;
    pane->bg = CONSOLE_PANEL;
    pane->cursor_visible = 1;

    pane->ansi_state = 0;
    pane->ansi_value = 0u;
    pane->ansi_has_value = 0;

    pane->owner_task_id = -1;
    pane->mode = HDMI_PANE_MODE_CONSOLE;
    pane->title = title;

    for (row = 0u; row < HDMI_MAX_ROWS; row++)
    {
        for (col = 0u; col < HDMI_MAX_COLUMNS; col++)
        {
            pane->cells[row][col].ch = ' ';
            pane->cells[row][col].fg = CONSOLE_FG;
            pane->cells[row][col].bg = CONSOLE_PANEL;
            pane->cells[row][col].dirty = 1u;

            pane->draw[row][col].ch = 0;
            pane->draw[row][col].fg = 0;
            pane->draw[row][col].bg = 0;
            pane->draw[row][col].dirty = 0;
        }
    }
}

/*
 * Clear only the logical text area. The static chrome remains untouched.
 */
static void hdmi_text_model_clear(hdmi_pane_t *pane, uint32_t fg, uint32_t bg)
{
    uint32_t row;
    uint32_t col;

    if (!pane)
    {
        return;
    }

    for (row = 0u; row < pane->rows; row++)
    {
        for (col = 0u; col < pane->columns; col++)
        {
            pane->cells[row][col].ch = ' ';
            pane->cells[row][col].fg = fg;
            pane->cells[row][col].bg = bg;
            pane->cells[row][col].dirty = 1u;
        }
    }

    pane->cursor_x = 0u;
    pane->cursor_y = 0u;
    pane->cursor_visible = 1;
    pane->ansi_state = 0;
    pane->ansi_value = 0u;
    pane->ansi_has_value = 0;
}

/*
 * Configure frame and text geometry for one pane.
 *
 * The text area is derived from the pane frame and chrome padding. The visible
 * row and column count is clamped to the fixed logical text-buffer size.
 */
static void hdmi_configure_pane(hdmi_pane_t *pane, uint32_t frame_x, uint32_t frame_y, uint32_t frame_w, uint32_t frame_h, const char *title)
{
    uint32_t calculated_columns;
    uint32_t calculated_rows;

    if (!pane)
    {
        return;
    }

    pane->frame_x = frame_x;
    pane->frame_y = frame_y;
    pane->frame_w = frame_w;
    pane->frame_h = frame_h;

    pane->content_x = frame_x + HDMI_PADDING_X;
    pane->content_y = frame_y + HDMI_HEADER_H + HDMI_CONTENT_PADDING_TOP;
    pane->content_w = frame_w - (HDMI_PADDING_X * 2u);
    pane->content_h = frame_h - HDMI_HEADER_H - HDMI_CONTENT_PADDING_TOP - HDMI_CONTENT_PADDING_BOTTOM - 6u;

    calculated_columns = pane->content_w / CHAR_ADVANCE_X;
    calculated_rows = pane->content_h / CHAR_ADVANCE_Y;

    if (calculated_columns > HDMI_MAX_COLUMNS)
    {
        calculated_columns = HDMI_MAX_COLUMNS;
    }

    if (calculated_rows > HDMI_MAX_ROWS)
    {
        calculated_rows = HDMI_MAX_ROWS;
    }

    pane->columns = calculated_columns;
    pane->rows = calculated_rows;

    hdmi_text_model_init(pane, title);
}

/*
 * Set a logical cell and mark it dirty only when its visible contents changed.
 */
static void hdmi_set_cell(hdmi_pane_t *pane, uint32_t column, uint32_t row, char ch, uint32_t fg, uint32_t bg)
{
    hdmi_cell_t *cell;

    if (!pane || row >= pane->rows || column >= pane->columns)
    {
        return;
    }

    if (ch == 0)
    {
        ch = ' ';
    }

    cell = &pane->cells[row][column];

    if (cell->ch == ch && cell->fg == fg && cell->bg == bg)
    {
        return;
    }

    cell->ch = ch;
    cell->fg = fg;
    cell->bg = bg;
    cell->dirty = 1u;
}

/*
 * Scroll the logical text model by one row.
 *
 * No framebuffer pixels are copied here. The flush step will redraw dirty cells
 * in bounded slices.
 */
static void hdmi_scroll(hdmi_pane_t *pane)
{
    uint32_t row;
    uint32_t col;

    if (!pane || pane->rows == 0u || pane->columns == 0u)
    {
        return;
    }

    for (row = 1u; row < pane->rows; row++)
    {
        for (col = 0u; col < pane->columns; col++)
        {
            pane->cells[row - 1u][col] = pane->cells[row][col];
            pane->cells[row - 1u][col].dirty = 1u;
        }
    }

    for (col = 0u; col < pane->columns; col++)
    {
        pane->cells[pane->rows - 1u][col].ch = ' ';
        pane->cells[pane->rows - 1u][col].fg = pane->fg;
        pane->cells[pane->rows - 1u][col].bg = pane->bg;
        pane->cells[pane->rows - 1u][col].dirty = 1u;
    }
}

/*
 * Advance the logical cursor to the next line and scroll the text model if
 * needed.
 */
static void hdmi_newline(hdmi_pane_t *pane)
{

    if (!pane || pane->rows == 0u || pane->columns == 0u)
    {
        return;
    }

    uint32_t old_x = pane->cursor_x;
    uint32_t old_y = pane->cursor_y;

    pane->cursor_x = 0u;
    pane->cursor_y++;

    if (pane->cursor_y >= pane->rows)
    {
        hdmi_scroll(pane);
        pane->cursor_y = pane->rows - 1u;
    }

    hdmi_mark_cell_dirty(pane, old_x, old_y);
    hdmi_mark_cell_dirty(pane, pane->cursor_x, pane->cursor_y);
}

/*
 * Clear the logical current line.
 */
static void hdmi_clear_pane_line(hdmi_pane_t *pane)
{
    uint32_t col;

    if (!pane || pane->cursor_y >= pane->rows)
    {
        return;
    }

    for (col = 0u; col < pane->columns; col++)
    {
        hdmi_set_cell(pane, col, pane->cursor_y, ' ', pane->fg, pane->bg);
        hdmi_mark_cell_dirty(pane, col, pane->cursor_y);
    }
}

/*
 * Minimal CSI handling used by the shell prompt.
 *
 * Supported:
 *   ESC [ n A    cursor up
 *   ESC [ n B    cursor down
 *   ESC [ 2 K    clear entire current line
 */
static void hdmi_handle_csi(hdmi_pane_t *pane, char c)
{
    if (!pane)
    {
        return;
    }

    uint32_t count = pane->ansi_has_value ? pane->ansi_value : 1u;

    if (count == 0u)
    {
        count = 1u;
    }

    if (c == 'A')
    {
        hdmi_set_cursor(pane == &panes[HDMI_PANE_MENU] ? HDMI_PANE_MENU : HDMI_PANE_MAIN,
                        pane->cursor_x, count > pane->cursor_y ? 0u : pane->cursor_y - count);
    }
    else if (c == 'B')
    {
        uint32_t row = pane->cursor_y + count;
        if (row >= pane->rows)
        {
            row = pane->rows - 1u;
        }
        hdmi_set_cursor(pane == &panes[HDMI_PANE_MENU] ? HDMI_PANE_MENU : HDMI_PANE_MAIN,
                        pane->cursor_x, row);
    }
    else if (c == 'K' && pane->ansi_value == 2u)
    {
        hdmi_clear_pane_line(pane);
    }

    pane->ansi_state = 0;
    pane->ansi_value = 0;
    pane->ansi_has_value = 0;
}

/*
 * Draw the static part of the bootscreen.
 *
 * This function defines only the bootscreen layout. The actual framebuffer
 * drawing is delegated to hdmi_draw.c. The progress bar is updated separately
 * during the short boot animation.
 */
static void hdmi_draw_bootscreen_static(void)
{
    const char *eyebrow = "Bare metal startup";
    const char *title = "Raspi OS";
    const char *subtitle = "Booting kernel...";
    uint32_t card_x = (framebuffer_width / 2u) - 240u;
    uint32_t card_y = (framebuffer_height / 2u) - 170u;
    uint32_t card_w = 480u;
    uint32_t card_h = 320u;
    uint32_t logo_x = card_x + 162u;
    uint32_t logo_y = card_y + 42u;

    hdmi_fill_rect_blend(0u, 0u, framebuffer_width, framebuffer_height, 0x00060B16u, 0x000B1420u, 1);
    hdmi_fill_rect_blend(0u, 0u, framebuffer_width, 180u, 0x000B1D30u, BOOT_BG, 1);
    hdmi_draw_corner_glow(framebuffer_width - 160u, 0u, 160u, BOOT_GLOW);
    hdmi_draw_corner_glow(0u, framebuffer_height - 160u, 160u, 0x00092A46u);

    hdmi_draw_frame(card_x, card_y, card_w, card_h, BOOT_CARD_EDGE, BOOT_CARD);
    hdmi_fill_rect(card_x + 1u, card_y + 1u, card_w - 2u, 8u, CONSOLE_ACCENT);
    hdmi_fill_rect(card_x + 26u, card_y + 24u, 108u, 18u, 0x00111C2A);
    hdmi_draw_string_at(card_x + 32u, card_y + 20u, eyebrow, BOOT_MUTED, BOOT_CARD, CONSOLE_SHADOW);

    hdmi_draw_boot_badge(logo_x, logo_y);

    hdmi_draw_string_at((framebuffer_width - hdmi_string_width(title)) / 2u, card_y + 206u, title, CONSOLE_FG, BOOT_CARD, CONSOLE_SHADOW);
    hdmi_draw_string_at((framebuffer_width - hdmi_string_width(subtitle)) / 2u, card_y + 236u, subtitle, BOOT_MUTED, BOOT_CARD, CONSOLE_SHADOW);
}

/*
 * Update the animated progress bar shown on the bootscreen.
 */
static void hdmi_update_boot_progress(uint32_t progress)
{
    uint32_t card_x = (framebuffer_width / 2u) - 240u;
    uint32_t card_y = (framebuffer_height / 2u) - 170u;
    uint32_t bar_x = card_x + 90u;
    uint32_t bar_y = card_y + 274u;

    hdmi_draw_progress_bar(bar_x, bar_y, 300u, progress);
}

/*
 * Show the short animated HDMI bootscreen.
 *
 * Called before the scheduler is running, so a plain busy-wait is used.
 * No scheduler_yield() here – there are no runnable tasks yet.
 */
void hdmi_show_bootscreen(void)
{
    uint32_t progress;

    if (!framebuffer)
    {
        return;
    }

    hdmi_draw_bootscreen_static();

    for (progress = 0u; progress <= 100u; progress += 5u)
    {
        hdmi_update_boot_progress(progress);
        hdmi_wait_ms(90u);
    }
}

/*
 * Draw the static console chrome and calculate the text surface.
 *
 * This recreates the background, panel frame, title bar, status indicator and
 * text-area geometry. It must be called after framebuffer initialization and
 * whenever the full HDMI console layout has to be restored.
 *
 * The logical text model is reinitialized because changing the layout may
 * change the visible row and column count.
 */
static void hdmi_draw_pane_chrome(hdmi_pane_t *pane)
{
    if (!pane)
    {
        return;
    }

    hdmi_draw_frame(pane->frame_x, pane->frame_y, pane->frame_w,
                    pane->frame_h, CONSOLE_BORDER, CONSOLE_PANEL);

    hdmi_fill_rect(pane->frame_x + 1u, pane->frame_y + 1u, pane->frame_w - 2u,
                   HDMI_HEADER_H, CONSOLE_PANEL_ALT);

    hdmi_fill_rect(pane->frame_x + 1u, pane->frame_y + HDMI_HEADER_H + 1u,
                   pane->frame_w - 2u, 2u, CONSOLE_ACCENT);

    hdmi_fill_rect(pane->frame_x + 18u, pane->frame_y + 13u, 10u, 10u, 0x00FF6B6Bu);
    hdmi_fill_rect(pane->frame_x + 34u, pane->frame_y + 13u, 10u, 10u, 0x00FFCE54u);
    hdmi_fill_rect(pane->frame_x + 50u, pane->frame_y + 13u, 10u, 10u, 0x0048E27Bu);

    hdmi_draw_string_at(pane->frame_x + 84u, pane->frame_y + 8u,
                        pane->title, CONSOLE_FG, CONSOLE_PANEL_ALT, CONSOLE_SHADOW);
}

/*
 * Draw the static chrome for all HDMI panes.
 *
 * This sets up the split-screen layout, configures pane geometry and draws
 * the frame, header and status indicator for each pane.
 */
static void hdmi_draw_panes_chrome(void)
{
    uint32_t screen_x = HDMI_MARGIN_X;
    uint32_t screen_y = HDMI_MARGIN_Y;
    uint32_t screen_w = framebuffer_width - (HDMI_MARGIN_X * 2u);
    uint32_t screen_h = framebuffer_height - (HDMI_MARGIN_Y * 2u);

    uint32_t menu_w = HDMI_MENU_WIDTH;
    uint32_t main_w = screen_w - HDMI_PANEL_GAP - menu_w;

    hdmi_fill_rect_blend(0u, 0u, framebuffer_width, framebuffer_height, 0x000C1520u, CONSOLE_BG, 1);

    hdmi_fill_rect_blend(0u, 0u, framebuffer_width, 120u, 0x00152131u, CONSOLE_BG, 1);

    hdmi_configure_pane(&panes[HDMI_PANE_MAIN], screen_x, screen_y,
                        main_w, screen_h, "System Output");

    hdmi_configure_pane(&panes[HDMI_PANE_MENU], screen_x + main_w + HDMI_PANEL_GAP, screen_y,
                        menu_w, screen_h, "Joystick Menu");

    hdmi_draw_pane_chrome(&panes[HDMI_PANE_MAIN]);
    hdmi_draw_pane_chrome(&panes[HDMI_PANE_MENU]);

    status_dot_x = screen_x + screen_w - 30u;
    status_dot_y = screen_y + 13u;
    hdmi_draw_status_dot(status_dot_x, status_dot_y, 0u, CONSOLE_PANEL_ALT);
}

/*
 * Submit a property mailbox request and wait for the response.
 *
 * IRQs are disabled for the duration of the mailbox transaction to prevent
 * the I2C (or any other peripheral) IRQ handler from racing against the
 * mailbox read/write sequence. The VideoCore property channel shares the
 * same interrupt controller, so an unrelated IRQ firing mid-transaction
 * can corrupt the response check.
 */
static int mailbox_call(uint8_t channel)
{
    uint32_t message = (((uint32_t)(uintptr_t)mailbox) & ~0xFu) | (uint32_t)(channel & 0xFu);

    irq_disable();

    while (mmio_read(MAILBOX_STATUS) & MAILBOX_STATUS_FULL)
    {
    }

    mmio_write(MAILBOX_WRITE, message);

    while (1)
    {
        while (mmio_read(MAILBOX_STATUS) & MAILBOX_STATUS_EMPTY)
        {
        }

        if (mmio_read(MAILBOX_READ) == message)
        {
            irq_enable();
            return mailbox[1] == MAILBOX_RESPONSE_OK;
        }
    }
}

/*
 * Flush dirty cells for one pane.
 *
 * At most max_cells cells are rendered across all panes in one hdmi_present()
 * call. This keeps HDMI rendering bounded so other cooperative tasks can run.
 *
 * Returns 1 if this pane still has dirty cells after the budget was exhausted.
 */
static int hdmi_present_pane(hdmi_pane_t *pane, uint32_t *rendered, uint32_t max_cells)
{
    uint32_t row;
    uint32_t col;
    int more_dirty = 0;

    if (!pane || pane->columns == 0u || pane->rows == 0u)
    {
        return 0;
    }

    for (row = 0u; row < pane->rows; row++)
    {
        for (col = 0u; col < pane->columns; col++)
        {
            hdmi_cell_t *cell = &pane->cells[row][col];
            hdmi_cell_t *drawn = &pane->draw[row][col];
            int is_cursor = pane->cursor_visible && row == pane->cursor_y && col == pane->cursor_x;

            if (!cell->dirty && cell->ch == drawn->ch && cell->fg == drawn->fg && cell->bg == drawn->bg)
            {
                continue;
            }

            if (*rendered >= max_cells)
            {
                more_dirty = 1;
                continue;
            }

            {
                uint32_t px = pane->content_x + (col * CHAR_ADVANCE_X);
                uint32_t py = pane->content_y + (row * CHAR_ADVANCE_Y);

                hdmi_draw_char_at(px, py, cell->ch, cell->fg, cell->bg, CONSOLE_SHADOW);

                if (is_cursor)
                {
                    hdmi_draw_cursor_at(px, py, CONSOLE_ACCENT);
                }
            }

            drawn->ch = cell->ch;
            drawn->fg = cell->fg;
            drawn->bg = cell->bg;
            drawn->dirty = 0u;

            cell->dirty = 0u;

            (*rendered)++;
        }
    }

    return more_dirty;
}

/*
 * Initialize the HDMI framebuffer via the firmware mailbox interface.
 *
 * Steps:
 *  - request a 1280x720 framebuffer in 32-bit RGB format
 *  - allocate the framebuffer through the firmware
 *  - query the framebuffer pitch
 *  - attach hdmi_draw.c to the allocated framebuffer
 *  - draw the initial console chrome and flush the text model once
 *
 * Returns 1 on success and 0 on failure.
 */
int hdmi_init(void)
{
    mailbox[0] = 35u * sizeof(uint32_t);
    mailbox[1] = 0u;

    mailbox[2] = TAG_SET_PHYSICAL_SIZE;
    mailbox[3] = 8u;
    mailbox[4] = 8u;
    mailbox[5] = SCREEN_WIDTH;
    mailbox[6] = SCREEN_HEIGHT;

    mailbox[7] = TAG_SET_VIRTUAL_SIZE;
    mailbox[8] = 8u;
    mailbox[9] = 8u;
    mailbox[10] = SCREEN_WIDTH;
    mailbox[11] = SCREEN_HEIGHT;

    mailbox[12] = TAG_SET_VIRTUAL_OFFSET;
    mailbox[13] = 8u;
    mailbox[14] = 8u;
    mailbox[15] = 0u;
    mailbox[16] = 0u;

    mailbox[17] = TAG_SET_DEPTH;
    mailbox[18] = 4u;
    mailbox[19] = 4u;
    mailbox[20] = SCREEN_DEPTH;

    mailbox[21] = TAG_SET_PIXEL_ORDER;
    mailbox[22] = 4u;
    mailbox[23] = 4u;
    mailbox[24] = PIXEL_ORDER_RGB;

    mailbox[25] = TAG_ALLOCATE_BUFFER;
    mailbox[26] = 8u;
    mailbox[27] = 4u;
    mailbox[28] = 16u;
    mailbox[29] = 0u;

    mailbox[30] = TAG_GET_PITCH;
    mailbox[31] = 4u;
    mailbox[32] = 4u;
    mailbox[33] = 0u;

    mailbox[34] = 0u;

    if (!mailbox_call(MAILBOX_CHANNEL_PROPERTY))
    {
        return 0;
    }

    if (!(mailbox[19] & MAILBOX_TAG_RESPONSE) ||
        !(mailbox[23] & MAILBOX_TAG_RESPONSE) ||
        !(mailbox[27] & MAILBOX_TAG_RESPONSE) ||
        !(mailbox[32] & MAILBOX_TAG_RESPONSE))
    {
        return 0;
    }

    if (mailbox[28] == 0u || mailbox[33] == 0u)
    {
        return 0;
    }

    framebuffer = (uint32_t *)(uintptr_t)(mailbox[28] & 0x3FFFFFFFu);
    framebuffer_width = mailbox[10];
    framebuffer_height = mailbox[11];
    framebuffer_pitch = mailbox[33];
    hdmi_draw_set_framebuffer(framebuffer, framebuffer_width, framebuffer_height, framebuffer_pitch);

    hdmi_ready = 1;

    hdmi_draw_panes_chrome();
    hdmi_present(HDMI_MAX_COLUMNS * HDMI_MAX_ROWS * HDMI_PANE_COUNT);
    return 1;
}

/*
 * Return whether HDMI output is ready for use.
 *
 * HDMI is considered available only after the firmware framebuffer was
 * allocated successfully and the draw module has been attached to it.
 */
int hdmi_is_available(void)
{
    return hdmi_ready && framebuffer != 0;
}

/*
 * Acquire direct HDMI ownership for a task.
 *
 * A task may acquire HDMI if no other direct owner exists, or if it already is
 * the current owner. Returns 0 on success and -1 if HDMI is unavailable or
 * owned by another task.
 */
int hdmi_acquire_pane(hdmi_pane_id_t pane_id, int task_id)
{
    hdmi_pane_t *pane = hdmi_get_pane(pane_id);
    int result = -1;

    if (!pane || task_id < 0 || !hdmi_is_available())
    {
        return -1;
    }

    irq_disable();

    if (pane->owner_task_id < 0 || pane->owner_task_id == task_id)
    {
        pane->owner_task_id = task_id;
        pane->mode = HDMI_PANE_MODE_APP;
        result = 0;
    }

    irq_enable();
    return result;
}

/*
 * Release direct HDMI ownership for a task.
 *
 * Releasing a task that does not own HDMI has no effect.
 */
void hdmi_release_pane(hdmi_pane_id_t pane_id, int task_id)
{
    hdmi_pane_t *pane = hdmi_get_pane(pane_id);

    if (!pane || task_id < 0)
    {
        return;
    }

    irq_disable();

    if (pane->owner_task_id == task_id)
    {
        pane->owner_task_id = -1;
        pane->mode = HDMI_PANE_MODE_CONSOLE;
    }

    irq_enable();
}

/*
 * Return whether the pane can currently receive mirrored console output.
 *
 * Console output is allowed only while the pane is in console mode and has no
 * direct task owner.
 */
int hdmi_pane_is_console_writable(hdmi_pane_id_t pane_id)
{
    hdmi_pane_t *pane = hdmi_get_pane(pane_id);
    int writable;

    if (!pane)
    {
        return 0;
    }

    irq_disable();

    writable = pane->mode == HDMI_PANE_MODE_CONSOLE && pane->owner_task_id < 0;

    irq_enable();

    return writable;
}

/*
 * Set the current operating mode of a pane.
 *
 * The mode controls whether the pane behaves as a console target or is used
 * directly by an application/task.
 */
void hdmi_set_pane_mode(hdmi_pane_id_t pane_id, hdmi_pane_mode_t mode)
{
    hdmi_pane_t *pane = hdmi_get_pane(pane_id);

    if (!pane)
    {
        return;
    }

    irq_disable();
    pane->mode = mode;
    irq_enable();
}

/*
 * Return the current operating mode of a pane.
 */
hdmi_pane_mode_t hdmi_get_pane_mode(hdmi_pane_id_t pane_id)
{
    hdmi_pane_t *pane = hdmi_get_pane(pane_id);
    hdmi_pane_mode_t mode = HDMI_PANE_MODE_CONSOLE;

    if (!pane)
    {
        return HDMI_PANE_MODE_CONSOLE;
    }

    irq_disable();
    mode = pane->mode;
    irq_enable();

    return mode;
}

uint32_t hdmi_get_pane_columns(hdmi_pane_id_t pane_id)
{
    hdmi_pane_t *pane = hdmi_get_pane(pane_id);
    uint32_t columns = 0u;

    if (!pane)
    {
        return 0u;
    }

    irq_disable();
    columns = pane->columns;
    irq_enable();

    return columns;
}

uint32_t hdmi_get_pane_rows(hdmi_pane_id_t pane_id)
{
    hdmi_pane_t *pane = hdmi_get_pane(pane_id);
    uint32_t rows = 0u;

    if (!pane)
    {
        return 0u;
    }

    irq_disable();
    rows = pane->rows;
    irq_enable();

    return rows;
}

/*
 * Set the foreground and background colors used for subsequently written text
 * cells.
 *
 * Existing cells are not repainted unless their contents are changed or marked
 * dirty separately.
 */
void hdmi_set_text_colors(hdmi_pane_id_t pane_id, uint32_t fg, uint32_t bg)
{
    hdmi_pane_t *pane = hdmi_get_pane(pane_id);

    if (!pane)
    {
        return;
    }

    pane->fg = fg;
    pane->bg = bg;
}

/*
 * Restore the default HDMI console text colors.
 */
void hdmi_reset_text_colors(hdmi_pane_id_t pane_id)
{
    hdmi_pane_t *pane = hdmi_get_pane(pane_id);

    if (!pane)
    {
        return;
    }

    pane->fg = CONSOLE_FG;
    pane->bg = CONSOLE_PANEL;
}

/*
 * Move the logical cursor. The old and new cells are dirty because the cursor is
 * rendered as an overlay by hdmi_present().
 */
void hdmi_set_cursor(hdmi_pane_id_t pane_id, uint32_t column, uint32_t row)
{
    hdmi_pane_t *pane = hdmi_get_pane(pane_id);

    if (!pane || pane->columns == 0u || pane->rows == 0u)
    {
        return;
    }

    uint32_t old_x = pane->cursor_x;
    uint32_t old_y = pane->cursor_y;

    if (column >= pane->columns)
    {
        column = pane->columns - 1u;
    }

    if (row >= pane->rows)
    {
        row = pane->rows - 1u;
    }

    pane->cursor_x = column;
    pane->cursor_y = row;

    hdmi_mark_cell_dirty(pane, old_x, old_y);
    hdmi_mark_cell_dirty(pane, pane->cursor_x, pane->cursor_y);
}

/*
 * Write one character into the logical HDMI text console.
 *
 * Printable characters update text_cells and mark affected cells dirty. The
 * framebuffer is not touched here; hdmi_present() performs the actual flush.
 * Newline, carriage return, tab, backspace and a minimal CSI subset are handled
 * explicitly.
 */
void hdmi_putc(hdmi_pane_id_t pane_id, char c)
{
    hdmi_pane_t *pane = hdmi_get_pane(pane_id);

    if (!framebuffer || !pane || pane->columns == 0u || pane->rows == 0u)
    {
        return;
    }

    if (pane->ansi_state == 1)
    {
        if (c == '[')
        {
            pane->ansi_state = 2;
            pane->ansi_value = 0u;
            pane->ansi_has_value = 0;
        }
        else
        {
            pane->ansi_state = 0;
        }
        return;
    }

    if (pane->ansi_state == 2)
    {
        if (c >= '0' && c <= '9')
        {
            pane->ansi_value = (pane->ansi_value * 10u) + (uint32_t)(c - '0');
            pane->ansi_has_value = 1;
            return;
        }

        hdmi_handle_csi(pane, c);
        return;
    }

    if (c == '\x1b')
    {
        pane->ansi_state = 1;
        return;
    }

    if (c == '\r')
    {
        hdmi_set_cursor(pane_id, 0u, pane->cursor_y);
        return;
    }

    if (c == '\n')
    {
        hdmi_newline(pane);
        return;
    }

    if (c == '\b')
    {
        if (pane->cursor_x > 0u)
        {
            hdmi_set_cursor(pane_id, pane->cursor_x - 1u, pane->cursor_y);
        }

        hdmi_set_cell(pane, pane->cursor_x, pane->cursor_y, ' ', pane->fg, pane->bg);
        hdmi_mark_cell_dirty(pane, pane->cursor_x, pane->cursor_y);
        return;
    }

    if (c == '\t')
    {
        hdmi_putc(pane_id, ' ');
        hdmi_putc(pane_id, ' ');
        hdmi_putc(pane_id, ' ');
        hdmi_putc(pane_id, ' ');
        return;
    }

    if (pane->cursor_x >= pane->columns)
    {
        hdmi_newline(pane);
    }

    hdmi_set_cell(pane, pane->cursor_x, pane->cursor_y, c, pane->fg, pane->bg);

    pane->cursor_x++;

    if (pane->cursor_x >= pane->columns)
    {
        hdmi_newline(pane);
    }
    else
    {
        hdmi_mark_cell_dirty(pane, pane->cursor_x, pane->cursor_y);
    }
}

/*
 * Draw a zero-terminated string into the logical HDMI text console.
 */
void hdmi_puts(hdmi_pane_id_t pane_id, const char *s)
{
    while (s && *s)
    {
        hdmi_putc(pane_id, *s++);
    }
}

/*
 * Flush at most max_cells dirty text cells into the framebuffer.
 *
 * The logical text model is compared against drawn_cells so unchanged cells can
 * be skipped. The cursor is rendered as an overlay and therefore causes its
 * current cell to be repainted when needed.
 *
 * Return value:
 *   1 if more dirty work remains
 *   0 if the visible console is fully up to date
 */
int hdmi_present(uint32_t max_cells)
{
    uint32_t rendered = 0u;
    int more_dirty = 0;

    if (!framebuffer)
    {
        return 0;
    }

    if (max_cells == 0u)
    {
        max_cells = 1u;
    }

    if (hdmi_present_pane(&panes[HDMI_PANE_MAIN], &rendered, max_cells))
    {
        more_dirty = 1;
    }

    if (hdmi_present_pane(&panes[HDMI_PANE_MENU], &rendered, max_cells))
    {
        more_dirty = 1;
    }

    return more_dirty;
}

/*
 * Clear only the HDMI text console. The static chrome remains untouched.
 */
void hdmi_clear_pane(hdmi_pane_id_t pane_id)
{
    hdmi_pane_t *pane = hdmi_get_pane(pane_id);

    if (!framebuffer || !pane)
    {
        return;
    }

    hdmi_text_model_clear(pane, pane->fg, pane->bg);
    hdmi_mark_pane_dirty(pane);
}

/*
 * Redraw static chrome and reset the logical text console.
 *
 * Use this after a fullscreen HDMI application releases the display or when the
 * layout/theme must be recreated. Normal clear operations should use
 * hdmi_clear_console() instead.
 */
void hdmi_reset_panes(void)
{
    if (!framebuffer)
    {
        return;
    }

    hdmi_draw_panes_chrome();

    hdmi_text_model_clear(&panes[HDMI_PANE_MAIN], CONSOLE_FG, CONSOLE_PANEL);
    hdmi_text_model_clear(&panes[HDMI_PANE_MENU], CONSOLE_FG, CONSOLE_PANEL);

    hdmi_mark_pane_dirty(&panes[HDMI_PANE_MAIN]);
    hdmi_mark_pane_dirty(&panes[HDMI_PANE_MENU]);
}

/*
 * Busy-wait for the requested duration using the generic counter.
 */
void hdmi_wait_ms(uint32_t ms)
{
    uint64_t counter_freq;
    uint64_t start_counter;
    uint64_t wait_ticks;
    uint64_t now;

    asm volatile("mrs %0, cntfrq_el0" : "=r"(counter_freq));
    asm volatile("mrs %0, cntpct_el0" : "=r"(start_counter));

    wait_ticks = (counter_freq * (uint64_t)ms) / 1000u;

    do
    {
        asm volatile("mrs %0, cntpct_el0" : "=r"(now));
    } while ((now - start_counter) < wait_ticks);
}
