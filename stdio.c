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

int printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    int count = 0;
    
    while (*format != '\0') {
        if (*format == '%') {
            format++;
            
            // Flag parsing
            bool left_justify = false;  // '-' flag
            bool plus_sign = false;     // '+' flag
            bool space_prefix = false;  // ' ' flag
            bool zero_pad = false;      // '0' flag
            bool alternate_form = false; // '#' flag
            
            // Parse flags
            bool parsing_flags = true;
            while (parsing_flags) {
                switch (*format) {
                    case '-': left_justify = true; format++; break;
                    case '+': plus_sign = true; format++; break;
                    case ' ': space_prefix = true; format++; break;
                    case '0': zero_pad = true; format++; break;
                    case '#': alternate_form = true; format++; break;
                    default: parsing_flags = false; break;
                }
            }
            
            // Width parsing
            int width = 0;
            if (*format == '*') {
                // Width from argument
                width = va_arg(args, int);
                if (width < 0) {
                    left_justify = true;
                    width = -width;
                }
                format++;
            } else while (*format >= '0' && *format <= '9') {
                width = width * 10 + (*format - '0');
                format++;
            }
            
            // Precision parsing
            int precision = -1;  // Default (unspecified)
            if (*format == '.') {
                format++;
                precision = 0;  // Default for specified but empty precision
                if (*format == '*') {
                    // Precision from argument
                    precision = va_arg(args, int);
                    if (precision < 0) precision = -1;  // Negative means unspecified
                    format++;
                } else while (*format >= '0' && *format <= '9') {
                    precision = precision * 10 + (*format - '0');
                    format++;
                }
            }
            
            // Length modifier parsing
            enum {
                LENGTH_NONE,
                LENGTH_HH,  // char
                LENGTH_H,   // short
                LENGTH_L,   // long
                LENGTH_LL,  // long long
                LENGTH_Z,   // size_t
                LENGTH_J,   // intmax_t
                LENGTH_T    // ptrdiff_t
            } length = LENGTH_NONE;
            
            switch (*format) {
                case 'h':
                    format++;
                    if (*format == 'h') {
                        length = LENGTH_HH;
                        format++;
                    } else {
                        length = LENGTH_H;
                    }
                    break;
                case 'l':
                    format++;
                    if (*format == 'l') {
                        length = LENGTH_LL;
                        format++;
                    } else {
                        length = LENGTH_L;
                    }
                    break;
                case 'z':
                    length = LENGTH_Z;
                    format++;
                    break;
                case 'j':
                    length = LENGTH_J;
                    format++;
                    break;
                case 't':
                    length = LENGTH_T;
                    format++;
                    break;
            }
            
            // Format specifier
            char buffer[32];  // For number conversion
            const char* str = NULL;
            int str_len = 0;
            int num_len = 0;
            bool is_upper = false;
            bool is_signed = false;
            int base = 10;
            unsigned long long int unum = 0;
            long long int snum = 0;
            
            // Process format specifier
            switch (*format) {
                case 's': {
                    str = va_arg(args, const char*);
                    if (str == NULL) str = "(null)";
                    
                    // Calculate string length
                    const char* s = str;
                    str_len = 0;
                    while (*s) {
                        str_len++;
                        s++;
                    }
                    
                    // Apply precision to truncate if needed
                    if (precision >= 0 && str_len > precision) {
                        str_len = precision;
                    }
                    break;
                }
                case 'c': {
                    buffer[0] = (char)va_arg(args, int);
                    str = buffer;
                    str_len = 1;
                    break;
                }
                case 'd':
                case 'i': {
                    is_signed = true;
                    base = 10;
                    // Get the argument according to length modifier
                    switch (length) {
                        case LENGTH_HH: snum = (signed char)va_arg(args, int); break;
                        case LENGTH_H:  snum = (short)va_arg(args, int); break;
                        case LENGTH_L:  snum = va_arg(args, long); break;
                        case LENGTH_LL: snum = va_arg(args, long long); break;
                        case LENGTH_Z:  snum = va_arg(args, size_t); break;
                        case LENGTH_J:  snum = va_arg(args, intmax_t); break;
                        case LENGTH_T:  snum = va_arg(args, ptrdiff_t); break;
                        default:        snum = va_arg(args, int); break;
                    }
                    
                    // Handle sign
                    bool negative = false;
                    if (snum < 0) {
                        unum = -snum;
                        negative = true;
                    } else {
                        unum = snum;
                    }
                    
                    // Convert to string
                    if (unum == 0) {
                        buffer[0] = '0';
                        num_len = 1;
                    } else {
                        num_len = 0;
                        while (unum > 0) {
                            buffer[num_len++] = '0' + (unum % 10);
                            unum /= 10;
                        }
                    }
                    
                    // Add sign or space
                    char sign_char = 0;
                    if (negative) {
                        sign_char = '-';
                    } else if (plus_sign) {
                        sign_char = '+';
                    } else if (space_prefix) {
                        sign_char = ' ';
                    }
                    
                    // Print the number
                    int pad_len = width - num_len - (sign_char ? 1 : 0);
                    
                    // Padding before sign/number if right-aligned with spaces
                    if (!left_justify && !zero_pad && pad_len > 0) {
                        for (int i = 0; i < pad_len; i++) {
                            terminal_putchar(' ');
                            count++;
                        }
                        pad_len = 0;
                    }
                    
                    // Print sign if needed
                    if (sign_char) {
                        terminal_putchar(sign_char);
                        count++;
                    }
                    
                    // Padding after sign if zero-padded
                    if (!left_justify && zero_pad && pad_len > 0) {
                        for (int i = 0; i < pad_len; i++) {
                            terminal_putchar('0');
                            count++;
                        }
                    }
                    
                    // Print the digits in reverse order
                    for (int i = num_len - 1; i >= 0; i--) {
                        terminal_putchar(buffer[i]);
                        count++;
                    }
                    
                    // Padding after number if left-aligned
                    if (left_justify && pad_len > 0) {
                        for (int i = 0; i < pad_len; i++) {
                            terminal_putchar(' ');
                            count++;
                        }
                    }
                    
                    format++;
                    continue;  // Skip the default handling
                }
                case 'u': {
                    is_signed = false;
                    base = 10;
                    goto handle_unsigned;
                }
                case 'o': {
                    is_signed = false;
                    base = 8;
                    goto handle_unsigned;
                }
                case 'x': {
                    is_signed = false;
                    base = 16;
                    is_upper = false;
                    goto handle_unsigned;
                }
                case 'X': {
                    is_signed = false;
                    base = 16;
                    is_upper = true;
                    goto handle_unsigned;
                }
                case 'p': {
                    is_signed = false;
                    base = 16;
                    is_upper = false;
                    unum = (uintptr_t)va_arg(args, void*);
                    alternate_form = true;  // Force 0x prefix
                    zero_pad = true;        // Usually padded with zeros
                    width = width > 8 ? width : 8;  // Minimum width for pointers
                    goto format_unsigned;
                }
handle_unsigned:
                    // Get the argument according to length modifier
                    switch (length) {
                        case LENGTH_HH: unum = (unsigned char)va_arg(args, unsigned int); break;
                        case LENGTH_H:  unum = (unsigned short)va_arg(args, unsigned int); break;
                        case LENGTH_L:  unum = va_arg(args, unsigned long); break;
                        case LENGTH_LL: unum = va_arg(args, unsigned long long); break;
                        case LENGTH_Z:  unum = va_arg(args, size_t); break;
                        case LENGTH_J:  unum = va_arg(args, uintmax_t); break;
                        case LENGTH_T:  unum = va_arg(args, ptrdiff_t); break;
                        default:        unum = va_arg(args, unsigned int); break;
                    }
format_unsigned:
                    // Convert to string
                    if (unum == 0) {
                        buffer[0] = '0';
                        num_len = 1;
                    } else {
                        const char* digits = is_upper ? "0123456789ABCDEF" : "0123456789abcdef";
                        num_len = 0;
                        while (unum > 0) {
                            buffer[num_len++] = digits[unum % base];
                            unum /= base;
                        }
                    }
                    
                    // Calculate prefix
                    const char* prefix = "";
                    int prefix_len = 0;
                    if (alternate_form && base == 16 && num_len > 0 && buffer[0] != '0') {
                        prefix = is_upper ? "0X" : "0x";
                        prefix_len = 2;
                    } else if (alternate_form && base == 8 && (num_len == 0 || buffer[0] != '0')) {
                        prefix = "0";
                        prefix_len = 1;
                    }
                    
                    // Calculate padding
                    int pad_len = width - num_len - prefix_len;
                    
                    // Padding before prefix/number if right-aligned with spaces
                    if (!left_justify && !zero_pad && pad_len > 0) {
                        for (int i = 0; i < pad_len; i++) {
                            terminal_putchar(' ');
                            count++;
                        }
                        pad_len = 0;
                    }
                    
                    // Print prefix if needed
                    for (int i = 0; i < prefix_len; i++) {
                        terminal_putchar(prefix[i]);
                        count++;
                    }
                    
                    // Padding after prefix if zero-padded
                    if (!left_justify && zero_pad && pad_len > 0) {
                        for (int i = 0; i < pad_len; i++) {
                            terminal_putchar('0');
                            count++;
                        }
                    }
                    
                    // Print the digits in reverse order
                    for (int i = num_len - 1; i >= 0; i--) {
                        terminal_putchar(buffer[i]);
                        count++;
                    }
                    
                    // Padding after number if left-aligned
                    if (left_justify && pad_len > 0) {
                        for (int i = 0; i < pad_len; i++) {
                            terminal_putchar(' ');
                            count++;
                        }
                    }
                    
                    format++;
                    continue;  // Skip the default handling
                case '%': {
                    terminal_putchar('%');
                    count++;
                    format++;
                    continue;
                }
                case 'n': {
                    // Store the number of characters written so far
                    switch (length) {
                        case LENGTH_HH: *va_arg(args, signed char*) = count; break;
                        case LENGTH_H:  *va_arg(args, short*) = count; break;
                        case LENGTH_L:  *va_arg(args, long*) = count; break;
                        case LENGTH_LL: *va_arg(args, long long*) = count; break;
                        case LENGTH_Z:  *va_arg(args, size_t*) = count; break;
                        case LENGTH_J:  *va_arg(args, intmax_t*) = count; break;
                        case LENGTH_T:  *va_arg(args, ptrdiff_t*) = count; break;
                        default:        *va_arg(args, int*) = count; break;
                    }
                    format++;
                    continue;
                }
                default: {
                    // Unknown format specifier, just output it
                    terminal_putchar('%');
                    terminal_putchar(*format);
                    count += 2;
                    format++;
                    continue;
                }
            }
            
            // Common code for formats that produce a string (s, c)
            if (str != NULL) {
                // Calculate padding
                int pad_len = width - str_len;
                
                // Padding before string if right-aligned
                if (!left_justify && pad_len > 0) {
                    char pad_char = zero_pad ? '0' : ' ';
                    for (int i = 0; i < pad_len; i++) {
                        terminal_putchar(pad_char);
                        count++;
                    }
                }
                
                // Print string, respecting precision limit
                for (int i = 0; i < str_len; i++) {
                    terminal_putchar(str[i]);
                    count++;
                }
                
                // Padding after string if left-aligned
                if (left_justify && pad_len > 0) {
                    for (int i = 0; i < pad_len; i++) {
                        terminal_putchar(' ');
                        count++;
                    }
                }
            }
            
        } else if (*format == '\n') {
            // Handle newline specially
            terminal_putchar('\n');
            count++; // Changed from count+=2 to count++ to correctly count newlines as one character
        } else {
            terminal_putchar(*format);
            count++;
        }
        
        format++;
    }
    
    va_end(args);
    return count;
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