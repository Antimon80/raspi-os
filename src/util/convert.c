#include "util/convert.h"

/*
 * Parse an unsigned decimal integer.
 *
 * Returns 0 on success, -1 on failure.
 */
int parse_uint(const char *s, int *value)
{
    int result = 0;

    if (!s || !*s || !value)
    {
        return -1;
    }

    while (*s)
    {
        if (*s < '0' || *s > '9')
        {
            return -1;
        }

        result = result * 10 + (*s - '0');
        s++;
    }

    *value = result;
    return 0;
}

/*
 * Convert a positive integer value to decimal ASCII.
 * Returns the number of characters written (without trailing '\0').
 */
int utoa_dec(uint64_t value, char *buffer, int buffer_size)
{
    char temp[32];
    int temp_len = 0;
    int out_len = 0;

    if (!buffer || buffer_size <= 0)
    {
        return 0;
    }

    if (value == 0)
    {
        if (buffer_size > 1)
        {
            buffer[0] = '0';
            buffer[1] = '\0';
            return 1;
        }

        buffer[0] = '\0';
        return 0;
    }

    while (value > 0 && temp_len < (int)sizeof(temp))
    {
        temp[temp_len++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (temp_len > 0 && out_len < (buffer_size - 1))
    {
        buffer[out_len++] = temp[--temp_len];
    }

    buffer[out_len] = '\0';
    return out_len;
}

/*
 * Convert an integer value to hexadecimal ASCII.
 * Returns the number of characters written (without trailing '\0').
 */
int utoa_hex(uint64_t value, char *buffer, int buffer_size)
{
    const char *hex = "0123456789ABCDEF";
    char temp[32];
    int temp_len = 0;
    int out_len = 0;

    if (!buffer || buffer_size <= 0)
    {
        return 0;
    }

    if (value == 0)
    {
        if (buffer_size > 1)
        {
            buffer[0] = '0';
            buffer[1] = '\0';
            return 1;
        }

        buffer[0] = '\0';
        return 0;
    }

    while (value > 0 && temp_len < (int)sizeof(temp))
    {
        temp[temp_len++] = hex[value & 0xF];
        value >>= 4;
    }

    while (temp_len > 0 && out_len < (buffer_size - 1))
    {
        buffer[out_len++] = temp[--temp_len];
    }

    buffer[out_len] = '\0';
    return out_len;
}