#ifndef IOSTREAM_WRAPPER_H
#define IOSTREAM_WRAPPER_H

#include "types.h"
#include "terminal_hooks.h"
#include "stdlib_hooks.h"
// Enhanced output class with scrolling capabilities
class TerminalOutput {
private:
    static const int SCROLLBACK_BUFFER_HEIGHT = VGA_HEIGHT * 5;  // Store 5 screens worth of scrollback
    uint16_t scrollback_buffer[SCROLLBACK_BUFFER_HEIGHT * VGA_WIDTH];
    int scrollback_lines = 0;  // Number of lines in the scrollback buffer

    // Helper function to scroll the screen up by one line
    void scroll_screen_internal();
    
    // Helper function to put a character at a specific position
    void put_entry_at(char c, uint8_t color, size_t x, size_t y);
    
    // Enhanced putchar function with scrolling
    void put_char(char c);

public:
    TerminalOutput();
    
    // Show a specific page from the scrollback buffer (0 is newest, scrollback_pages-1 is oldest)
    bool show_scrollback_page(int page);
    
    // Restore the screen after scrollback viewing
    void restore_screen();
    
    // Get the number of pages available in scrollback
    int get_scrollback_pages();
    
    // Standard output operators
    TerminalOutput& operator<<(const char* str);
    TerminalOutput& operator<<(char c);
    TerminalOutput& operator<<(int num);
    TerminalOutput& operator<<(unsigned int num);
    TerminalOutput& operator<<(void* ptr);
};

// Input class for terminal input
class TerminalInput {
private:
    static const int HISTORY_SIZE = 10;
    char input_buffer[MAX_COMMAND_LENGTH];
    bool input_ready = false;
    
    // Command history implementation
    char command_history[HISTORY_SIZE][MAX_COMMAND_LENGTH];
    int history_count = 0;
    int history_index = -1;  // -1 means current input, not from history

public:
    TerminalInput();
    
    // Called from the keyboard handler when Enter is pressed
    void setInputReady(const char* buffer);
    
    // Handle up/down keys for history navigation
    void navigateHistory(bool up);
    
    // Clear the current input line (helper function)
    void clearInputLine();
    
    // Standard input operation with waiting
    TerminalInput& operator>>(char* str);
};

// Global instances
extern TerminalOutput cout;
extern TerminalInput cin;

// Initialize the terminal I/O system
void init_terminal_io();

#endif // IOSTREAM_WRAPPER_H
