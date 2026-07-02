#pragma once

#include <stddef.h>

void *memset(void *dest, int val, size_t count);
void *memcpy(void *dest, const void *src, size_t count);
void *memmove(void *dest, const void *src, size_t count);
int memcmp(const void *a, const void *b, size_t count);
static inline int abs(int x) { return x < 0 ? -x : x; }
