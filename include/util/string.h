#ifndef UTIL_STRING_H
#define UTIL_STRING_H

int str_length(const char *s);
void str_copy(char *dst, const char *src, int max_len);
int str_equals(const char *a, const char *b);
int str_starts_with(const char *str, const char *prefix);

#endif