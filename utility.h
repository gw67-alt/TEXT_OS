#ifndef UTILITY_H
#define UTILITY_H

#include <stddef.h>

// String functions
size_t strlen(const char* str);
bool strcmp(const char* s1, const char* s2);
bool strncmp(const char* s1, const char* s2, size_t n);
char* strchr(const char* str, char ch);

// Memory functions
void* memcpy(void* dest, const void* src, size_t n);
void* memset(void* s, int c, size_t n);

// Conversion functions
void itoa(int value, char* str, int base);

#endif /* UTILITY_H */

/* Memory allocation functions for our implementation */
void* malloc(size_t size) {
    // This is a very basic memory allocator - not suitable for production
    // For this example, we'll use a fixed buffer
    static uint8_t memory_pool[16384]; // 16KB memory pool
    static size_t next_free = 0;
    
    // Simple bump allocator - no free functionality
    void* ptr = memory_pool + next_free;
    next_free += size;
    
    // Check for out of memory
    if (next_free > sizeof(memory_pool)) {
        return NULL;
    }
    
    return ptr;
}

void free(void* ptr) {
    // This simple allocator doesn't support freeing memory
    // Just a stub for compatibility
    (void)ptr;
}

void* memcpy(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    
    return dest;
}

void* memset(void* s, int c, size_t n) {
    uint8_t* p = (uint8_t*)s;
    
    for (size_t i = 0; i < n; i++) {
        p[i] = (uint8_t)c;
    }
    
    return s;
}

size_t strlen(const char* str) {
    size_t len = 0;
    while (str[len])
        len++;
    return len;
}

bool strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *s1 == *s2;
}

bool strncmp(const char* s1, const char* s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i]) {
            return false;
        }
        if (s1[i] == '\0') {
            break;
        }
    }
    return true;
}

char* strchr(const char* str, char ch) {
    while (*str && *str != ch) {
        str++;
    }
    
    return (*str == ch) ? (char*)str : NULL;
}