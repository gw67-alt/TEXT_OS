#include <stdbool.h> /* C doesn't have booleans by default. */
#include <stddef.h>
#include <stdint.h>
#include "hardware_specs.h"
#include "io.h"
#include "stdio.h"

/* Check if the compiler thinks we are targeting the wrong operating system. */
#if defined(__linux__)
/* TODO what is this check? It was failing. */
/*#error "You are not using a cross-compiler, you will most certainly run into trouble"*/
#endif

/* This tutorial will only work for the 32-bit ix86 targets. */
#if !defined(__i386__)
#error "This tutorial needs to be compiled with a ix86-elf compiler"
#endif

#define SCREEN_BACKUP_SIZE (80 * 25)  // Hardcoded VGA dimensions

/* Hardware text mode color constants. */
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

// RTC (Real Time Clock) registers
#define CMOS_ADDRESS     0x70
#define CMOS_DATA        0x71

// RTC register addresses
#define RTC_SECONDS      0x00
#define RTC_MINUTES      0x02
#define RTC_HOURS        0x04
#define RTC_DAY          0x07
#define RTC_MONTH        0x08
#define RTC_YEAR         0x09

// Variables to store current time
uint8_t current_seconds = 0;
uint8_t current_minutes = 0;
uint8_t current_hours = 0;

// Timer counter for tracking seconds
uint32_t timer_ticks = 0;

// Function prototypes
uint8_t make_color(enum vga_color fg, enum vga_color bg);
uint16_t make_vgaentry(char c, uint8_t color);
void* memset(void* s, int c, size_t n);
void* memcpy(void* dest, const void* src, size_t n);
size_t strlen(const char* str);
char* strcpy(char* dest, const char* src);
void itoa(int value, char* str, int base);
void ultoa(unsigned long value, char* str, int base);
int toupper(int c);
void int_to_string(int num, char* str);
void reverse_string(char* str, int start, int end);
uint8_t inb(uint16_t port);
void outb(uint16_t port, uint8_t val);
void update_hardware_cursor(int x, int y);
void enable_hardware_cursor(uint8_t cursor_start, uint8_t cursor_end);
void disable_hardware_cursor();
void clear_screen();
void terminal_initialize();
void terminal_setcolor(uint8_t color);
void terminal_putentryat(char c, uint8_t color, size_t x, size_t y);
void terminal_putchar(char c);
void update_cursor_state();
void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran);
void init_gdt();
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);
void idt_load();
void keyboard_handler();
void timer_handler();
void init_pic();
void init_pit();
void init_keyboard();
void cmd_help();
void cmd_clear();
void cmd_hello();
void process_command();

// RTC function prototypes
uint8_t read_cmos(uint8_t reg);
uint8_t bcd_to_binary(uint8_t bcd);
void read_rtc_time();
void update_clock_display();
void init_rtc();

uint8_t make_color(enum vga_color fg, enum vga_color bg) {
    return fg | bg << 4;
}

uint16_t make_vgaentry(char c, uint8_t color) {
    uint16_t c16 = c;
    uint16_t color16 = color;
    return c16 | color16 << 8;
}


/* Memory set */
void* memset(void* s, int c, size_t n) {
    unsigned char* p = (unsigned char*)s;
    while (n--) {
        *p++ = (unsigned char)c;
    }
    return s;
}

/* Memory copy */
void* memcpy(void* dest, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

/* String length */
size_t strlen(const char* str) {
    size_t len = 0;
    while (str[len] != '\0') {
        len++;
    }
    return len;
}

/* String copy */
char* strcpy(char* dest, const char* src) {
    char* original_dest = dest;
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
    return original_dest;
}

/* String comparison */
bool strcmp(const char* s1, const char* s2) {
    while (*s1 && *s2) {
        if (*s1 != *s2) {
            return false;
        }
        s1++;
        s2++;
    }
    return *s1 == *s2;
}

/* Integer to string conversion */
void itoa(int value, char* str, int base) {
    char* rc;
    char* ptr;
    char* low;
    
    // Check for supported base
    if (base < 2 || base > 36) {
        *str = '\0';
        return;
    }
    
    rc = ptr = str;
    
    // Handle negative numbers
    if (value < 0) {
        *ptr++ = '-';
        value = -value;
    }
    
    low = ptr;
    
    // Convert number
    do {
        *ptr++ = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"[value % base];
        value /= base;
    } while (value > 0);
    
    // Terminate string
    *ptr = '\0';
    
    // Reverse string
    ptr--;
    while (low < ptr) {
        char temp = *low;
        *low++ = *ptr;
        *ptr-- = temp;
    }
}

/* Unsigned long to string conversion */
void ultoa(unsigned long value, char* str, int base) {
    char* rc;
    char* ptr;
    char* low;
    
    // Check for supported base
    if (base < 2 || base > 36) {
        *str = '\0';
        return;
    }
    
    rc = ptr = str;
    
    // Convert number
    do {
        *ptr++ = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"[value % base];
        value /= base;
    } while (value > 0);
    
    // Terminate string
    *ptr = '\0';
    
    // Reverse string
    ptr--;
    while (rc < ptr) {
        char temp = *rc;
        *rc++ = *ptr;
        *ptr-- = temp;
    }
}

/* Character to uppercase */
int toupper(int c) {
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 'A';
    }
    return c;
}


// Convert an integer to a string
void int_to_string(int num, char* str) {
    int i = 0;
    bool is_negative = false;
    
    // Handle 0 explicitly
    if (num == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return;
    }
    
    // Handle negative numbers
    if (num < 0) {
        is_negative = true;
        num = -num;
    }
    
    // Process individual digits
    while (num != 0) {
        int rem = num % 10;
        str[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
        num = num / 10;
    }
    
    // Add negative sign if needed
    if (is_negative)
        str[i++] = '-';
    
    // Add null terminator
    str[i] = '\0';
    
    // Reverse the string
    reverse_string(str, 0, i - 1);
}

// Helper function to reverse a string
void reverse_string(char* str, int start, int end) {
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        start++;
        end--;
    }
}

// Function to read a value from CMOS/RTC
uint8_t read_cmos(uint8_t reg) {
    // Disable NMI and select the register
    outb(CMOS_ADDRESS, reg | 0x80);
    // Read the value
    uint8_t value = inb(CMOS_DATA);
    return value;
}

// Convert BCD to binary if needed
uint8_t bcd_to_binary(uint8_t bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

// Function to read the current time from RTC
void read_rtc_time() {
    // Read hours, minutes, and seconds from RTC
    uint8_t seconds = read_cmos(RTC_SECONDS);
    uint8_t minutes = read_cmos(RTC_MINUTES);
    uint8_t hours = read_cmos(RTC_HOURS);
    
    // Check if values are in BCD format (typical for RTC)
    // Status Register B, bit 2 indicates if BCD (0) or binary (1)
    uint8_t status_b = read_cmos(0x0B);
    
    if (!(status_b & 0x04)) {
        // Convert from BCD to binary if needed
        seconds = bcd_to_binary(seconds);
        minutes = bcd_to_binary(minutes);
        hours = bcd_to_binary(hours);
    }
    
    // Store the current time
    current_seconds = seconds;
    current_minutes = minutes;
    current_hours = hours;
}

static const size_t VGA_WIDTH = 80;
static const size_t VGA_HEIGHT = 25;

size_t terminal_row;
size_t terminal_column;
uint8_t terminal_color;
uint16_t* terminal_buffer;
bool cursor_visible = true;  // Cursor visibility state
uint32_t cursor_blink_counter = 0;  // Counter for cursor blinking

// Function to update the clock display in the top-right corner
void update_clock_display() {
    const size_t clock_x = VGA_WIDTH - 8;  // Position for HH:MM:SS
    const size_t clock_y = 0;              // Top row
    char time_str[9];                      // HH:MM:SS + null terminator
    
    // Format the time string
    time_str[0] = (current_hours / 10) + '0';
    time_str[1] = (current_hours % 10) + '0';
    time_str[2] = ':';
    time_str[3] = (current_minutes / 10) + '0';
    time_str[4] = (current_minutes % 10) + '0';
    time_str[5] = ':';
    time_str[6] = (current_seconds / 10) + '0';
    time_str[7] = (current_seconds % 10) + '0';
    time_str[8] = '\0';
    
    // Backup current cursor position
    size_t saved_row = terminal_row;
    size_t saved_column = terminal_column;
    uint8_t saved_color = terminal_color;
    
    // Set color for clock (bright white on black)
    terminal_setcolor(make_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    
    // Directly write the time characters to the terminal buffer
    for (size_t i = 0; i < 8; i++) {
        terminal_putentryat(time_str[i], terminal_color, clock_x + i, clock_y);
    }
    
    // Restore cursor position and color
    terminal_row = saved_row;
    terminal_column = saved_column;
    terminal_setcolor(saved_color);
    
    // Update hardware cursor to restore its position
    update_hardware_cursor(terminal_column, terminal_row);
}

// Initialize the real-time clock functionality
void init_rtc() {
    // Read the initial time
    read_rtc_time();
    
    // Set up the initial clock display
    update_clock_display();
}

/* VGA cursor control functions */
void update_hardware_cursor(int x, int y) {
    uint16_t pos = y * VGA_WIDTH + x;

    // CRT Controller registers: cursor position (low and high bytes)
    outb(0x3D4, 0x0F);  // Low byte index
    outb(0x3D5, (uint8_t)(pos & 0xFF));  // Low byte data
    outb(0x3D4, 0x0E);  // High byte index
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));  // High byte data
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
    
    // Make sure to update the clock display after clearing screen
    update_clock_display();
}

void terminal_initialize() {
    terminal_row = 0;
    terminal_column = 0;
    terminal_color = make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_buffer = (uint16_t*) 0xB8000;
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

// Add this function before terminal_putchar
void terminal_scroll() {
    // Move all rows up by one (effectively deleting the top row)
    for (size_t y = 0; y < VGA_HEIGHT - 1; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t dst_index = y * VGA_WIDTH + x;
            const size_t src_index = (y + 1) * VGA_WIDTH + x;
            terminal_buffer[dst_index] = terminal_buffer[src_index];
        }
    }
    
    // Clear the last row
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        const size_t index = (VGA_HEIGHT - 1) * VGA_WIDTH + x;
        terminal_buffer[index] = make_vgaentry(' ', terminal_color);
    }
    
    // Adjust the cursor to the beginning of the last row
    terminal_row = VGA_HEIGHT - 1;
    terminal_column = 0;
    
    // Make sure to update the clock display after scrolling
    update_clock_display();
}

// Now modify the terminal_putchar function to use scrolling
void terminal_putchar(char c) {
    if (c == '\n') {
        // Handle newline character
        terminal_column = 0;
        if (++terminal_row == VGA_HEIGHT) {
            // Instead of wrapping to 0, scroll the screen
            terminal_scroll();
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
                // Instead of wrapping to 0, scroll the screen
                terminal_scroll();
            }
        }
    }

    // Update the hardware cursor position
    update_hardware_cursor(terminal_column, terminal_row);
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

/* Extended scancode table for function keys */
const char extended_scancode_table[128] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '\n', 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
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
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '-',
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


/* Extended key flag */
bool extended_key = false;

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
    gdtp.base = (uint32_t)&gdt;

    /* NULL descriptor */
    gdt_set_gate(0, 0, 0, 0, 0);

    /* Code segment: base = 0, limit = 4GB, 32-bit, code, ring 0 */
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);

    /* Data segment: base = 0, limit = 4GB, 32-bit, data, ring 0 */
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);

    /* Load GDT */
    __asm__ volatile ("lgdt %0" : : "m" (gdtp));

    /* Update segment registers */
    __asm__ volatile (
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
    idtp.base = (uint32_t)&idt;

    __asm__ volatile ("lidt %0" : : "m" (idtp));
}

/* Keyboard interrupt handler wrapper (assembler entry point) */
void keyboard_handler_wrapper();
__asm__(
    ".globl keyboard_handler_wrapper\n"
    "keyboard_handler_wrapper:\n"
    "    pusha\n"            // Save registers
    "    call keyboard_handler\n" // Call our C handler
    "    popa\n"             // Restore registers
    "    iret\n"             // Return from interrupt
);

/* Timer interrupt handler wrapper */
void timer_handler_wrapper();
__asm__(
    ".globl timer_handler_wrapper\n"
    "timer_handler_wrapper:\n"
    "    pusha\n"            // Save registers
    "    call timer_handler\n" // Call our C handler
    "    popa\n"             // Restore registers
    "    iret\n"             // Return from interrupt
);

/* Timer handler C function */
void timer_handler() {
    // Increment timer ticks
    timer_ticks++;
    
    // Blink cursor
    update_cursor_state();
    
    // Update clock every second (100 ticks at 100 Hz)
    if (timer_ticks % 100 == 0) {
        // Increment seconds
        current_seconds++;
        if (current_seconds >= 60) {
            current_seconds = 0;
            current_minutes++;
            if (current_minutes >= 60) {
                current_minutes = 0;
                current_hours++;
                if (current_hours >= 24) {
                    current_hours = 0;
                }
            }
        }
        
        // Every 10 seconds, sync with RTC to ensure accuracy
        if (current_seconds % 10 == 0) {
            read_rtc_time();
        }
        
        // Update the clock display
        update_clock_display();
    }

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


/* Initialize PIT (Programmable Interval Timer) for cursor blinking and clock */
void init_pit() {
    uint32_t divisor = 1193180 / 100; // 100 Hz timer frequency (10ms intervals)

    // Set command byte: channel 0, access mode lobyte/hibyte, mode 3 (square wave)
    outb(0x43, 0x36);

    // Send divisor (low byte first, then high byte)
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
    
    // Reset timer ticks counter
    timer_ticks = 0;
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
    idt_set_gate(0x20, (uint32_t)timer_handler_wrapper, 0x08, 0x8E);

    /* Set keyboard interrupt gate (IRQ1) */
    idt_set_gate(0x21, (uint32_t)keyboard_handler_wrapper, 0x08, 0x8E);

    /* Load IDT */
    idt_load();

    /* Initialize PIC */
    init_pic();

    /* Initialize PIT */
    init_pit();

    /* Enable interrupts */
    __asm__ volatile ("sti");
}

#define MAX_COMMAND_LENGTH 80
char command_buffer[MAX_COMMAND_LENGTH];
int command_length = 0;
bool command_ready = false;

// Function prototypes for command handlers
void cmd_help();
void cmd_clear();
void cmd_hello();
void cmd_time(); // New command for displaying time info
void process_command();

/* Keyboard handler with integrated command processing */
void keyboard_handler() {
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

    /* Normal input handling */
    char key = scancode_to_ascii[scancode];
    if (key != 0) {
        if (key == '\n') {
            // Enter key - process command
            terminal_putchar(key);
            command_buffer[command_length] = '\0';
            
            // Process the command immediately in the interrupt handler
            process_command();
            
            // Reset for next command
            command_length = 0;
            printf("> ");
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

    /* Reset extended key flag */
    extended_key = false;

    /* Send EOI to PIC */
    outb(0x20, 0x20);
}

/* Process the command in the buffer */
void process_command() {
    // Skip processing if buffer is empty
    if (command_length == 0) {
        return;
    }
    
    // Null-terminate for string comparison
    command_buffer[command_length] = '\0';
    
    // Check against known commands
    if (strcmp(command_buffer, "help")) {
        cmd_help();
    } else if (strcmp(command_buffer, "clear")) {
        cmd_clear();
    } else if (strcmp(command_buffer, "hello")) {
        cmd_hello();
    } else {
        printf("Unknown command: ");
        printf(command_buffer);
        printf("\n");
    }
}

/* Command implementations */
void cmd_help() {
    printf("Available commands:\n");
    printf("  help  - Show this help message\n");
    printf("  clear - Clear the screen\n");
    printf("  hello - Display a greeting\n");
}

void cmd_clear() {
    clear_screen();
}

void cmd_hello() {
    printf("Hello, user!\n");
}

/* Modified kernel_main function */
void kernel_main() {
    /* Initialize terminal interface */
    terminal_initialize();
    terminal_setcolor(make_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK));
    hardware_specs_initialize();
    
    /* Initialize keyboard and timer interrupts */
    init_keyboard();
    
    /* Initialize the real-time clock */
    init_rtc();
	
    enumerate_pci_devices();	

	uint32_t abar = find_ahci_controller();
    
    if (abar != 0) {
        // Identify disks connected to the AHCI controller
        identify_disks(abar);
    }    
	printf("Initialization complete. Start typing commands...\n");

    /* Display initial prompt */
    printf("> ");
    
    /* Reset command buffer */
    command_length = 0;
    
    /* Main loop - just wait for interrupts */
    while (1) {
        __asm__ volatile ("hlt");
    }
}
