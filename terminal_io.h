#ifndef TERMINAL_IO_H
#define TERMINAL_IO_H

#include <cstddef>  // size_t
#include <cstdint>  // uint16_t
#include <cstdarg>
#include <cstdio>
void print_prog();
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
private:
    static const int SCROLLBACK_BUFFER_HEIGHT = VGA_HEIGHT * 5;
    uint16_t scrollback_buffer[SCROLLBACK_BUFFER_HEIGHT * VGA_WIDTH];
    int scrollback_lines = 0;

    void scroll_screen_internal();
    void put_entry_at(char c, uint8_t color, size_t x, size_t y);
    void put_char(char c);

public:
    TerminalOutput(); // Constructor

    bool show_scrollback_page(int page);
    void restore_screen();
    int get_scrollback_pages();

    TerminalOutput& operator<<(const char* str);
    TerminalOutput& operator<<(char c);
    TerminalOutput& operator<<(int num);
    TerminalOutput& operator<<(unsigned int num);
    TerminalOutput& operator<<(void* ptr);
};

class TerminalInput {
private:
    char input_buffer[MAX_COMMAND_LENGTH];
    bool input_ready = false;
    char command_history[HISTORY_SIZE][MAX_COMMAND_LENGTH];
    int history_count = 0;
    int history_index = -1;

    void setInputReady(const char* buffer);
    void navigateHistory(bool up);
    void clearInputLine();

public:
    TerminalInput(); // Constructor

    TerminalInput& operator>>(char* str);
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