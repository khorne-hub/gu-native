/* The C library's string functions for the game code built with Zig's C compiler on the phone (see math.h): from
 * Android's libc.so at run time. */
#ifndef MHGU_ANDROID_STRING_H
#define MHGU_ANDROID_STRING_H
#include <stddef.h>
void *memcpy(void *restrict dest, const void *restrict src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
void *memchr(const void *s, int c, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *restrict dest, const char *restrict src);
char *strncpy(char *restrict dest, const char *restrict src, size_t n);
char *strchr(const char *s, int c);
#endif
