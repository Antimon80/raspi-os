#include "util/format.h"

/*
 * Initialize a bounded output buffer.
 *
 * The buffer is cleared immediately when a valid storage area is provided.
 */
void format_buffer_init(format_buffer_t *fmt, char *buffer, unsigned int size)
{
    if (!fmt)
    {
        return;
    }

    fmt->buffer = buffer;
    fmt->size = size;
    fmt->pos = 0u;

    if (buffer && size > 0u)
    {
        buffer[0] = 0;
    }
}

/*
 * Append one character to the buffer.
 *
 * The function keeps the string null-terminated and silently drops the
 * character if the buffer is full.
 */
void format_append_char(format_buffer_t *fmt, char c)
{
    if (!fmt || !fmt->buffer || fmt->size == 0u)
    {
        return;
    }

    if (fmt->pos + 1u >= fmt->size)
    {
        return;
    }

    fmt->buffer[fmt->pos] = c;
    fmt->pos++;
    fmt->buffer[fmt->pos] = 0;
}

/*
 * Append a null-terminated string to the buffer.
 *
 * Characters are appended one by one through format_append_char(), so the
 * same bounds checks and null-termination rules apply.
 */
void format_append_string(format_buffer_t *fmt, const char *s)
{
    while (s && *s)
    {
        format_append_char(fmt, *s);
        s++;
    }
}

/*
 * Append an unsigned integer in decimal notation.
 *
 * Digits are generated in reverse order first and then written back in the
 * correct order.
 */
void format_append_uint(format_buffer_t *fmt, uint64_t value)
{
    char tmp[24];
    unsigned int count = 0u;

    if (value == 0u)
    {
        format_append_char(fmt, '0');
        return;
    }

    while (value > 0u && count < sizeof(tmp))
    {
        tmp[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (count > 0u)
    {
        count--;
        format_append_char(fmt, tmp[count]);
    }
}

/*
 * Append a signed integer in decimal notation.
 *
 * Negative values are converted through int64_t first so INT32_MIN can be
 * handled safely.
 */
void format_append_int(format_buffer_t *fmt, int32_t value)
{
    if (value < 0)
    {
        format_append_char(fmt, '-');
        format_append_uint(fmt, (uint64_t)(-(int64_t)value));
        return;
    }

    format_append_uint(fmt, (uint64_t)value);
}

/*
 * Append a centi-unit value as a fixed two-decimal number.
 *
 * Example:
 *   3542 -> 35.42
 */
void format_append_centi(format_buffer_t *fmt, int32_t centi_value)
{
    int32_t whole;
    int32_t fraction;

    if (centi_value < 0)
    {
        format_append_char(fmt, '-');
        centi_value = (int32_t)(-(int64_t)centi_value);
    }

    whole = centi_value / 100;
    fraction = centi_value % 100;

    format_append_int(fmt, whole);
    format_append_char(fmt, '.');

    if (fraction < 10)
    {
        format_append_char(fmt, '0');
    }

    format_append_int(fmt, fraction);
}

void format_append_timestamp(format_buffer_t *fmt, uint64_t tick)
{
    uint64_t seconds = tick / 100u;
    uint64_t hours = seconds / 3600u;
    uint64_t minutes = (seconds / 60u) % 60u;
    uint64_t secs = seconds % 60u;

    if (hours < 10u)
    {
        format_append_char(fmt, '0');
    }
    format_append_uint(fmt, hours);
    format_append_char(fmt, ':');

    if (minutes < 10u)
    {
        format_append_char(fmt, '0');
    }
    format_append_uint(fmt, minutes);
    format_append_char(fmt, ':');

    if (secs < 10u)
    {
        format_append_char(fmt, '0');
    }
    format_append_uint(fmt, secs);
}

/*
 * Build a shell command from a fixed prefix and a name.
 *
 * Used by menu code to generate commands such as "start env" or "log shell"
 * without depending on snprintf().
 */
void format_make_prefixed_command(char *out, unsigned int size, const char *prefix, const char *name)
{
    format_buffer_t fmt;

    format_buffer_init(&fmt, out, size);
    format_append_string(&fmt, prefix);
    format_append_string(&fmt, name);
}