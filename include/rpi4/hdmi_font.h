#ifndef RPI4_HDMI_FONT_H
#define RPI4_HDMI_FONT_H

#include <stdint.h>

#define HDMI_FONT_GLYPH_WIDTH 5u
#define HDMI_FONT_GLYPH_HEIGHT 7u

typedef struct
{
    char c;
    uint8_t rows[HDMI_FONT_GLYPH_HEIGHT];
} hdmi_glyph_t;

extern const hdmi_glyph_t hdmi_font[];
extern const unsigned int hdmi_font_count;

#endif
