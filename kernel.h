#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* VGA Text Mode Color Codes */
enum vga_color {
    VGA_COLOR_BLACK = 0,
    VGA_COLOR_BLUE = 1,
    VGA_COLOR_GREEN = 2,
    VGA_COLOR_CYAN = 3,
    VGA_COLOR_RED = 4,
    VGA_COLOR_MAGENTA = 5,
    VGA_COLOR_BROWN = 6,
    VGA_COLOR_LIGHT_GREY = 7,
    VGA_COLOR_DARK_GREY = 8,
    VGA_COLOR_LIGHT_BLUE = 9,
    VGA_COLOR_LIGHT_GREEN = 10,
    VGA_COLOR_LIGHT_CYAN = 11,
    VGA_COLOR_LIGHT_RED = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_LIGHT_BROWN = 14,
    VGA_COLOR_WHITE = 15,
};

/* Terminal Functions */
void terminal_initialize(void);
void terminal_setcolor(uint8_t color);
void terminal_putchar(char c);
void terminal_write(const char* data, size_t size);
void terminal_writestring(const char* data);
void terminal_clear(void);

/* Utility Functions */
void* memset(void* s, int c, size_t n);
void* memcpy(void* dest, const void* src, size_t n);
size_t strlen(const char* str);
char* strcpy(char* dest, const char* src);
bool strcmp(const char* s1, const char* s2);
void itoa(int value, char* str, int base);
void ultoa(unsigned long value, char* str, int base);
int toupper(int c);

// SATA drive functions
void init_sata_drives();
void cmd_list_sata_drives();

/* String function declarations */
char* strrchr(const char* str, int c);
int memcmp(const void* s1, const void* s2, size_t n);


/* Kernel Entry Point */
void kernel_main(void);

#endif // KERNEL_H

