#ifndef RPI4_HDMI_HDMI_DRAW_H
#define RPI4_HDMI_HDMI_DRAW_H

#include <stdint.h>
#include "rpi4/hdmi/hdmi_font.h"

/* Built-in bitmap font geometry. */
#define GLYPH_WIDTH HDMI_FONT_GLYPH_WIDTH
#define GLYPH_HEIGHT HDMI_FONT_GLYPH_HEIGHT
#define GLYPH_SCALE 3u
#define GLYPH_GAP_X 2u
#define GLYPH_GAP_Y 4u
#define CHAR_ADVANCE_X ((GLYPH_WIDTH * GLYPH_SCALE) + GLYPH_GAP_X)
#define CHAR_ADVANCE_Y ((GLYPH_HEIGHT * GLYPH_SCALE) + GLYPH_GAP_Y)

void hdmi_draw_set_framebuffer(uint32_t *framebuffer, uint32_t width, uint32_t height, uint32_t pitch);

void hdmi_draw_pixel(uint32_t x, uint32_t y, uint32_t color);
void hdmi_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);
void hdmi_fill_rect_blend(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color_a, uint32_t color_b, int vertical);
void hdmi_draw_frame(uint32_t x, uint32_t y, uint32_t widht, uint32_t height, uint32_t border, uint32_t fill);
void hdmi_fill_circle(uint32_t cx, uint32_t cy, uint32_t radius , uint32_t color);
void hdmi_draw_circle_ring(uint32_t cx, uint32_t cy, uint32_t outer_radius, uint32_t inner_radius, uint32_t color);
void hdmi_draw_corner_glow(uint32_t cx, uint32_t cy, uint32_t radius, uint32_t color);

uint32_t hdmi_string_width(const char *s);
void hdmi_draw_char_at(uint32_t px, uint32_t py, char c, uint32_t fg, uint32_t bg, uint32_t shadow);
void hdmi_draw_string_at(uint32_t px, uint32_t py, const char *s, uint32_t fg, uint32_t bg, uint32_t shadow);

void hdmi_draw_status_dot(uint32_t x, uint32_t y, uint32_t phase, uint32_t bg);
void hdmi_draw_cursor_at(uint32_t px, uint32_t py, uint32_t color);
void hdmi_draw_progress_bar(uint32_t x, uint32_t y, uint32_t widht, uint32_t progress);
void hdmi_draw_boot_badge(uint32_t x, uint32_t y);

#endif