#ifndef STDLIB_HOOKS_H
#define STDLIB_HOOKS_H

#include <cstddef>
#include <cstdint>
#include <new>  // For placement new

// ============================================================================
// Common Constants
// ============================================================================

// Terminal buffer constants
#define MAX_COMMAND_LENGTH 80

// ============================================================================
// External Declarations
// ============================================================================

// Declare external command buffer (defined in your terminal code)
extern char command_buffer[MAX_COMMAND_LENGTH];
extern bool command_ready;

// ============================================================================
// Standard C Library Function Overrides
// ============================================================================

// Memory operations
extern "C" {
    // String functions
    void* memcpy(void* dest, const void* src, size_t n);
    void* memmove(void* dest, const void* src, size_t n);
    void* memset(void* dest, int val, size_t n);
    int memcmp(const void* s1, const void* s2, size_t n);
    
    // String operations
    char* strcpy(char* dest, const char* src);
    char* strncpy(char* dest, const char* src, size_t n);
    char* strcat(char* dest, const char* src);
    char* strncat(char* dest, const char* src, size_t n);
    size_t strlen(const char* str);
    int strcmp(const char* s1, const char* s2);
    int strncmp(const char* s1, const char* s2, size_t n);
    char* strchr(const char* s, int c);
    char* strrchr(const char* s, int c);
    
    // Printing functions
    int printf(const char* format, ...);
    int sprintf(char* str, const char* format, ...);
    int snprintf(char* str, size_t size, const char* format, ...);
    
    // Memory allocation (simple implementations)
    void* malloc(size_t size);
    void free(void* ptr);
    void* calloc(size_t num, size_t size);
    void* realloc(void* ptr, size_t size);
}

// ============================================================================
// Global State Variables
// ============================================================================

// Output format state
extern bool use_hex; // Formatting state - true for hexadecimal output, false for decimal

// ============================================================================
// C++ Standard Library Hooks
// ============================================================================

// Basic C++ operator overloads for memory management
void* operator new(size_t size);
void* operator new[](size_t size);
void operator delete(void* ptr) noexcept;
void operator delete[](void* ptr) noexcept;

// Implement placement new (this is actually just a declaration; implementation is compiler-intrinsic)
// void* operator new(size_t, void* ptr) noexcept;

// ============================================================================
// Implementation of a simple heap allocator for the kernel
// ============================================================================

class KernelHeap {
private:
    // Simple memory tracking structure (just for demonstration)
    struct MemoryBlock {
        size_t size;
        bool used;
        MemoryBlock* next;
    };
    
    static const size_t HEAP_SIZE = 1024 * 1024; // 1MB heap
    static uint8_t heap_space[HEAP_SIZE];
    static MemoryBlock* free_list;
    
    static void init();
    
public:
    static void* allocate(size_t size);
    static void deallocate(void* ptr);
    static void* reallocate(void* ptr, size_t size);
};

// ============================================================================
// Implementation of C++ iostream-like functionality
// ============================================================================

// Forward declaration of terminal I/O classes
class TerminalIO;
extern TerminalIO kout;

// Basic stream class
class TerminalIO {
public:
    // Output functions
    TerminalIO& operator<<(const char* str);
    TerminalIO& operator<<(char c);
    TerminalIO& operator<<(int num);
    TerminalIO& operator<<(unsigned int num);
    TerminalIO& operator<<(long num);
    TerminalIO& operator<<(unsigned long num);
    TerminalIO& operator<<(void* ptr);
    
    // Input functions
    TerminalIO& operator>>(char* str);
    TerminalIO& operator>>(int& num);
    TerminalIO& operator>>(unsigned int& num);
    
    // Special actions (like std::endl)
    typedef TerminalIO& (*ManipulatorFunc)(TerminalIO&);
    TerminalIO& operator<<(ManipulatorFunc func);
};

// Stream manipulators
TerminalIO& endl(TerminalIO& stream);
TerminalIO& hex(TerminalIO& stream);
TerminalIO& dec(TerminalIO& stream);

#endif // STDLIB_HOOKS_H