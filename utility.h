#include "kernel.h"

/* Memory set */
void* memset(void* s, int c, size_t n) {
    unsigned char* p = (unsigned char*)s;
    while (n--) {
        *p++ = (unsigned char)c;
    }
    return s;
}

/* Memory copy */
void* memcpy(void* dest, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

/* String length */
size_t strlen(const char* str) {
    size_t len = 0;
    while (str[len] != '\0') {
        len++;
    }
    return len;
}

/* String copy */
char* strcpy(char* dest, const char* src) {
    char* original_dest = dest;
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
    return original_dest;
}

/* String comparison */
bool strcmp(const char* s1, const char* s2) {
    while (*s1 && *s2) {
        if (*s1 != *s2) {
            return false;
        }
        s1++;
        s2++;
    }
    return *s1 == *s2;
}

/* Integer to string conversion */
void itoa(int value, char* str, int base) {
    char* rc;
    char* ptr;
    char* low;
    
    // Check for supported base
    if (base < 2 || base > 36) {
        *str = '\0';
        return;
    }
    
    rc = ptr = str;
    
    // Handle negative numbers
    if (value < 0) {
        *ptr++ = '-';
        value = -value;
    }
    
    low = ptr;
    
    // Convert number
    do {
        *ptr++ = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"[value % base];
        value /= base;
    } while (value > 0);
    
    // Terminate string
    *ptr = '\0';
    
    // Reverse string
    ptr--;
    while (low < ptr) {
        char temp = *low;
        *low++ = *ptr;
        *ptr-- = temp;
    }
}

/* Unsigned long to string conversion */
void ultoa(unsigned long value, char* str, int base) {
    char* rc;
    char* ptr;
    char* low;
    
    // Check for supported base
    if (base < 2 || base > 36) {
        *str = '\0';
        return;
    }
    
    rc = ptr = str;
    
    // Convert number
    do {
        *ptr++ = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"[value % base];
        value /= base;
    } while (value > 0);
    
    // Terminate string
    *ptr = '\0';
    
    // Reverse string
    ptr--;
    while (rc < ptr) {
        char temp = *rc;
        *rc++ = *ptr;
        *ptr-- = temp;
    }
}

/* Character to uppercase */
int toupper(int c) {
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 'A';
    }
    return c;
}


// Convert an integer to a string
void int_to_string(int num, char* str) {
    int i = 0;
    bool is_negative = false;
    
    // Handle 0 explicitly
    if (num == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return;
    }
    
    // Handle negative numbers
    if (num < 0) {
        is_negative = true;
        num = -num;
    }
    
    // Process individual digits
    while (num != 0) {
        int rem = num % 10;
        str[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
        num = num / 10;
    }
    
    // Add negative sign if needed
    if (is_negative)
        str[i++] = '-';
    
    // Add null terminator
    str[i] = '\0';
    
    // Reverse the string
    reverse_string(str, 0, i - 1);
}

// Helper function to reverse a string
void reverse_string(char* str, int start, int end) {
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        start++;
        end--;
    }
}