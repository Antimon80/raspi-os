#ifndef UTIL_CONVERT_H
#define UTIL_CONVERT_H

#include <stdint.h>

int parse_uint(const char *s, int *value);
int utoa_dec(uint64_t value, char *buffer, int buffer_size);
int utoa_hex(uint64_t value, char *buffer, int buffer_size);

#endif