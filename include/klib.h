#ifndef KLIB_H
#define KLIB_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

size_t strlen (const char *str);
int strcmp (const char *s1, const char *s2);
void *memset (void *s, int c, size_t n);

void terminal_putchar (char c);

uint64_t rdtsc (void);

int printk (const char *format, ...);

#endif
