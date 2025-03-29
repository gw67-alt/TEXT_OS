#if !defined(__cplusplus)
#error "This code needs to be compiled with a C++ compiler"
#endif
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio> // For sprintf
#include "test.h" 
#include "test2.h"

#define SCREEN_BACKUP_SIZE (80 * 25)  // Hardcoded VGA dimensions
#define MAX_COMMAND_LENGTH 80

// Forward declarations
class TerminalOutput;
class TerminalInput;

char* strcpy(char *dest, const char *src) {
    char *original_dest = dest;
    while (*src != '\0') {
        *dest = *src;
        dest++;
        src++;
    }
    *dest = '\0';
    return original_dest;
}

void* memset(void *str, int c, size_t n) {
    unsigned char *ptr = static_cast<unsigned char*>(str);
    unsigned char value = static_cast<unsigned char>(c);
    for (size_t i = 0; i < n; i++) {
        ptr[i] = value;
    }
    return str;
}

/* Hardware text mode color constants. */
enum vga_color {
    COLOR_BLACK = 0,
    COLOR_BLUE = 1,
    COLOR_GREEN = 2,
    COLOR_CYAN = 3,
    COLOR_RED = 4,
    COLOR_MAGENTA = 5,
    COLOR_BROWN = 6,
    COLOR_LIGHT_GREY = 7,
    COLOR_DARK_GREY = 8,
    COLOR_LIGHT_BLUE = 9,
    COLOR_LIGHT_GREEN = 10,
    COLOR_LIGHT_CYAN = 11,
    COLOR_LIGHT_RED = 12,
    COLOR_LIGHT_MAGENTA = 13,
    COLOR_LIGHT_BROWN = 14,
    COLOR_WHITE = 15,
};


// Add this to your existing code, near where strcpy and memset are defined

/**
 * Copy memory area
 *
 * @param dest   Destination memory area
 * @param src    Source memory area
 * @param n      Number of bytes to copy
 * @return       A pointer to dest
 */
 void* memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = static_cast<unsigned char*>(dest);
    const unsigned char *s = static_cast<const unsigned char*>(src);
    
    // Simple byte-by-byte copy
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    
    return dest;
}

// Optimized version of memcpy for word-aligned addresses (optional)
void* memcpy_aligned(void *dest, const void *src, size_t n) {
    // Check if addresses are word-aligned and size is multiple of 4
    if (((reinterpret_cast<uintptr_t>(dest) & 0x3) == 0) && 
        ((reinterpret_cast<uintptr_t>(src) & 0x3) == 0) && 
        ((n & 0x3) == 0)) {
        
        uint32_t *d = static_cast<uint32_t*>(dest);
        const uint32_t *s = static_cast<const uint32_t*>(src);
        
        // Copy 4 bytes at a time
        for (size_t i = 0; i < n/4; i++) {
            d[i] = s[i];
        }
    } else {
        // Fall back to regular byte-by-byte copy
        unsigned char *d = static_cast<unsigned char*>(dest);
        const unsigned char *s = static_cast<const unsigned char*>(src);
        
        for (size_t i = 0; i < n; i++) {
            d[i] = s[i];
        }
    }
    
    return dest;
}


static const size_t VGA_WIDTH = 80;
static const size_t VGA_HEIGHT = 25;

size_t terminal_row;
size_t terminal_column;
uint8_t terminal_color;
uint16_t* terminal_buffer;
bool cursor_visible = true;  // Cursor visibility state
uint32_t cursor_blink_counter = 0;  // Counter for cursor blinking
bool extended_key = false;
char command_buffer[MAX_COMMAND_LENGTH];
int command_length = 0;
bool command_ready = false;

/* IDT structures */
struct idt_entry {
    uint16_t base_lo;
    uint16_t sel;
    uint8_t always0;
    uint8_t flags;
    uint16_t base_hi;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct idt_entry idt[256];
struct idt_ptr idtp;

/* GDT strutures - we need this for interrupts */
struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct gdt_entry gdt[3];
struct gdt_ptr gdtp;

/* Extended scancode table for function keys */
const char extended_scancode_table[128] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '\n', 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* Keyboard scancode array for US QWERTY layout */
const char scancode_to_ascii[128] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '-',
    0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* Key scancodes */
#define SCANCODE_F1  0x3B
#define SCANCODE_F2  0x3C
#define SCANCODE_F3  0x3D
#define SCANCODE_F4  0x3E
#define SCANCODE_F5  0x3F
#define SCANCODE_F6  0x40
#define SCANCODE_F7  0x41
#define SCANCODE_F8  0x42
#define SCANCODE_F9  0x43
#define SCANCODE_F10 0x44
#define SCANCODE_F11 0x57
#define SCANCODE_F12 0x58

#define SCANCODE_UP    0x48
#define SCANCODE_DOWN  0x50
#define SCANCODE_LEFT  0x4B
#define SCANCODE_RIGHT 0x4D

// Function declarations
uint8_t make_color(enum vga_color fg, enum vga_color bg);
uint16_t make_vgaentry(char c, uint8_t color);
size_t strlen(const char* str);
static inline uint8_t inb(uint16_t port);
static inline void outb(uint16_t port, uint8_t val);
void update_hardware_cursor(int x, int y);
void enable_hardware_cursor(uint8_t cursor_start, uint8_t cursor_end);
void disable_hardware_cursor();
void clear_screen();
void terminal_initialize();
void terminal_setcolor(uint8_t color);
void terminal_putentryat(char c, uint8_t color, size_t x, size_t y);
void terminal_putchar(char c);
void terminal_writestring(const char* data);
void update_cursor_state();
void scroll_screen();
void init_pic();
void init_pit();
void init_keyboard();
void cmd_help();
void cmd_clear();
void cmd_hello();
void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran);
extern "C" void keyboard_handler_wrapper();
extern "C" void timer_handler_wrapper();
extern "C" void timer_handler();
extern "C" void keyboard_handler();
extern "C" void kernel_main();

uint8_t make_color(enum vga_color fg, enum vga_color bg) {
    return fg | bg << 4;
}

uint16_t make_vgaentry(char c, uint8_t color) {
    uint16_t c16 = static_cast<uint16_t>(c);
    uint16_t color16 = static_cast<uint16_t>(color);
    return c16 | color16 << 8;
}

size_t strlen(const char* str) {
    size_t ret = 0;
    while (str[ret] != 0)
        ret++;
    return ret;
}

bool string_compare(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *s1 == *s2;
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

void update_hardware_cursor(int x, int y) {
    uint16_t pos = y * VGA_WIDTH + x;

    // CRT Controller registers: cursor position (low and high bytes)
    outb(0x3D4, 0x0F);  // Low byte index
    outb(0x3D5, static_cast<uint8_t>(pos & 0xFF));  // Low byte data
    outb(0x3D4, 0x0E);  // High byte index
    outb(0x3D5, static_cast<uint8_t>((pos >> 8) & 0xFF));  // High byte data
}

void enable_hardware_cursor(uint8_t cursor_start, uint8_t cursor_end) {
    // CRT Controller registers: cursor shape
    outb(0x3D4, 0x0A);  // Cursor start register
    outb(0x3D5, (inb(0x3D5) & 0xC0) | cursor_start);  // Set start line (bits 0-4)

    outb(0x3D4, 0x0B);  // Cursor end register
    outb(0x3D5, (inb(0x3D5) & 0xE0) | cursor_end);  // Set end line (bits 0-4)
}

void disable_hardware_cursor() {
    outb(0x3D4, 0x0A);  // Cursor start register
    outb(0x3D5, 0x20);  // Bit 5 disables the cursor
}

void clear_screen() {
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            terminal_buffer[index] = make_vgaentry(' ', terminal_color);
        }
    }
    terminal_row = 0;
    terminal_column = 0;
    update_hardware_cursor(terminal_column, terminal_row);
}

void terminal_initialize() {
    terminal_row = 0;
    terminal_column = 0;
    terminal_color = make_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    terminal_buffer = reinterpret_cast<uint16_t*>(0xB8000);
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            terminal_buffer[index] = make_vgaentry(' ', terminal_color);
        }
    }

    // Initialize hardware cursor (start line 14, end line 15 - typical underline cursor)
    enable_hardware_cursor(14, 15);
    update_hardware_cursor(0, 0);
}

void terminal_setcolor(uint8_t color) {
    terminal_color = color;
}

void terminal_putentryat(char c, uint8_t color, size_t x, size_t y) {
    const size_t index = y * VGA_WIDTH + x;
    terminal_buffer[index] = make_vgaentry(c, color);
}

/* Add this new function to scroll the screen content up by one line */
void scroll_screen() {
    // Move all rows up one line (except the first row which will be overwritten)
    for (size_t y = 0; y < VGA_HEIGHT - 1; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t dest_index = y * VGA_WIDTH + x;
            const size_t src_index = (y + 1) * VGA_WIDTH + x;
            terminal_buffer[dest_index] = terminal_buffer[src_index];
        }
    }
    
    // Clear the last row
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        const size_t index = (VGA_HEIGHT - 1) * VGA_WIDTH + x;
        terminal_buffer[index] = make_vgaentry(' ', terminal_color);
    }
}

/* Modified terminal_putchar function with scrolling */
void terminal_putchar(char c) {
    if (c == '\n') {
        // Handle newline character
        terminal_column = 0;
        if (++terminal_row == VGA_HEIGHT) {
            // We've reached the bottom of the screen, need to scroll
            scroll_screen();
            terminal_row = VGA_HEIGHT - 1; // Stay at the last row
        }
    } else if (c == '\b') {
        // Handle backspace character
        if (terminal_column > 0) {
            // Move cursor back one position
            terminal_column--;
            // Clear the character at the cursor position
            terminal_putentryat(' ', terminal_color, terminal_column, terminal_row);
        } else if (terminal_row > 0) {
            // If at the beginning of a line and not the first line,
            // move to the end of the previous line
            terminal_row--;
            terminal_column = VGA_WIDTH - 1;
            // Clear the character at the cursor position
            terminal_putentryat(' ', terminal_color, terminal_column, terminal_row);
        }
    } else {
        terminal_putentryat(c, terminal_color, terminal_column, terminal_row);
        if (++terminal_column == VGA_WIDTH) {
            terminal_column = 0;
            if (++terminal_row == VGA_HEIGHT) {
                // We've reached the bottom of the screen, need to scroll
                scroll_screen();
                terminal_row = VGA_HEIGHT - 1; // Stay at the last row
            }
        }
    }

    // Update the hardware cursor position
    update_hardware_cursor(terminal_column, terminal_row);
}

void terminal_writestring(const char* data) {
    size_t datalen = strlen(data);
    for (size_t i = 0; i < datalen; i++)
        terminal_putchar(data[i]);
}

void update_cursor_state() {
    cursor_blink_counter++;
    if (cursor_blink_counter >= 25) {  // Adjust this value to control blink speed
        cursor_blink_counter = 0;
        cursor_visible = !cursor_visible;

        if (cursor_visible) {
            enable_hardware_cursor(14, 15);  // Show cursor (underline style)
        } else {
            disable_hardware_cursor();  // Hide cursor
        }

        update_hardware_cursor(terminal_column, terminal_row);
    }
}

// Enhanced TerminalOutput class with scrolling capabilities
class TerminalOutput {
private:
    static const int SCROLLBACK_BUFFER_HEIGHT = VGA_HEIGHT * 5;  // Store 5 screens worth of scrollback
    uint16_t scrollback_buffer[SCROLLBACK_BUFFER_HEIGHT * VGA_WIDTH];
    int scrollback_lines = 0;  // Number of lines in the scrollback buffer

    // Helper function to scroll the screen up by one line
    void scroll_screen_internal() {
        // First, save the top line that's about to be scrolled off to scrollback buffer
        if (scrollback_lines < SCROLLBACK_BUFFER_HEIGHT) {
            // We have room in scrollback buffer, so save the first line
            for (size_t x = 0; x < VGA_WIDTH; x++) {
                scrollback_buffer[scrollback_lines * VGA_WIDTH + x] = terminal_buffer[x];
            }
            scrollback_lines++;
        } else {
            // Scrollback buffer is full, so shift everything up one line
            for (size_t y = 0; y < SCROLLBACK_BUFFER_HEIGHT - 1; y++) {
                for (size_t x = 0; x < VGA_WIDTH; x++) {
                    scrollback_buffer[y * VGA_WIDTH + x] = scrollback_buffer[(y + 1) * VGA_WIDTH + x];
                }
            }
            
            // Now save the current top line to the last scrollback line
            for (size_t x = 0; x < VGA_WIDTH; x++) {
                scrollback_buffer[(SCROLLBACK_BUFFER_HEIGHT - 1) * VGA_WIDTH + x] = terminal_buffer[x];
            }
        }
        
        // Move all rows up one line
        for (size_t y = 0; y < VGA_HEIGHT - 1; y++) {
            for (size_t x = 0; x < VGA_WIDTH; x++) {
                const size_t dest_index = y * VGA_WIDTH + x;
                const size_t src_index = (y + 1) * VGA_WIDTH + x;
                terminal_buffer[dest_index] = terminal_buffer[src_index];
            }
        }
        
        // Clear the last row
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = (VGA_HEIGHT - 1) * VGA_WIDTH + x;
            terminal_buffer[index] = make_vgaentry(' ', terminal_color);
        }
    }
    
    // Helper function to put a character at a specific position
    void put_entry_at(char c, uint8_t color, size_t x, size_t y) {
        const size_t index = y * VGA_WIDTH + x;
        terminal_buffer[index] = make_vgaentry(c, color);
    }
    
    // Enhanced putchar function with scrolling
    void put_char(char c) {
        if (c == '\n') {
            // Handle newline character
            terminal_column = 0;
            if (++terminal_row == VGA_HEIGHT) {
                // We've reached the bottom of the screen, need to scroll
                scroll_screen_internal();
                terminal_row = VGA_HEIGHT - 1; // Stay at the last row
            }
        } else if (c == '\b') {
            // Handle backspace character
            if (terminal_column > 0) {
                // Move cursor back one position
                terminal_column--;
                // Clear the character at the cursor position
                put_entry_at(' ', terminal_color, terminal_column, terminal_row);
            } else if (terminal_row > 0) {
                // If at the beginning of a line and not the first line,
                // move to the end of the previous line
                terminal_row--;
                terminal_column = VGA_WIDTH - 1;
                // Clear the character at the cursor position
                put_entry_at(' ', terminal_color, terminal_column, terminal_row);
            }
        } else if (c == '\r') {
            // Handle carriage return (move to beginning of line)
            terminal_column = 0;
        } else if (c == '\t') {
            // Handle tab (move to next 8-character boundary)
            size_t tab_size = 8;
            terminal_column = (terminal_column + tab_size) & ~(tab_size - 1);
            if (terminal_column >= VGA_WIDTH) {
                terminal_column = 0;
                if (++terminal_row == VGA_HEIGHT) {
                    scroll_screen_internal();
                    terminal_row = VGA_HEIGHT - 1;
                }
            }
        } else {
            put_entry_at(c, terminal_color, terminal_column, terminal_row);
            if (++terminal_column == VGA_WIDTH) {
                terminal_column = 0;
                if (++terminal_row == VGA_HEIGHT) {
                    scroll_screen_internal();
                    terminal_row = VGA_HEIGHT - 1;
                }
            }
        }

        // Update the hardware cursor position
        update_hardware_cursor(terminal_column, terminal_row);
    }

public:
    TerminalOutput() {
        // Initialize scrollback buffer
        memset(scrollback_buffer, 0, sizeof(scrollback_buffer));
    }
    
    // Show a specific page from the scrollback buffer (0 is newest, scrollback_pages-1 is oldest)
    bool show_scrollback_page(int page) {
        if (page < 0 || page >= scrollback_lines / VGA_HEIGHT) {
            return false;  // Invalid page number
        }
        
        // Calculate starting line in scrollback buffer
        int start_line = scrollback_lines - (page + 1) * VGA_HEIGHT;
        if (start_line < 0) {
            start_line = 0;
        }
        
        // Backup current screen if this is the first scrollback operation
        static bool first_scrollback = true;
        static uint16_t screen_backup[SCREEN_BACKUP_SIZE];
        if (first_scrollback) {
            for (size_t y = 0; y < VGA_HEIGHT; y++) {
                for (size_t x = 0; x < VGA_WIDTH; x++) {
                    screen_backup[y * VGA_WIDTH + x] = terminal_buffer[y * VGA_WIDTH + x];
                }
            }
            first_scrollback = false;
        }
        
        // Copy scrollback buffer to screen
        for (size_t y = 0; y < VGA_HEIGHT; y++) {
            for (size_t x = 0; x < VGA_WIDTH; x++) {
                if (start_line + y < scrollback_lines) {
                    terminal_buffer[y * VGA_WIDTH + x] = scrollback_buffer[(start_line + y) * VGA_WIDTH + x];
                } else {
                    // If we run out of scrollback lines, use the backup screen
                    int backup_line = y - (scrollback_lines - start_line);
                    if (backup_line >= 0 && backup_line < VGA_HEIGHT) {
                        terminal_buffer[y * VGA_WIDTH + x] = screen_backup[backup_line * VGA_WIDTH + x];
                    } else {
                        // Fill with blanks if we somehow run out of data
                        terminal_buffer[y * VGA_WIDTH + x] = make_vgaentry(' ', terminal_color);
                    }
                }
            }
        }
        
        // Update hardware cursor (place at bottom left during scrollback)
        update_hardware_cursor(0, VGA_HEIGHT - 1);
        return true;
    }
    
    // Restore the screen after scrollback viewing
    void restore_screen() {
        // Redraw the current screen (the system should continue from where it left off)
        // This is essentially just a refresh, since the actual terminal_buffer hasn't changed
        update_hardware_cursor(terminal_column, terminal_row);
    }
    
    // Get the number of pages available in scrollback
    int get_scrollback_pages() {
        return (scrollback_lines + VGA_HEIGHT - 1) / VGA_HEIGHT;  // Ceiling division
    }
    
    // Standard output operators
    TerminalOutput& operator<<(const char* str) {
        size_t len = strlen(str);
        for (size_t i = 0; i < len; i++) {
            put_char(str[i]);
        }
        return *this;
    }

    TerminalOutput& operator<<(char c) {
        put_char(c);
        return *this;
    }

    TerminalOutput& operator<<(int num) {
        char buffer[20];
        sprintf(buffer, "%d", num);
        *this << buffer;
        return *this;
    }
    
    // Add support for unsigned integers
    TerminalOutput& operator<<(unsigned int num) {
        char buffer[20];
        sprintf(buffer, "%u", num);
        *this << buffer;
        return *this;
    }
    
    // Add support for hex output
    TerminalOutput& operator<<(void* ptr) {
        char buffer[20];
        sprintf(buffer, "0x%x", reinterpret_cast<unsigned int>(ptr));
        *this << buffer;
        return *this;
    }
};

// Modified TerminalInput class without scrolling functionality
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
        TerminalInput() : input_ready(false) {
            // Initialize history
            for (int i = 0; i < HISTORY_SIZE; i++) {
                memset(command_history[i], 0, MAX_COMMAND_LENGTH);
            }
        }
        
        // Called from the keyboard handler when Enter is pressed
        void setInputReady(const char* buffer) {
            strcpy(input_buffer, buffer);
            
            // Add command to history if not empty
            if (strlen(buffer) > 0) {
                // Shift history entries down
                for (int i = HISTORY_SIZE - 1; i > 0; i--) {
                    strcpy(command_history[i], command_history[i-1]);
                }
                
                // Add new command at the top
                strcpy(command_history[0], buffer);
                
                // Update history count if needed
                if (history_count < HISTORY_SIZE) {
                    history_count++;
                }
            }
            
            history_index = -1;  // Reset history navigation
            input_ready = true;
        }
        
        // Handle up/down keys for history navigation
        void navigateHistory(bool up) {
            if (up) {  // Up key
                if (history_index < history_count - 1) {
                    history_index++;
                    // Update command buffer with history item
                    strcpy(command_buffer, command_history[history_index]);
                    command_length = strlen(command_buffer);
                    
                    // Clear current input line and display the history item
                    clearInputLine();
                    terminal_writestring(command_buffer);
                }
            } else {  // Down key
                if (history_index > -1) {
                    history_index--;
                    
                    // Clear current input line
                    clearInputLine();
                    
                    if (history_index == -1) {
                        // Return to empty input
                        command_buffer[0] = '\0';
                        command_length = 0;
                    } else {
                        // Show previous history item
                        strcpy(command_buffer, command_history[history_index]);
                        command_length = strlen(command_buffer);
                        terminal_writestring(command_buffer);
                    }
                }
            }
        }
        
        // Clear the current input line (helper function)
        void clearInputLine() {
            size_t current_col = terminal_column;
            while (current_col > 0) {
                terminal_putchar('\b');
                current_col--;
            }
            
            // Clear the entire line
            for (size_t i = 0; i < VGA_WIDTH; i++) {
                terminal_putentryat(' ', terminal_color, i, terminal_row);
            }
            
            // Reset cursor to beginning of line
            terminal_column = 0;
            update_hardware_cursor(terminal_column, terminal_row);
        }
        
        // Standard input operation with waiting
        TerminalInput& operator>>(char* str) {
            input_ready = false;
            memset(input_buffer, 0, sizeof(input_buffer));
            
            // Display prompt (using low-level functions to avoid circular dependency)
            
            // Reset command buffer
            command_length = 0;
            
            // Wait for input to be ready (set by keyboard interrupt)
            while (!input_ready) {
                asm volatile ("hlt"); // Wait for input
            }
            
            // Copy input to provided string
            strcpy(str, input_buffer);
            return *this;
        }
    };
    

// Global instances - now we can actually define them
TerminalOutput cout;
TerminalInput cin;

// Modified keyboard_handler function without Page Up/Down scrolling functionality
extern "C" void keyboard_handler() {
    /* Read scancode from keyboard data port */
    uint8_t scancode = inb(0x60);

    /* Check for extended key code (0xE0) */
    if (scancode == 0xE0) {
        extended_key = true;
        /* Send EOI to PIC */
        outb(0x20, 0x20);
        return;
    }

    /* Handle key release (bit 7 set) */
    if (scancode & 0x80) {
        /* Reset extended key flag if it was set */
        extended_key = false;
        /* Send EOI to PIC */
        outb(0x20, 0x20);
        return;
    }

    // Handle special keys for extended keyboard sequences
    if (extended_key) {
        switch (scancode) {
            case SCANCODE_UP: // Up arrow
                // Access the global cin object and use history navigation
                cin.navigateHistory(true);
                break;
                
            case SCANCODE_DOWN: // Down arrow
                // Access the global cin object and use history navigation
                cin.navigateHistory(false);
                break;
                
            // Removed Page Up (0x49) case
            // Removed Page Down (0x51) case
        }
        
        // Reset extended key flag
        extended_key = false;
        
        /* Send EOI to PIC */
        outb(0x20, 0x20);
        return;
    }

    /* Normal input handling */
    char key = scancode_to_ascii[scancode];
    if (key != 0) {
        if (key == '\n') {
            // Enter key - process command
            terminal_putchar(key);
            command_buffer[command_length] = '\0';

            // Notify TerminalInput about the command
            cin.setInputReady(command_buffer);

            // Reset for next command
            command_length = 0;
        } else if (key == '\b') {
            // Backspace - delete last character
            if (command_length > 0) {
                terminal_putchar(key);
                command_length--;
            }
        } else if (command_length < MAX_COMMAND_LENGTH - 1) {
            // Regular character - add to buffer and display
            command_buffer[command_length++] = key;
            terminal_putchar(key);
        }
    }

    /* Send EOI to PIC */
    outb(0x20, 0x20);
}


// Initialize the TerminalOutput
void init_terminal_io() {
    // Just make sure the global instances are properly initialized
    cout = TerminalOutput();
    cin = TerminalInput();
}

/* Set up a GDT entry */
void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;

    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);

    gdt[num].access = access;
}

/* Initialize GDT */
void init_gdt() {
    gdtp.limit = (sizeof(struct gdt_entry) * 3) - 1;
    gdtp.base = reinterpret_cast<uint32_t>(&gdt);

    /* NULL descriptor */
    gdt_set_gate(0, 0, 0, 0, 0);

    /* Code segment: base = 0, limit = 4GB, 32-bit, code, ring 0 */
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);

    /* Data segment: base = 0, limit = 4GB, 32-bit, data, ring 0 */
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);

    /* Load GDT */
    asm volatile ("lgdt %0" : : "m" (gdtp));

    /* Update segment registers */
    asm volatile (
        "jmp $0x08, $reload_cs\n"
        "reload_cs:\n"
        "mov $0x10, %ax\n"
        "mov %ax, %ds\n"
        "mov %ax, %es\n"
        "mov %ax, %fs\n"
        "mov %ax, %gs\n"
        "mov %ax, %ss\n"
    );
}

/* Set up IDT entry */
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_lo = (base & 0xFFFF);
    idt[num].base_hi = (base >> 16) & 0xFFFF;
    idt[num].sel = sel;
    idt[num].always0 = 0;
    idt[num].flags = flags;
}

/* Load IDT */
void idt_load() {
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base = reinterpret_cast<uint32_t>(&idt);

    asm volatile ("lidt %0" : : "m" (idtp));
}

/* Keyboard interrupt handler wrapper (assembler entry point) */
extern "C" void keyboard_handler_wrapper();
asm(
    ".global keyboard_handler_wrapper\n"
    "keyboard_handler_wrapper:\n"
    "    pusha\n"            // Save registers
    "    call keyboard_handler\n" // Call our C++ handler
    "    popa\n"             // Restore registers
    "    iret\n"             // Return from interrupt
);

/* Timer interrupt handler wrapper */
extern "C" void timer_handler_wrapper();
asm(
    ".global timer_handler_wrapper\n"
    "timer_handler_wrapper:\n"
    "    pusha\n"            // Save registers
    "    call timer_handler\n" // Call our C++ handler
    "    popa\n"             // Restore registers
    "    iret\n"             // Return from interrupt
);

/* Timer handler C++ function */
extern "C" void timer_handler() {
    // Blink cursor
    update_cursor_state();

    // Send EOI to PIC
    outb(0x20, 0x20);
}

/* Initialize PIC */
void init_pic() {
    /* ICW1: Start initialization sequence */
    outb(0x20, 0x11); /* Master PIC */
    outb(0xA0, 0x11); /* Slave PIC */

    /* ICW2: Define PIC vectors */
    outb(0x21, 0x20); /* Master PIC vector offset (IRQ0 = int 0x20) */
    outb(0xA1, 0x28); /* Slave PIC vector offset (IRQ8 = int 0x28) */

    /* ICW3: Tell Master PIC that there is a slave PIC at IRQ2 */
    outb(0x21, 0x04);
    /* ICW3: Tell Slave PIC its cascade identity */
    outb(0xA1, 0x02);

    /* ICW4: Set x86 mode */
    outb(0x21, 0x01);
    outb(0xA1, 0x01);

    /* Mask all interrupts except keyboard (IRQ1) and timer (IRQ0) */
    outb(0x21, 0xFC); /* 1111 1100 = all but IRQ0 and IRQ1 masked */
    outb(0xA1, 0xFF); /* Mask all slave interrupts */
}

/* Initialize PIT (Programmable Interval Timer) for cursor blinking */
void init_pit() {
    uint32_t divisor = 1193180 / 100; // 100 Hz timer frequency

    // Set command byte: channel 0, access mode lobyte/hibyte, mode 3 (square wave)
    outb(0x43, 0x36);

    // Send divisor (low byte first, then high byte)
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
}

/* Initialize keyboard */
void init_keyboard() {
    /* First, set up GDT */
    init_gdt();

    /* Initialize IDT */
    for (int i = 0; i < 256; i++) {
        idt_set_gate(i, 0, 0, 0);
    }

    /* Set timer interrupt gate (IRQ0) */
    idt_set_gate(0x20, reinterpret_cast<uint32_t>(timer_handler_wrapper), 0x08, 0x8E);

    /* Set keyboard interrupt gate (IRQ1) */
    idt_set_gate(0x21, reinterpret_cast<uint32_t>(keyboard_handler_wrapper), 0x08, 0x8E);

    /* Load IDT */
    idt_load();

    /* Initialize PIC */
    init_pic();

    /* Initialize PIT */
    init_pit();

    /* Enable interrupts */
    asm volatile ("sti");
}


/* Command implementations */
void cmd_help() {
    cout << "Available commands:\n";
    cout << "  help  - Show this help message\n";
    cout << "  clear - Clear the screen\n";
    cout << "  hello - Display a greeting\n";
    cout << "  program1 - run a print program\n";
    cout << "  program2 - run a print program\n";
}

void cmd_hello() {
    cout << "Hello, user!\n";
}
class StringRef {
    private:
        const char* data;

    public:
        // Implicit constructor to allow automatic conversion
        StringRef(const char* str) : data(str) {}

        // Get the underlying string
        const char* c_str() const { return data; }

        // Compare with another string using string_compare
        bool operator==(const StringRef& other) const {
            return string_compare(data, other.data);
        }
    };

void command_prompt() {
    char input[MAX_COMMAND_LENGTH + 1]; // Add 1 for null terminator
    /* Display initial prompt */
    while (true) {
        cout << "> ";

        // Safely read input and null-terminate
        cin >> input;
        input[MAX_COMMAND_LENGTH] = '\0'; // Ensure null termination

        StringRef cmd(input); // Create a StringRef from the input

        if (cmd == "help") {
            cmd_help();
        }
        if (cmd == "clear") {
            clear_screen();
        }
        if (cmd == "hello") {
            cmd_hello();
        }
        if (cmd == "program1") {
            print_prog();
        }

        if (cmd == "program2") {
            print_prog2();
        }
    }
}


// Modified kernel_main function with updated message (no PgUp/PgDn)
extern "C" void kernel_main() {
    /* Initialize terminal interface */
    terminal_initialize();

    /* Initialize terminal I/O */
    init_terminal_io();

    /* Initialize keyboard and timer interrupts */
    init_keyboard();

    cout << "Hello, kernel World!" << '\n';
    cout << "Initialization complete. Start typing commands...\n";

    command_prompt();
    /* Reset command buffer */
    command_length = 0;

}