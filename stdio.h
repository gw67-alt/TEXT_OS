/* stdio.h - Basic standard I/O functions for kernel */
#ifndef _KERNEL_STDIO_H
#define _KERNEL_STDIO_H

#include <stdarg.h>
#include <stddef.h>

/* Print a formatted string to the terminal */
int printf(const char* format, ...);

/* Format a string and store it in a buffer */
int sprintf(char* buffer, const char* format, ...);

/* Format a string with a specified maximum length */
int snprintf(char* buffer, size_t n, const char* format, ...);

/* Format a string using a va_list */
int vsprintf(char* buffer, const char* format, va_list args);

/* Format a string with a specified maximum length using a va_list */
int vsnprintf(char* buffer, size_t n, const char* format, va_list args);

#endif /* _KERNEL_STDIO_H */