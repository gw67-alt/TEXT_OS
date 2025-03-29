#ifndef TERMINAL_HOOKS_H
#define TERMINAL_HOOKS_H

#include "stdlib_hooks.h"
// Terminal buffer constants
#define MAX_COMMAND_LENGTH 80

// ============================================================================
// External Declarations
// ============================================================================

// Declare external command buffer (defined in your terminal code)
extern char command_buffer[];  // Don't specify size in extern declaration
extern bool command_ready;
// Define wrapper functions that connect our standard library hooks
// to your existing terminal implementation

// Make your terminal's command_buffer and command_ready available to our hooks
char* get_command_buffer() {
    extern char command_buffer[MAX_COMMAND_LENGTH];
    return command_buffer;
}

bool* get_command_ready_ptr() {
    extern bool command_ready;
    return &command_ready;
}

// Terminal output function - connects to your existing terminal_putchar
void terminal_putchar_original(char c) {
    extern void terminal_putchar(char c);
    terminal_putchar(c);
}

#endif // TERMINAL_HOOKS_H