#include "rpi4/hdmi.h"
#include "rpi4/hdmi_font.h"
#include "rpi4/mmio.h"

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

/* Built-in bitmap font geometry. */
#define GLYPH_WIDTH HDMI_FONT_GLYPH_WIDTH
#define GLYPH_HEIGHT HDMI_FONT_GLYPH_HEIGHT
#define GLYPH_SCALE 3u
#define GLYPH_GAP_X 2u
#define GLYPH_GAP_Y 4u
#define CHAR_ADVANCE_X ((GLYPH_WIDTH * GLYPH_SCALE) + GLYPH_GAP_X)
#define CHAR_ADVANCE_Y ((GLYPH_HEIGHT * GLYPH_SCALE) + GLYPH_GAP_Y)

/* Console and bootscreen colors. */
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
static uint32_t console_origin_x = 0;
static uint32_t console_origin_y = 0;
static uint32_t console_content_width = 0;
static uint32_t console_content_height = 0;
static uint32_t console_columns = 0;
static uint32_t console_rows = 0;
static uint32_t cursor_x = 0;
static uint32_t cursor_y = 0;
static uint32_t status_dot_x = 0;
static uint32_t status_dot_y = 0;

static int mailbox_call(uint8_t channel);
static char hdmi_normalize_char(char c);
static const uint8_t *hdmi_lookup_glyph(char c);
static void hdmi_draw_pixel(uint32_t x, uint32_t y, uint32_t color);
static void hdmi_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);
static void hdmi_fill_rect_blend(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                                 uint32_t color_a, uint32_t color_b, int vertical);
static void hdmi_draw_frame(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                            uint32_t border, uint32_t fill);
static void hdmi_draw_corner_glow(uint32_t cx, uint32_t cy, uint32_t radius, uint32_t color);
static void hdmi_draw_progress_bar(uint32_t x, uint32_t y, uint32_t width, uint32_t progress);
static void hdmi_draw_bootscreen_static(void);
static void hdmi_update_boot_progress(uint32_t progress);
static void hdmi_draw_console_chrome(void);
static void hdmi_scroll(void);
static void hdmi_newline(void);
static void hdmi_draw_char_at(uint32_t px, uint32_t py, char c, uint32_t fg, uint32_t bg);
static void hdmi_draw_string_at(uint32_t px, uint32_t py, const char *s, uint32_t fg, uint32_t bg);
static uint32_t hdmi_string_width(const char *s);
static void hdmi_draw_status_dot(uint32_t phase);
static void hdmi_draw_cursor(uint32_t visible);

/*
 * Normalize characters for the small bitmap font.
 * Lowercase input is rendered as uppercase.
 */
static char hdmi_normalize_char(char c)
{
    if (c >= 'a' && c <= 'z')
    {
        return (char)(c - ('a' - 'A'));
    }

    return c;
}

/*
 * Return the bitmap rows for a single character.
 * Unknown characters fall back to '?'.
 */
static const uint8_t *hdmi_lookup_glyph(char c)
{
    char normalized = hdmi_normalize_char(c);
    unsigned int i;

    for (i = 0; i < hdmi_font_count; i++)
    {
        if (hdmi_font[i].c == normalized)
        {
            return hdmi_font[i].rows;
        }
    }

    return hdmi_font[27].rows;
}

/*
 * Write one pixel into the framebuffer.
 */
static void hdmi_draw_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    volatile uint8_t *row;
    volatile uint32_t *pixel;

    if (!framebuffer || x >= framebuffer_width || y >= framebuffer_height)
    {
        return;
    }

    row = (volatile uint8_t *)framebuffer + (y * framebuffer_pitch);
    pixel = (volatile uint32_t *)(row + (x * sizeof(uint32_t)));
    *pixel = color;
}

/*
 * Fill a solid rectangle with a single color.
 */
static void hdmi_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color)
{
    uint32_t yy;
    uint32_t xx;

    for (yy = 0; yy < height; yy++)
    {
        for (xx = 0; xx < width; xx++)
        {
            hdmi_draw_pixel(x + xx, y + yy, color);
        }
    }
}

/*
 * Fill a rectangle using a simple linear color blend.
 * The blend direction is vertical if 'vertical' is non-zero.
 */
static void hdmi_fill_rect_blend(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                                 uint32_t color_a, uint32_t color_b, int vertical)
{
    uint32_t yy;
    uint32_t xx;
    uint32_t steps = vertical ? height : width;

    if (steps <= 1u)
    {
        hdmi_fill_rect(x, y, width, height, color_a);
        return;
    }

    for (yy = 0; yy < height; yy++)
    {
        for (xx = 0; xx < width; xx++)
        {
            uint32_t t = vertical ? yy : xx;
            uint32_t r1 = (color_a >> 16) & 0xFFu;
            uint32_t g1 = (color_a >> 8) & 0xFFu;
            uint32_t b1 = color_a & 0xFFu;
            uint32_t r2 = (color_b >> 16) & 0xFFu;
            uint32_t g2 = (color_b >> 8) & 0xFFu;
            uint32_t b2 = color_b & 0xFFu;
            uint32_t divisor = steps - 1u;
            uint32_t r = ((r1 * (divisor - t)) + (r2 * t)) / divisor;
            uint32_t g = ((g1 * (divisor - t)) + (g2 * t)) / divisor;
            uint32_t b = ((b1 * (divisor - t)) + (b2 * t)) / divisor;

            hdmi_draw_pixel(x + xx, y + yy, (r << 16) | (g << 8) | b);
        }
    }
}

/*
 * Draw a simple panel frame with a one-pixel border.
 */
static void hdmi_draw_frame(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                            uint32_t border, uint32_t fill)
{
    if (width < 2u || height < 2u)
    {
        return;
    }

    hdmi_fill_rect(x, y, width, height, border);
    hdmi_fill_rect(x + 1u, y + 1u, width - 2u, height - 2u, fill);
}

/*
 * Draw a coarse quarter-circle glow used in the bootscreen background.
 */
static void hdmi_draw_corner_glow(uint32_t cx, uint32_t cy, uint32_t radius, uint32_t color)
{
    uint32_t y;
    uint32_t x;
    uint32_t rr = radius * radius;

    for (y = 0; y < radius; y++)
    {
        for (x = 0; x < radius; x++)
        {
            uint32_t dx = radius - x;
            uint32_t dy = radius - y;
            uint32_t dist = (dx * dx) + (dy * dy);

            if (dist <= rr)
            {
                hdmi_draw_pixel(cx + x, cy + y, color);
            }
        }
    }
}

/*
 * Return the rendered width of a string in console character cells.
 */
static uint32_t hdmi_string_width(const char *s)
{
    uint32_t width = 0;

    while (s && *s)
    {
        width += CHAR_ADVANCE_X;
        s++;
    }

    return width;
}

/*
 * Draw one scaled bitmap glyph with a subtle shadow.
 */
static void hdmi_draw_char_at(uint32_t px, uint32_t py, char c, uint32_t fg, uint32_t bg)
{
    const uint8_t *glyph = hdmi_lookup_glyph(c);
    uint32_t row;
    uint32_t col;
    uint32_t sy;
    uint32_t sx;
    uint32_t x;
    uint32_t y;

    hdmi_fill_rect(px, py, CHAR_ADVANCE_X, CHAR_ADVANCE_Y, bg);

    for (row = 0; row < GLYPH_HEIGHT; row++)
    {
        for (col = 0; col < GLYPH_WIDTH; col++)
        {
            if (!(glyph[row] & (1u << (GLYPH_WIDTH - 1u - col))))
            {
                continue;
            }

            for (sy = 0; sy < GLYPH_SCALE; sy++)
            {
                y = py + 1u + (row * GLYPH_SCALE) + sy;

                for (sx = 0; sx < GLYPH_SCALE; sx++)
                {
                    x = px + 1u + (col * GLYPH_SCALE) + sx;
                    hdmi_draw_pixel(x + 1u, y + 1u, CONSOLE_SHADOW);
                    hdmi_draw_pixel(x, y, fg);
                }
            }
        }
    }
}

/*
 * Draw a zero-terminated string using the built-in bitmap font.
 */
static void hdmi_draw_string_at(uint32_t px, uint32_t py, const char *s, uint32_t fg, uint32_t bg)
{
    uint32_t x = px;

    while (s && *s)
    {
        hdmi_draw_char_at(x, py, *s, fg, bg);
        x += CHAR_ADVANCE_X;
        s++;
    }
}

/*
 * Draw the small status indicator shown in the console header.
 */
static void hdmi_draw_status_dot(uint32_t phase)
{
    uint32_t color;

    if (!framebuffer)
    {
        return;
    }

    hdmi_fill_rect(status_dot_x, status_dot_y, 10u, 10u, CONSOLE_PANEL_ALT);

    switch (phase % 4u)
    {
    case 0u:
        color = 0x003A5366u;
        break;
    case 1u:
        color = 0x00467EA6u;
        break;
    case 2u:
        color = 0x0028C7FAu;
        break;
    default:
        color = 0x0090EBFFu;
        break;
    }

    hdmi_fill_rect(status_dot_x + 1u, status_dot_y + 1u, 8u, 8u, color);
}

/*
 * Draw or erase the thin text cursor at the current console position.
 */
static void hdmi_draw_cursor(uint32_t visible)
{
    uint32_t x;
    uint32_t y;
    uint32_t h;
    uint32_t color;

    if (!framebuffer || cursor_x >= console_columns || cursor_y >= console_rows)
    {
        return;
    }

    x = console_origin_x + (cursor_x * CHAR_ADVANCE_X);
    y = console_origin_y + (cursor_y * CHAR_ADVANCE_Y);
    h = (GLYPH_HEIGHT * GLYPH_SCALE) + 2u;
    color = visible ? CONSOLE_ACCENT : CONSOLE_PANEL;

    hdmi_fill_rect(x + (GLYPH_WIDTH * GLYPH_SCALE) + 1u, y + 2u, 3u, h, color);
}

/*
 * Draw the graphical progress bar used in the bootscreen.
 */
static void hdmi_draw_progress_bar(uint32_t x, uint32_t y, uint32_t width, uint32_t progress)
{
    uint32_t fill_width;

    hdmi_draw_frame(x, y, width, 14u, BOOT_CARD_EDGE, BOOT_CARD);
    hdmi_fill_rect_blend(x + 2u, y + 2u, width - 4u, 10u, 0x000D1420u, 0x00111D2Au, 0);

    fill_width = ((width - 4u) * progress) / 100u;
    if (fill_width > 0u)
    {
        hdmi_fill_rect_blend(x + 2u, y + 2u, fill_width, 10u, 0x001B7FE0u, 0x0041E4FFu, 0);
        if (fill_width > 10u)
        {
            hdmi_fill_rect(x + 2u + fill_width - 8u, y + 2u, 6u, 10u, 0x0098F4FFu);
        }
    }
}

/*
 * Draw the static part of the bootscreen.
 * The progress bar is updated separately during the short boot animation.
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
    hdmi_draw_string_at(card_x + 32u, card_y + 20u, eyebrow, BOOT_MUTED, BOOT_CARD);

    hdmi_fill_rect(logo_x, logo_y, 156u, 156u, BOOT_ACCENT);
    hdmi_fill_rect(logo_x + 10u, logo_y + 10u, 136u, 136u, BOOT_CARD);
    hdmi_fill_rect(logo_x + 50u, logo_y + 24u, 56u, 56u, BOOT_ACCENT);
    hdmi_fill_rect(logo_x + 70u, logo_y + 82u, 16u, 48u, BOOT_ACCENT);
    hdmi_fill_rect(logo_x + 116u, logo_y + 38u, 12u, 78u, 0x0041E4FFu);

    hdmi_draw_string_at((framebuffer_width - hdmi_string_width(title)) / 2u, card_y + 206u, title, CONSOLE_FG, BOOT_CARD);
    hdmi_draw_string_at((framebuffer_width - hdmi_string_width(subtitle)) / 2u, card_y + 236u, subtitle, BOOT_MUTED, BOOT_CARD);
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
 * Draw the static console chrome around the HDMI text area.
 */
static void hdmi_draw_console_chrome(void)
{
    uint32_t panel_x = 48u;
    uint32_t panel_y = 38u;
    uint32_t panel_w = framebuffer_width - 96u;
    uint32_t panel_h = framebuffer_height - 76u;
    uint32_t header_h = 42u;
    uint32_t padding_x = 24u;
    uint32_t padding_y = 22u;

    hdmi_fill_rect_blend(0u, 0u, framebuffer_width, framebuffer_height, 0x000C1520u, CONSOLE_BG, 1);
    hdmi_fill_rect_blend(0u, 0u, framebuffer_width, 120u, 0x00152131u, CONSOLE_BG, 1);

    hdmi_draw_frame(panel_x, panel_y, panel_w, panel_h, CONSOLE_BORDER, CONSOLE_PANEL);
    hdmi_fill_rect(panel_x + 1u, panel_y + 1u, panel_w - 2u, header_h, CONSOLE_PANEL_ALT);
    hdmi_fill_rect(panel_x + 1u, panel_y + header_h + 1u, panel_w - 2u, 2u, CONSOLE_ACCENT);
    hdmi_fill_rect(panel_x + 18u, panel_y + 13u, 10u, 10u, 0x00FF6B6Bu);
    hdmi_fill_rect(panel_x + 34u, panel_y + 13u, 10u, 10u, 0x00FFCE54u);
    hdmi_fill_rect(panel_x + 50u, panel_y + 13u, 10u, 10u, 0x0048E27Bu);
    hdmi_draw_string_at(panel_x + 84u, panel_y + 8u, "HDMI Console", CONSOLE_FG, CONSOLE_PANEL_ALT);

    status_dot_x = panel_x + panel_w - 30u;
    status_dot_y = panel_y + 13u;
    hdmi_draw_status_dot(0u);

    console_origin_x = panel_x + padding_x;
    console_origin_y = panel_y + header_h + padding_y;
    console_content_width = panel_w - (padding_x * 2u);
    console_content_height = panel_h - header_h - (padding_y * 2u) - 8u;
    console_columns = console_content_width / CHAR_ADVANCE_X;
    console_rows = console_content_height / CHAR_ADVANCE_Y;
    cursor_x = 0;
    cursor_y = 0;
}

/*
 * Scroll the visible HDMI text area up by exactly one text row.
 * Existing pixels are moved in-place inside the console content region.
 */
static void hdmi_scroll(void)
{
    uint32_t rows_to_move;
    uint32_t y;
    uint32_t x;
    uint32_t pixels_per_row;
    volatile uint32_t *dst_row;
    volatile uint32_t *src_row;

    if (!framebuffer || console_content_height <= CHAR_ADVANCE_Y)
    {
        return;
    }

    rows_to_move = console_content_height - CHAR_ADVANCE_Y;
    pixels_per_row = console_content_width;

    for (y = 0; y < rows_to_move; y++)
    {
        dst_row = (volatile uint32_t *)((volatile uint8_t *)framebuffer +
                                        ((console_origin_y + y) * framebuffer_pitch) +
                                        (console_origin_x * sizeof(uint32_t)));
        src_row = (volatile uint32_t *)((volatile uint8_t *)framebuffer +
                                        ((console_origin_y + y + CHAR_ADVANCE_Y) * framebuffer_pitch) +
                                        (console_origin_x * sizeof(uint32_t)));

        for (x = 0; x < pixels_per_row; x++)
        {
            dst_row[x] = src_row[x];
        }
    }

    for (; y < console_content_height; y++)
    {
        dst_row = (volatile uint32_t *)((volatile uint8_t *)framebuffer +
                                        ((console_origin_y + y) * framebuffer_pitch) +
                                        (console_origin_x * sizeof(uint32_t)));
        for (x = 0; x < pixels_per_row; x++)
        {
            dst_row[x] = CONSOLE_PANEL;
        }
    }
}

/*
 * Advance the console cursor to the next line and scroll if needed.
 */
static void hdmi_newline(void)
{
    cursor_x = 0;
    cursor_y++;

    if (cursor_y >= console_rows)
    {
        hdmi_scroll();
        cursor_y = console_rows - 1u;
    }
}

/*
 * Submit a property mailbox request and wait for the response.
 */
static int mailbox_call(uint8_t channel)
{
    uint32_t message = (((uint32_t)(uintptr_t)mailbox) & ~0xFu) | (uint32_t)(channel & 0xFu);

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
            return mailbox[1] == MAILBOX_RESPONSE_OK;
        }
    }
}

/*
 * Initialize the HDMI framebuffer via the firmware mailbox interface.
 *
 * Steps:
 *  - request a 1280x720 framebuffer in 32-bit RGB format
 *  - allocate the framebuffer through the firmware
 *  - query the framebuffer pitch
 *  - draw the static console chrome
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

    hdmi_draw_console_chrome();
    return 1;
}

/*
 * Draw one character into the HDMI text console.
 * Control characters for newline, tab and backspace are handled explicitly.
 */
void hdmi_putc(char c)
{
    uint32_t px;
    uint32_t py;

    if (!framebuffer)
    {
        return;
    }

    if (c == '\r')
    {
        return;
    }

    if (c == '\n')
    {
        hdmi_draw_cursor(0u);
        hdmi_newline();
        hdmi_draw_cursor(1u);
        return;
    }

    if (c == '\b')
    {
        if (cursor_x > 0u)
        {
            cursor_x--;
        }

        px = console_origin_x + (cursor_x * CHAR_ADVANCE_X);
        py = console_origin_y + (cursor_y * CHAR_ADVANCE_Y);
        hdmi_fill_rect(px, py, CHAR_ADVANCE_X, CHAR_ADVANCE_Y, CONSOLE_PANEL);
        hdmi_draw_cursor(1u);
        return;
    }

    if (c == '\t')
    {
        hdmi_putc(' ');
        hdmi_putc(' ');
        hdmi_putc(' ');
        hdmi_putc(' ');
        return;
    }

    if (cursor_x >= console_columns)
    {
        hdmi_draw_cursor(0u);
        hdmi_newline();
    }

    hdmi_draw_cursor(0u);

    px = console_origin_x + (cursor_x * CHAR_ADVANCE_X);
    py = console_origin_y + (cursor_y * CHAR_ADVANCE_Y);
    hdmi_draw_char_at(px, py, c, CONSOLE_FG, CONSOLE_PANEL);
    cursor_x++;

    if (cursor_x >= console_columns)
    {
        hdmi_newline();
    }

    hdmi_draw_cursor(1u);
}

/*
 * Draw a zero-terminated string into the HDMI console.
 */
void hdmi_puts(const char *s)
{
    while (s && *s)
    {
        hdmi_putc(*s++);
    }
}

/*
 * Show the short animated HDMI bootscreen.
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
 * Clear the HDMI text console and redraw its static chrome.
 */
void hdmi_clear_console(void)
{
    if (!framebuffer)
    {
        return;
    }

    hdmi_draw_console_chrome();
    hdmi_draw_cursor(1u);
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
