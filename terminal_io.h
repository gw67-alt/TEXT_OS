#ifndef TERMINAL_IO_H
#define TERMINAL_IO_H

#include <cstddef>  // size_t
#include <cstdint>  // uint16_t
#include <cstdarg>
#include <cstdio>

// VGA dimensions (you might want these in a separate vga.h)
static const size_t VGA_WIDTH = 80;
static const size_t VGA_HEIGHT = 25;

// Definitions for MAX_COMMAND_LENGTH and HISTORY_SIZE
static const int MAX_COMMAND_LENGTH = 80;
static const int HISTORY_SIZE = 10;

enum vga_color {
    COLOR_BLACK = 0,
    COLOR_BLUE = 1,
    // ... (rest of your color enum)
    COLOR_WHITE = 15,
};

uint8_t make_color(enum vga_color fg, enum vga_color bg);
uint16_t make_vgaentry(char c, uint8_t color);

class TerminalOutput {
    public:
        TerminalOutput& operator<<(const char* str);
        TerminalOutput& operator<<(char c);
        TerminalOutput& operator<<(int num);
        TerminalOutput& operator<<(unsigned int num);
        TerminalOutput& operator<<(void* ptr);
    };
    
    class TerminalInput {
    public:
    TerminalInput& operator>>(char* str);
    TerminalInput& operator>>(int num);
    };
    
    extern TerminalOutput cout;
    extern TerminalInput cin;
    
    TerminalOutput& get_cout() { 
        return cout; 
    }
    
    TerminalInput& get_cin() { 
        return cin; 
    }


    
    #endif

