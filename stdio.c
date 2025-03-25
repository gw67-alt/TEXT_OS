/* stdio.c - Basic standard I/O functions for kernel */
#include "stdio.h"
#include "io.h"  // For terminal_writestring, terminal_writechar
#include <stdarg.h>
#include <stddef.h>
#include <stdbool.h>

// Helper function prototypes
static int print_number(char* buffer, size_t maxlen, unsigned int num, int base, bool is_signed, bool uppercase, int width, bool zero_pad, size_t* pos);
static int print_string(char* buffer, size_t maxlen, const char* str, int width, size_t* pos);

// Main format function - used by all other printf-family functions
int vsnprintf(char* buffer, size_t maxlen, const char* format, va_list args) {
    if (!buffer || !format || maxlen == 0)
        return 0;
    
    size_t pos = 0;
    char ch;
    
    // Leave room for null terminator
    maxlen--;
    
    while ((ch = *format++) != 0) {
        if (ch != '%') {
            // Regular character
            if (pos < maxlen)
                buffer[pos] = ch;
            pos++;
            continue;
        }
        
        // Process format specifier
        ch = *format++;
        if (ch == 0)
            break;  // End of string after '%'
        
        // Process flags
        int width = 0;
        bool zero_pad = false;
        
        if (ch == '0') {
            zero_pad = true;
            ch = *format++;
        }
        
        // Process width
        while (ch >= '0' && ch <= '9') {
            width = width * 10 + (ch - '0');
            ch = *format++;
        }
        
        // Process format character
        switch (ch) {
            case 'c': {
                // Character
                char c = (char)va_arg(args, int);
                if (pos < maxlen)
                    buffer[pos] = c;
                pos++;
                break;
            }
            
            case 's': {
                // String
                const char* str = va_arg(args, const char*);
                pos += print_string(buffer, maxlen, str, width, &pos);
                break;
            }
            
            case 'd':
            case 'i': {
                // Signed decimal
                int value = va_arg(args, int);
                pos += print_number(buffer, maxlen, value, 10, true, false, width, zero_pad, &pos);
                break;
            }
            
            case 'u': {
                // Unsigned decimal
                unsigned int value = va_arg(args, unsigned int);
                pos += print_number(buffer, maxlen, value, 10, false, false, width, zero_pad, &pos);
                break;
            }
            
            case 'x': {
                // Hexadecimal lowercase
                unsigned int value = va_arg(args, unsigned int);
                pos += print_number(buffer, maxlen, value, 16, false, false, width, zero_pad, &pos);
                break;
            }
            
            case 'X': {
                // Hexadecimal uppercase
                unsigned int value = va_arg(args, unsigned int);
                pos += print_number(buffer, maxlen, value, 16, false, true, width, zero_pad, &pos);
                break;
            }
            
            case 'p': {
                // Pointer (hexadecimal)
                void* ptr = va_arg(args, void*);
                if (pos < maxlen)
                    buffer[pos] = '0';
                pos++;
                if (pos < maxlen)
                    buffer[pos] = 'x';
                pos++;
                pos += print_number(buffer, maxlen, (unsigned int)ptr, 16, false, false, width, true, &pos);
                break;
            }
            
            case '%': {
                // Literal '%'
                if (pos < maxlen)
                    buffer[pos] = '%';
                pos++;
                break;
            }
            
            default: {
                // Unknown format, just output as-is
                if (pos < maxlen)
                    buffer[pos] = '%';
                pos++;
                if (pos < maxlen)
                    buffer[pos] = ch;
                pos++;
                break;
            }
        }
    }
    
    // Ensure null-termination
    if (maxlen > 0)
        buffer[pos < maxlen ? pos : maxlen] = '\0';
    
    return pos;
}

// Helper function to print number
static int print_number(char* buffer, size_t maxlen, unsigned int num, int base, bool is_signed, bool uppercase, int width, bool zero_pad, size_t* pos) {
    static const char* digits_lower = "0123456789abcdef";
    static const char* digits_upper = "0123456789ABCDEF";
    const char* digits = uppercase ? digits_upper : digits_lower;
    
    char tmp[32];  // Temp buffer for number conversion
    int i = 0;
    size_t len = 0;
    unsigned int n = num;
    int sign = 0;
    
    // Handle sign
    if (is_signed && (int)num < 0) {
        sign = 1;
        n = -((int)num);
    }
    
    // Generate digits in reverse order
    do {
        tmp[i++] = digits[n % base];
        n /= base;
    } while (n > 0);
    
    // Calculate total length
    len = i;
    if (sign)
        len++;
    
    // Add padding to reach width
    if (width > 0 && len < (size_t)width) {
        size_t pad = width - len;
        if (zero_pad) {
            if (sign) {
                // Handle sign with zero padding
                if (*pos < maxlen)
                    buffer[*pos] = '-';
                (*pos)++;
                sign = 0;  // Mark sign as handled
            }
            
            for (size_t j = 0; j < pad; j++) {
                if (*pos < maxlen)
                    buffer[*pos] = '0';
                (*pos)++;
            }
        } else {
            for (size_t j = 0; j < pad; j++) {
                if (*pos < maxlen)
                    buffer[*pos] = ' ';
                (*pos)++;
            }
        }
    }
    
    // Add sign if needed
    if (sign) {
        if (*pos < maxlen)
            buffer[*pos] = '-';
        (*pos)++;
    }
    
    // Add digits in correct order
    while (i > 0) {
        i--;
        if (*pos < maxlen)
            buffer[(*pos)] = tmp[i];
        (*pos)++;
    }
    
    return len;
}

// Helper function to print string
static int print_string(char* buffer, size_t maxlen, const char* str, int width, size_t* pos) {
    if (!str)
        str = "(null)";
    
    size_t len = 0;
    const char* s = str;
    
    // Calculate string length
    while (*s)
        len++, s++;
    
    // Padding if needed
    if (width > 0 && len < (size_t)width) {
        size_t pad = width - len;
        for (size_t i = 0; i < pad; i++) {
            if (*pos < maxlen)
                buffer[*pos] = ' ';
            (*pos)++;
        }
    }
    
    // Copy string
    s = str;
    while (*s) {
        if (*pos < maxlen)
            buffer[*pos] = *s;
        (*pos)++;
        s++;
    }
    
    return len;
}

// Format a string and store it in a buffer
int sprintf(char* buffer, const char* format, ...) {
    va_list args;
    int ret;
    
    va_start(args, format);
    ret = vsnprintf(buffer, (size_t)-1, format, args);
    va_end(args);
    
    return ret;
}

// Format a string with specified maximum length
int snprintf(char* buffer, size_t n, const char* format, ...) {
    va_list args;
    int ret;
    
    va_start(args, format);
    ret = vsnprintf(buffer, n, format, args);
    va_end(args);
    
    return ret;
}

// Format a string using va_list
int vsprintf(char* buffer, const char* format, va_list args) {
    return vsnprintf(buffer, (size_t)-1, format, args);
}
// Completely rewired printf that bypasses terminal_writestring
int printf(const char* format, ...) {
    static char buffer[2048];
    
    // Clear the buffer first
    for (size_t i = 0; i < sizeof(buffer); i++) {
        buffer[i] = 0;
    }
    
    va_list args;
    int ret;
    
    // Format the string
    va_start(args, format);
    ret = vsnprintf(buffer, sizeof(buffer) - 1, format, args);
    va_end(args);
    
    // Ensure null termination
    buffer[sizeof(buffer) - 1] = '\0';
    
    // Directly output each character using terminal_putchar
    // This ensures newlines are properly processed
    for (size_t i = 0; buffer[i] != '\0'; i++) {
        // Directly call terminal_putchar for each character including newlines
        terminal_putchar(buffer[i]);
    }
    
    return ret;
}

// Custom implementation of strstr function
const char* strstr(const char* haystack, const char* needle) {
    if (!haystack || !needle || !*needle) {
        return haystack; // Return haystack for NULL pointers or empty needle
    }
    
    while (*haystack) {
        const char* h = haystack;
        const char* n = needle;
        
        while (*h && *n && *h == *n) {
            h++;
            n++;
        }
        
        if (!*n) { // We found the entire needle
            return haystack;
        }
        
        haystack++;
    }
    
    return NULL; // Needle not found
}