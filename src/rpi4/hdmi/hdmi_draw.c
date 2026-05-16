#include "rpi4/hdmi/hdmi_draw.h"

/*
 * Low-level HDMI framebuffer drawing primitives.
 *
 * This module does not own the HDMI device. hdmi.c allocates the framebuffer
 * and attaches it here via hdmi_draw_set_framebuffer().
 */

/* Active framebuffer target used by all draw primitives. */
static uint32_t *draw_framebuffer = 0;
static uint32_t draw_width = 0;
static uint32_t draw_height = 0;
static uint32_t draw_pitch = 0;

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
 * Attach the draw module to the active framebuffer.
 */
void hdmi_draw_set_framebuffer(uint32_t *framebuffer, uint32_t width, uint32_t height, uint32_t pitch)
{
    draw_framebuffer = framebuffer;
    draw_width = width;
    draw_height = height;
    draw_pitch = pitch;
}

/*
 * Write one pixel into the framebuffer.
 */
void hdmi_draw_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    volatile uint8_t *row;
    volatile uint32_t *pixel;

    if (!draw_framebuffer || x >= draw_width || y >= draw_height)
    {
        return;
    }

    row = (volatile uint8_t *)draw_framebuffer + (y * draw_pitch);
    pixel = (volatile uint32_t *)(row + (x * sizeof(uint32_t)));
    *pixel = color;
}

/*
 * Fill a solid rectangle with a single color.
 */
void hdmi_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color)
{
    uint32_t yy;
    uint32_t xx;
    uint32_t max_x;
    uint32_t max_y;
    uint32_t clipped_width;
    uint32_t clipped_height;
    volatile uint32_t *row;

    if (!draw_framebuffer || width == 0u || height == 0u)
    {
        return;
    }

    if (x >= draw_width || y >= draw_height)
    {
        return;
    }

    max_x = x + width;
    max_y = y + height;

    if (max_x < x || max_x > draw_width)
    {
        max_x = draw_width;
    }

    if (max_y < y || max_y > draw_height)
    {
        max_y = draw_height;
    }

    clipped_width = max_x - x;
    clipped_height = max_y - y;

    for (yy = 0; yy < clipped_height; yy++)
    {
        row = (volatile uint32_t *)((volatile uint8_t *)draw_framebuffer +
                                    ((y + yy) * draw_pitch) +
                                    (x * sizeof(uint32_t)));

        for (xx = 0; xx < clipped_width; xx++)
        {
            row[xx] = color;
        }
    }
}

/*
 * Fill a rectangle using a simple linear color blend.
 * The blend direction is vertical if 'vertical' is non-zero.
 */
void hdmi_fill_rect_blend(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                          uint32_t color_a, uint32_t color_b, int vertical)
{
    uint32_t yy;
    uint32_t xx;
    uint32_t max_x;
    uint32_t max_y;
    uint32_t clipped_width;
    uint32_t clipped_height;
    uint32_t steps = vertical ? height : width;
    uint32_t r1;
    uint32_t g1;
    uint32_t b1;
    uint32_t r2;
    uint32_t g2;
    uint32_t b2;
    uint32_t divisor;
    volatile uint32_t *row;

    if (!draw_framebuffer || width == 0u || height == 0u)
    {
        return;
    }

    if (x >= draw_width || y >= draw_height)
    {
        return;
    }

    if (steps <= 1u)
    {
        hdmi_fill_rect(x, y, width, height, color_a);
        return;
    }

    max_x = x + width;
    max_y = y + height;

    if (max_x < x || max_x > draw_width)
    {
        max_x = draw_width;
    }

    if (max_y < y || max_y > draw_height)
    {
        max_y = draw_height;
    }

    clipped_width = max_x - x;
    clipped_height = max_y - y;
    r1 = (color_a >> 16) & 0xFFu;
    g1 = (color_a >> 8) & 0xFFu;
    b1 = color_a & 0xFFu;
    r2 = (color_b >> 16) & 0xFFu;
    g2 = (color_b >> 8) & 0xFFu;
    b2 = color_b & 0xFFu;
    divisor = steps - 1u;

    for (yy = 0; yy < clipped_height; yy++)
    {
        row = (volatile uint32_t *)((volatile uint8_t *)draw_framebuffer +
                                    ((y + yy) * draw_pitch) +
                                    (x * sizeof(uint32_t)));

        for (xx = 0; xx < clipped_width; xx++)
        {
            uint32_t t = vertical ? yy : xx;
            uint32_t r = ((r1 * (divisor - t)) + (r2 * t)) / divisor;
            uint32_t g = ((g1 * (divisor - t)) + (g2 * t)) / divisor;
            uint32_t b = ((b1 * (divisor - t)) + (b2 * t)) / divisor;

            row[xx] = (r << 16) | (g << 8) | b;
        }
    }
}

/*
 * Draw a simple panel frame with a one-pixel border.
 */
void hdmi_draw_frame(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
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
 * Fill a simple circle using integer distance checks.
 */
void hdmi_fill_circle(uint32_t cx, uint32_t cy, uint32_t radius, uint32_t color)
{
    int32_t r = (int32_t)radius;
    int32_t rr = r * r;

    for (int32_t y = -r; y <= r; y++)
    {
        for (int32_t x = -r; x <= r; x++)
        {
            if ((x * x) + (y * y) <= rr)
            {
                hdmi_draw_pixel((uint32_t)((int32_t)cx + x), (uint32_t)((int32_t)cy + y), color);
            }
        }
    }
}

/*
 * Draw a simple circle ring between two radii.
 */
void hdmi_draw_circle_ring(uint32_t cx, uint32_t cy, uint32_t outer_radius,
                           uint32_t inner_radius, uint32_t color)
{
    int32_t outer = (int32_t)outer_radius;
    int32_t inner = (int32_t)inner_radius;
    int32_t outer_rr = outer * outer;
    int32_t inner_rr = inner * inner;

    for (int32_t y = -outer; y <= outer; y++)
    {
        for (int32_t x = -outer; x <= outer; x++)
        {
            int32_t dist = (x * x) + (y * y);

            if (dist <= outer_rr && dist >= inner_rr)
            {
                hdmi_draw_pixel((uint32_t)((int32_t)cx + x), (uint32_t)((int32_t)cy + y), color);
            }
        }
    }
}

/*
 * Draw a coarse quarter-circle glow used in the bootscreen background.
 */
void hdmi_draw_corner_glow(uint32_t cx, uint32_t cy, uint32_t radius, uint32_t color)
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
uint32_t hdmi_string_width(const char *s)
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
 *
 * This is  a low-level rendering primitive only. Normal console output must
 * modify text_cells[] and let hdmi_present() call this function.
 */
void hdmi_draw_char_at(uint32_t px, uint32_t py, char c, uint32_t fg, uint32_t bg, uint32_t shadow)
{
    const uint8_t *glyph;
    uint32_t row;
    uint32_t col;
    uint32_t sy;
    uint32_t sx;
    uint32_t x;
    uint32_t y;

    if (c == 0)
    {
        c = ' ';
    }

    glyph = hdmi_lookup_glyph(c);

    hdmi_fill_rect(px, py, CHAR_ADVANCE_X, CHAR_ADVANCE_Y, bg);

    if (c == ' ')
    {
        return;
    }

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
                    hdmi_draw_pixel(x + 1u, y + 1u, shadow);
                    hdmi_draw_pixel(x, y, fg);
                }
            }
        }
    }
}

/*
 * Draw a zero-terminated string using the built-in bitmap font.
 */
void hdmi_draw_string_at(uint32_t px, uint32_t py, const char *s, uint32_t fg, uint32_t bg, uint32_t shadow)
{
    uint32_t x = px;

    while (s && *s)
    {
        hdmi_draw_char_at(x, py, *s, fg, bg, shadow);
        x += CHAR_ADVANCE_X;
        s++;
    }
}

/*
 * Draw the small status indicator shown in the console header.
 */
void hdmi_draw_status_dot(uint32_t x, uint32_t y, uint32_t phase, uint32_t bg)
{
    uint32_t color;

    if (!draw_framebuffer)
    {
        return;
    }

    hdmi_fill_rect(x, y, 10u, 10u, bg);

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

    hdmi_fill_rect(x + 1u, y + 1u, 8u, 8u, color);
}

/*
 * Draw the thin text cursor as an overlay inside one already-rendered cell.
 */
void hdmi_draw_cursor_at(uint32_t px, uint32_t py, uint32_t color)
{
    uint32_t h = (GLYPH_HEIGHT * GLYPH_SCALE) + 2u;
    hdmi_fill_rect(px + (GLYPH_WIDTH * GLYPH_SCALE) + 1u, py + 2u, 3u, h, color);
}

/*
 * Draw the graphical progress bar used in the bootscreen.
 */
void hdmi_draw_progress_bar(uint32_t x, uint32_t y, uint32_t width, uint32_t progress)
{
    uint32_t fill_width;

    hdmi_draw_frame(x, y, width, 14u, 0x001B2A3Eu, 0x000E1623u);
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
 * Draw the decorative bootscreen badge.
 */
void hdmi_draw_boot_badge(uint32_t x, uint32_t y)
{
    uint32_t cx = x + 78u;
    uint32_t cy = y + 78u;

    hdmi_fill_circle(cx, cy, 78u, 0x00111D2Au);
    hdmi_draw_circle_ring(cx, cy, 78u, 73u, 0x0028C7FAu);
    hdmi_draw_circle_ring(cx, cy, 63u, 60u, 0x001B7FE0u);

    hdmi_fill_rect(cx - 38u, cy - 40u, 76u, 80u, 0x001B2633u);
    hdmi_fill_rect(cx - 31u, cy - 31u, 62u, 62u, 0x000E1623u);
    hdmi_fill_rect(cx - 44u, cy - 22u, 12u, 44u, 0x0028C7FAu);
    hdmi_fill_rect(cx + 32u, cy - 22u, 12u, 44u, 0x0028C7FAu);
    hdmi_fill_rect(cx - 22u, cy - 46u, 44u, 12u, 0x0041E4FFu);
    hdmi_fill_rect(cx - 22u, cy + 34u, 44u, 12u, 0x0041E4FFu);

    hdmi_fill_rect(cx - 18u, cy - 18u, 36u, 36u, 0x0028C7FAu);
    hdmi_fill_rect(cx - 11u, cy - 11u, 22u, 22u, 0x0098F4FFu);
    hdmi_fill_rect(cx - 3u, cy - 50u, 6u, 100u, 0x00111D2Au);
    hdmi_fill_rect(cx - 50u, cy - 3u, 100u, 6u, 0x00111D2Au);
}