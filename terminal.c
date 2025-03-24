#include "kernel.h"

/* VGA Text Mode Buffer */
static const size_t VGA_WIDTH = 80;
static const size_t VGA_HEIGHT = 25;
static uint16_t* const VGA_MEMORY = (uint16_t*)0xB8000;

/* Terminal State */
static size_t terminal_row;
static size_t terminal_column;
static uint8_t terminal_color;
static uint16_t* terminal_buffer;

/* Create color attribute */
uint8_t make_color(enum vga_color fg, enum vga_color bg) {
    return fg | bg << 4;
}

/* Create VGA character entry */
static inline uint16_t make_vga_entry(char c, uint8_t color) {
    return (uint16_t)c | (uint16_t)color << 8;
}

/* Initialize terminal */
void terminal_initialize(void) {
    terminal_row = 0;
    terminal_column = 0;
    terminal_color = make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_buffer = VGA_MEMORY;

    // Clear screen
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            terminal_buffer[index] = make_vga_entry(' ', terminal_color);
        }
    }
}

/* Set terminal color */
void terminal_setcolor(uint8_t color) {
    terminal_color = color;
}

/* Put character at current cursor position */
void terminal_putchar(char c) {
    // Handle newline
    if (c == '\n') {
        terminal_column = 0;
        terminal_row++;
        return;
    }

    // Scroll if needed
    if (terminal_row >= VGA_HEIGHT) {
        // Scroll up
        for (size_t y = 1; y < VGA_HEIGHT; y++) {
            for (size_t x = 0; x < VGA_WIDTH; x++) {
                const size_t source_index = y * VGA_WIDTH + x;
                const size_t dest_index = (y - 1) * VGA_WIDTH + x;
                terminal_buffer[dest_index] = terminal_buffer[source_index];
            }
        }

        // Clear last row
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = (VGA_HEIGHT - 1) * VGA_WIDTH + x;
            terminal_buffer[index] = make_vga_entry(' ', terminal_color);
        }

        terminal_row = VGA_HEIGHT - 1;
    }

    // Place character
    const size_t index = terminal_row * VGA_WIDTH + terminal_column;
    terminal_buffer[index] = make_vga_entry(c, terminal_color);

    // Move cursor
    terminal_column++;
    if (terminal_column == VGA_WIDTH) {
        terminal_column = 0;
        terminal_row++;
    }
}

/* Write buffer to terminal */
void terminal_write(const char* data, size_t size) {
    for (size_t i = 0; i < size; i++) {
        terminal_putchar(data[i]);
    }
}

/* Write null-terminated string */
void terminal_writestring(const char* data) {
    terminal_write(data, strlen(data));
}

/* Clear terminal */
void terminal_clear(void) {
    terminal_initialize();
}