#include "util/string.h"

/*
 * Return the length of a zero-terminated string.
 */
int str_length(const char *s)
{
    int len = 0;

    if (!s)
    {
        return 0;
    }

    while (s[len] != '\0')
    {
        len++;
    }

    return len;
}

/* Copy a zero-terminated string into a fixed-size buffer.*/
void str_copy(char *dst, const char *src, int max_len)
{
    int i = 0;

    if (!dst || max_len < 0)
    {
        return;
    }

    if (!src)
    {
        dst[0] = '\0';
        return;
    }

    while (src[i] != '\0' && i < (max_len - 1))
    {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

/*
 * Compare two zero-terminated strings.
 * Returns 1 if equal, 0 otherwise.
 */
int str_equals(const char *a, const char *b)
{
    while (*a && *b)
    {
        if (*a != *b)
        {
            return 0;
        }

        a++;
        b++;
    }

    return (*a == '\0' && *b == '\0');
}

/*
 * Check whether 'str' starts with 'prefix'.
 *
 * Returns 1 if true, 0 otherwise.
 */
int str_starts_with(const char *str, const char *prefix)
{
    while (*prefix)
    {
        if (*str != *prefix)
        {
            return 0;
        }

        str++;
        prefix++;
    }

    return 1;
}