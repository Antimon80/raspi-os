#include "util/memory.h"

void *memcpy(void *dest, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    for (size_t i = 0; i < n; i++)
    {
        d[i] = s[i];
    }

    return dest;
}

void *memset(void *ptr, int value, size_t num)
{
    unsigned char *p = (unsigned char *)ptr;

    for (size_t i = 0; i < num; i++)
    {
        p[i] = (unsigned char)value;
    }

    return ptr;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *p1 = a;
    const unsigned char *p2 = b;

    for (size_t i = 0; i < n; i++)
    {
        if (p1[i] != p2[i])
        {
            return p1[i] - p2[i];
        }
    }

    return 0;
}