#ifndef IOSTREAM_WRAPPER_H
#define IOSTREAM_WRAPPER_H

#include "stdlib_hooks.h"  // Include this to get the 'extern bool use_hex' declaration

// This file provides familiar C++ iostream-style syntax for your kernel
// by wrapping around our terminal I/O implementation

// Basic classes to mimic std::cin and std::cout
class InputStream {
public:
    InputStream& operator>>(char* str) {
        kout >> str;
        return *this;
    }
    
    InputStream& operator>>(int& num) {
        kout >> num;
        return *this;
    }
    
    InputStream& operator>>(unsigned int& num) {
        kout >> num;
        return *this;
    }
};

class OutputStream {
public:
    OutputStream& operator<<(const char* str) {
        kout << str;
        return *this;
    }
    
    OutputStream& operator<<(char c) {
        kout << c;
        return *this;
    }
    
    OutputStream& operator<<(int num) {
        kout << num;
        return *this;
    }
    
    OutputStream& operator<<(unsigned int num) {
        kout << num;
        return *this;
    }
    
    OutputStream& operator<<(long num) {
        kout << num;
        return *this;
    }
    
    OutputStream& operator<<(unsigned long num) {
        kout << num;
        return *this;
    }
    
    OutputStream& operator<<(void* ptr) {
        kout << ptr;
        return *this;
    }
    
    // Support for manipulators like endl, hex, dec
    typedef OutputStream& (*ManipulatorFunc)(OutputStream&);
    OutputStream& operator<<(ManipulatorFunc func) {
        return func(*this);
    }
};

// Create global iostream objects
extern OutputStream cout;
extern InputStream cin;

// Define stream manipulators
inline OutputStream& endl(OutputStream& stream) {
    stream << '\n';
    return stream;
}

inline OutputStream& hex(OutputStream& stream) {
    // Access the global variable directly - it's declared as extern in stdlib_hooks.h
    use_hex = true;
    return stream;
}

inline OutputStream& dec(OutputStream& stream) {
    // Access the global variable directly - it's declared as extern in stdlib_hooks.h
    use_hex = false;
    return stream;
}

#endif // IOSTREAM_WRAPPER_H