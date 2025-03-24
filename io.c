#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include "io.h"

// Output byte to I/O port
void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

// Input byte from I/O port
uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// Output word (16 bits) to I/O port
void outw(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

// Input word (16 bits) from I/O port
uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// Output long (32 bits) to I/O port
void outl(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

// Input long (32 bits) from I/O port
uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}


int sprintf(char* str, const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    char* s = str;
    char buffer[32]; // For number conversions
    
    while (*format) {
        // Check for format specifier
        if (*format != '%') {
            *str++ = *format++;
            continue;
        }
        
        // Skip '%'
        format++;
        
        // Handle format flags and width
        bool pad_zero = false;
        int width = 0;
        
        // Check for zero padding
        if (*format == '0') {
            pad_zero = true;
            format++;
        }
        
        // Check for width specification
        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (*format - '0');
            format++;
        }
        
        // Handle format specifiers
        switch (*format) {
            case 'd': {
                // Decimal integer
                int val = va_arg(args, int);
                itoa(val, buffer, 10);
                
                // Apply padding if needed
                int len = strlen(buffer);
                if (width > len && pad_zero) {
                    for (int i = 0; i < width - len; i++) {
                        *str++ = '0';
                    }
                }
                
                // Copy number
                char* p = buffer;
                while (*p) {
                    *str++ = *p++;
                }
                break;
            }
            case 'x': {
                // Hexadecimal (lowercase)
                int val = va_arg(args, int);
                itoa(val, buffer, 16);
                
                // Apply padding if needed
                int len = strlen(buffer);
                if (width > len && pad_zero) {
                    for (int i = 0; i < width - len; i++) {
                        *str++ = '0';
                    }
                }
                
                // Copy number
                char* p = buffer;
                while (*p) {
                    *str++ = *p++;
                }
                break;
            }
            case 'X': {
                // Hexadecimal (uppercase)
                int val = va_arg(args, int);
                itoa(val, buffer, 16);
                
                // Apply padding if needed
                int len = strlen(buffer);
                if (width > len && pad_zero) {
                    for (int i = 0; i < width - len; i++) {
                        *str++ = '0';
                    }
                }
                
                // Convert to uppercase and copy
                char* p = buffer;
                while (*p) {
                    *str++ = toupper(*p++);
                }
                break;
            }
            case 's': {
                // String
                char* val = va_arg(args, char*);
                if (val == NULL) val = "(null)";
                
                while (*val) {
                    *str++ = *val++;
                }
                break;
            }
            case '%':
                // Literal '%'
                *str++ = '%';
                break;
            default:
                // Unknown format specifier, copy literally
                *str++ = '%';
                *str++ = *format;
        }
        
        format++;
    }
    
    *str = '\0';
    va_end(args);
    return str - s;
}