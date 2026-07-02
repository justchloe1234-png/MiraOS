#include "mem.h"
#include <stdint.h>

void *memset(void *dest, int val, size_t count) {
    uint8_t *dst = (uint8_t *)dest;
    for (size_t i = 0; i < count; i++)
        dst[i] = (uint8_t)val;
    return dest;
}

void *memcpy(void *dest, const void *src, size_t count) {
    uint8_t *dst = (uint8_t *)dest;
    const uint8_t *src_bytes = (const uint8_t *)src;
    for (size_t i = 0; i < count; i++)
        dst[i] = src_bytes[i];
    return dest;
}

void *memmove(void *dest, const void *src, size_t count) {
    uint8_t *dst = (uint8_t *)dest;
    const uint8_t *src_bytes = (const uint8_t *)src;
    
    if (dst < src_bytes) {
        for (size_t i = 0; i < count; i++)
            dst[i] = src_bytes[i];
    } else if (dst > src_bytes) {
        for (size_t i = count; i > 0; i--)
            dst[i - 1] = src_bytes[i - 1];
    }
    return dest;
}

int memcmp(const void *a, const void *b, size_t count) {
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    for (size_t i = 0; i < count; i++) {
        if (pa[i] != pb[i])
            return pa[i] - pb[i];
    }
    return 0;
}