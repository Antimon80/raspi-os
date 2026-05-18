#ifndef UTIL_FORMAT_H
#define UTIL_FORMAT_H

#include <stdint.h>

typedef struct
{
    char *buffer;
    unsigned int size;
    unsigned int pos;
} format_buffer_t;

void format_buffer_init(format_buffer_t *fmt, char *buffer, unsigned int size);

void format_append_char(format_buffer_t *fmt, char c);
void format_append_string(format_buffer_t *fmt, const char *s);
void format_append_uint(format_buffer_t *fmt, uint64_t value);
void format_append_int(format_buffer_t *fmt, int32_t value);
void format_append_centi(format_buffer_t *fmt, int32_t centi_value);
void format_append_timestamp(format_buffer_t *fmt, uint64_t tick);

void format_make_prefixed_command(char *out, unsigned int size, const char *prefix, const char *name);

#endif