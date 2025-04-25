
// ----------------------------------------------------------------------
// Architecture-specific implementation for x86_64
// ----------------------------------------------------------------------
// The va_end macro is used to clean up after using a va_list.
// Here are several ways to implement it for bare metal:

// -------------------------------------------------------------
// 1. Using compiler builtin (most common approach)
// -------------------------------------------------------------

// This relies on the compiler's built-in knowledge of the ABI
#define va_end(ap) __builtin_va_end(ap)

// -------------------------------------------------------------
// 2. Empty implementation (minimal approach)
// -------------------------------------------------------------

// For many architectures, va_end can be implemented as a no-op
// because cleanup isn't strictly necessary
#define va_end_simple(ap) ((void)0)

// -------------------------------------------------------------
// 3. Architecture-specific cleanup (x86_64 example)
// -------------------------------------------------------------

// On x86_64, if we've defined a custom va_list structure
typedef struct {
    unsigned int gp_offset;
    unsigned int fp_offset;
    void* overflow_arg_area;
    void* reg_save_area;
} custom_va_list_struct;

typedef custom_va_list_struct custom_va_list[1];

// Custom va_end implementation for the above structure
inline void custom_va_end(custom_va_list ap) {
    // Clear the structure fields
    ap[0].gp_offset = 0;
    ap[0].fp_offset = 0;
    ap[0].overflow_arg_area = nullptr;
    ap[0].reg_save_area = nullptr;
}

// -------------------------------------------------------------
// 4. ARM architecture example
// -------------------------------------------------------------

// For ARM, va_end is typically simple:
typedef char* arm_va_list;
#define arm_va_end(ap) ((void)(ap = (arm_va_list)0))


// -------------------------------------------------------------
// Explanation of what va_end actually does
// -------------------------------------------------------------

/*
 * The purpose of va_end:
 * 
 * 1. Signals that we're done accessing variable arguments
 * 2. Performs any necessary cleanup of resources
 * 3. May reset internal state used for tracking arguments
 * 4. On some platforms, may perform stack cleanup
 * 
 * In many bare metal environments, va_end is often a no-op (does nothing),
 * but it's still important to include it for portability and correctness.
 * 
 * Failing to call va_end after va_start may cause issues if:
 * - You're using exception handling
 * - You're on platforms with special register usage for varargs
 * - You're using libraries that depend on proper cleanup
 */
// Define our own va_list type
// On x86_64, va_list is typically an array of 1 structure
typedef struct {
    unsigned int gp_offset;    // Offset for the next general-purpose register 
    unsigned int fp_offset;    // Offset for the next floating-point register
    void* overflow_arg_area;   // Pointer to the overflow arguments
    void* reg_save_area;       // Pointer to register save area
} __va_list_struct;

typedef __va_list_struct va_list[1];

// Define our own va_start macro
// This initializes the va_list for accessing variable arguments
#define va_start(ap, param) __builtin_va_start(ap, param)

// Define our own va_arg macro
// This expands to an expression that has the type and value of the next argument
#define va_arg(ap, type) __builtin_va_arg(ap, type)

// Define our own va_end macro
// This cleans up the va_list when done
#define va_end(ap) __builtin_va_end(ap)

// Define our own va_copy macro if needed
// This creates a copy of a va_list
#define va_copy(dest, src) __builtin_va_copy(dest, src)

// ----------------------------------------------------------------------
// Alternative implementation using compiler builtins directly
// ----------------------------------------------------------------------

// For gcc and clang, you can often rely on compiler builtins directly:
// #define va_start(v,l) __builtin_va_start(v,l)
// #define va_end(v) __builtin_va_end(v)
// #define va_arg(v,l) __builtin_va_arg(v,l)
// #define va_copy(d,s) __builtin_va_copy(d,s)

// ----------------------------------------------------------------------
// Simplified implementation for ARM architecture
// ----------------------------------------------------------------------

// For ARM, the implementation would look different:
/*
typedef char* va_list;

#define _INTSIZEOF(n)  ((sizeof(n) + sizeof(int) - 1) & ~(sizeof(int) - 1))

#define va_start(ap, v) (ap = (va_list)&v + _INTSIZEOF(v))
#define va_arg(ap, t)   (*(t*)((ap += _INTSIZEOF(t)) - _INTSIZEOF(t)))
#define va_end(ap)      (ap = (va_list)0)
*/

// ----------------------------------------------------------------------
// Example implementation of sscanf using our custom va_list
// ----------------------------------------------------------------------


/**
 * @brief Check if character is a digit (0-9)
 * @param c Character to check
 * @return True if digit, false otherwise
 */
bool is_digit(char c) {
    return (c >= '0' && c <= '9');
}

/**
 * @brief Check if character is whitespace (space, tab, newline)
 * @param c Character to check
 * @return True if whitespace, false otherwise
 */
bool is_space(char c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r');
}

/**
 * @brief Check if character is hexadecimal digit (0-9, a-f, A-F)
 * @param c Character to check
 * @return True if hex digit, false otherwise
 */
bool is_hex_digit(char c) {
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/**
 * @brief Convert hexadecimal character to integer value
 * @param c Hexadecimal character
 * @return Integer value (0-15)
 */
int hex_to_int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

/**
 * @brief Consume whitespace characters from the input string
 * @param str Pointer to the current position in the string (will be updated)
 */
void consume_whitespace(const char** str) {
    while (**str && is_space(**str)) {
        (*str)++;
    }
}

/**
 * @brief Parse an integer from the input string
 * @param str Pointer to the current position in the string (will be updated)
 * @param value Pointer to store the parsed integer
 * @param base Number base (10 for decimal, 16 for hex, etc.)
 * @return True if parsing succeeded, false otherwise
 */
bool parse_int(const char** str, int* value, int base = 10) {
    const char* s = *str;
    bool negative = false;
    
    // Handle sign
    if (*s == '-') {
        negative = true;
        s++;
    } else if (*s == '+') {
        s++;
    }
    
    // Handle hex prefix
    if (base == 16 && *s == '0' && (*(s+1) == 'x' || *(s+1) == 'X')) {
        s += 2;
    }
    
    // Parse digits
    int result = 0;
    bool any_digits = false;
    
    while (*s) {
        int digit;
        
        if (base == 10 && is_digit(*s)) {
            digit = *s - '0';
        } else if (base == 16 && is_hex_digit(*s)) {
            digit = hex_to_int(*s);
        } else {
            break;
        }
        
        result = result * base + digit;
        any_digits = true;
        s++;
    }
    
    if (!any_digits) {
        return false;
    }
    
    *value = negative ? -result : result;
    *str = s;
    return true;
}

/**
 * @brief Parse a floating-point number from the input string
 * @param str Pointer to the current position in the string (will be updated)
 * @param value Pointer to store the parsed float
 * @return True if parsing succeeded, false otherwise
 */
bool parse_float(const char** str, float* value) {
    const char* s = *str;
    bool negative = false;
    
    // Handle sign
    if (*s == '-') {
        negative = true;
        s++;
    } else if (*s == '+') {
        s++;
    }
    
    // Parse integer part
    float result = 0.0f;
    bool any_digits = false;
    
    while (is_digit(*s)) {
        result = result * 10.0f + (*s - '0');
        any_digits = true;
        s++;
    }
    
    // Parse fractional part
    if (*s == '.') {
        s++;
        float fraction = 0.0f;
        float multiplier = 0.1f;
        
        while (is_digit(*s)) {
            fraction += (*s - '0') * multiplier;
            multiplier *= 0.1f;
            any_digits = true;
            s++;
        }
        
        result += fraction;
    }
    
    // Parse exponential part (e.g., 1.23e-4)
    if ((*s == 'e' || *s == 'E') && any_digits) {
        s++;
        bool exp_negative = false;
        
        if (*s == '-') {
            exp_negative = true;
            s++;
        } else if (*s == '+') {
            s++;
        }
        
        int exponent = 0;
        bool exp_digits = false;
        
        while (is_digit(*s)) {
            exponent = exponent * 10 + (*s - '0');
            exp_digits = true;
            s++;
        }
        
        if (exp_digits) {
            float scale = 1.0f;
            for (int i = 0; i < exponent; i++) {
                scale = exp_negative ? scale * 0.1f : scale * 10.0f;
            }
            result *= scale;
        }
    }
    
    if (!any_digits) {
        return false;
    }
    
    *value = negative ? -result : result;
    *str = s;
    return true;
}

/**
 * @brief Parse a double-precision floating-point number from the input string
 * @param str Pointer to the current position in the string (will be updated)
 * @param value Pointer to store the parsed double
 * @return True if parsing succeeded, false otherwise
 */
bool parse_double(const char** str, double* value) {
    float float_val;
    bool result = parse_float(str, &float_val);
    if (result) {
        *value = static_cast<double>(float_val);
    }
    return result;
}

/**
 * @brief Parse a character from the input string
 * @param str Pointer to the current position in the string (will be updated)
 * @param value Pointer to store the parsed character
 * @return True if parsing succeeded, false otherwise
 */
bool parse_char(const char** str, char* value) {
    if (**str) {
        *value = **str;
        (*str)++;
        return true;
    }
    return false;
}

/**
 * @brief Parse a string from the input string
 * @param str Pointer to the current position in the string (will be updated)
 * @param buffer Buffer to store the parsed string
 * @param max_len Maximum length of the buffer
 * @return True if parsing succeeded, false otherwise
 */
bool parse_string(const char** str, char* buffer, int max_len) {
    const char* s = *str;
    int i = 0;
    
    // Skip leading whitespace
    consume_whitespace(&s);
    
    // Copy non-whitespace characters to buffer
    while (*s && !is_space(*s) && i < max_len - 1) {
        buffer[i++] = *s++;
    }
    
    if (i == 0) {
        return false;
    }
    
    buffer[i] = '\0';
    *str = s;
    return true;
}

/**
 * @brief Custom implementation of sscanf for bare metal environments
 * 
 * @param str Input string to parse
 * @param format Format string similar to standard sscanf
 * @param ... Variable arguments to store parsed values
 * @return Number of successfully parsed items
 */
int sscanf(const char* str, const char* format, ...) {
    int items_matched = 0;
    va_list args;
    va_start(args, format);
    
    while (*format && *str) {
        // Skip whitespace in format string
        if (is_space(*format)) {
            format++;
            continue;
        }
        
        // Match literal characters in format string
        if (*format != '%') {
            consume_whitespace(&str);
            if (*format == *str) {
                format++;
                str++;
            } else {
                break;  // Mismatch
            }
            continue;
        }
        
        // Handle format specifiers
        format++;  // Skip '%'
        
        // Check for width specifier (ignore for simplicity)
        int width = 0;
        while (is_digit(*format)) {
            width = width * 10 + (*format - '0');
            format++;
        }
        
        // Process format specifier
        switch (*format) {
            case 'd': {  // Integer
                int* value = va_arg(args, int*);
                consume_whitespace(&str);
                if (parse_int(&str, value)) {
                    items_matched++;
                } else {
                    va_end(args);
                    return items_matched;
                }
                break;
            }
            
            case 'x': {  // Hexadecimal
                int* value = va_arg(args, int*);
                consume_whitespace(&str);
                if (parse_int(&str, value, 16)) {
                    items_matched++;
                } else {
                    va_end(args);
                    return items_matched;
                }
                break;
            }
            
            case 'f': {  // Float
                float* value = va_arg(args, float*);
                consume_whitespace(&str);
                if (parse_float(&str, value)) {
                    items_matched++;
                } else {
                    va_end(args);
                    return items_matched;
                }
                break;
            }
            
            case 'l': {  // Long or Double
                format++;
                if (*format == 'f') {  // Double
                    double* value = va_arg(args, double*);
                    consume_whitespace(&str);
                    if (parse_double(&str, value)) {
                        items_matched++;
                    } else {
                        va_end(args);
                        return items_matched;
                    }
                }
                break;
            }
            
            case 'c': {  // Character
                char* value = va_arg(args, char*);
                if (parse_char(&str, value)) {
                    items_matched++;
                } else {
                    va_end(args);
                    return items_matched;
                }
                break;
            }
            
            case 's': {  // String
                char* buffer = va_arg(args, char*);
                consume_whitespace(&str);
                if (parse_string(&str, buffer, width > 0 ? width : 1024)) {
                    items_matched++;
                } else {
                    va_end(args);
                    return items_matched;
                }
                break;
            }
        }
        
        format++;
    }
    
    va_end(args);
    return items_matched;
}
