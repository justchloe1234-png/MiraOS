#pragma once

#include <stddef.h>

char *strchr(const char *str, int c);
char *strstr(const char *haystack, const char *needle);
size_t strlen(const char *str);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
char *strcat(char *dst, const char *src);
char *strncat(char *dst, const char *src, size_t n);
void *memset(void *dst, int value, size_t len);