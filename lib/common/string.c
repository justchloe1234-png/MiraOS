#include "string.h"

char *strchr(const char *str, int c) {
    while (*str) {
        if (*str == (char)c)
            return (char *)str;
        str++;
    }
    if (c == 0)
        return (char *)str;
    return 0;
}

char *strstr(const char *haystack, const char *needle) {
    if (!*needle)
        return (char *)haystack;
    
    while (*haystack) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && *h == *n) {
            h++;
            n++;
        }
        if (!*n)
            return (char *)haystack;
        haystack++;
    }
    return 0;
}

size_t strlen(const char *str) {
    size_t len = 0;
    while (str[len])
        len++;
    return len;
}

int strcmp(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b)
            return *a - *b;
        a++;
        b++;
    }
    return *a - *b;
}

int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i])
            return a[i] - b[i];
        if (a[i] == 0)
            break;
    }
    return 0;
}

char *strcpy(char *dst, const char *src) {
    char *orig = dst;
    while (*src) {
        *dst = *src;
        dst++;
        src++;
    }
    *dst = 0;
    return orig;
}

char *strncpy(char *dst, const char *src, size_t n) {
    char *orig = dst;
    size_t i;
    for (i = 0; i < n && src[i]; i++)
        dst[i] = src[i];
    for (; i < n; i++)
        dst[i] = 0;
    return orig;
}

char *strcat(char *dst, const char *src) {
    char *orig = dst;
    while (*dst)
        dst++;
    while (*src) {
        *dst = *src;
        dst++;
        src++;
    }
    *dst = 0;
    return orig;
}

char *strncat(char *dst, const char *src, size_t n) {
    char *orig = dst;
    while (*dst)
        dst++;
    size_t i;
    for (i = 0; i < n && src[i]; i++) {
        *dst = src[i];
        dst++;
    }
    *dst = 0;
    return orig;
}