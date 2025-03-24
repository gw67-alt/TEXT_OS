#if !defined(__cplusplus)
#include <stdbool.h> /* C doesn't have booleans by default. */
#endif
#include <stddef.h>
#include <stdint.h>
#include "utility.h"

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

// Function declarations for FAT32 filesystem support
void init_filesystem();
void cmd_fsinfo();
void cmd_ls();
void cmd_cat();
void cmd_write();
void cmd_mkdir();
void cmd_rm();


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

uint8_t make_color(enum vga_color fg, enum vga_color bg) {
    return fg | bg << 4;
}

uint16_t make_vgaentry(char c, uint8_t color) {
    uint16_t c16 = c;
    uint16_t color16 = color;
    return c16 | color16 << 8;
}


static const size_t VGA_WIDTH = 80;
static const size_t VGA_HEIGHT = 25;

size_t terminal_row;
size_t terminal_column;
uint8_t terminal_color;
uint16_t* terminal_buffer;
bool cursor_visible = true;  // Cursor visibility state
uint32_t cursor_blink_counter = 0;  // Counter for cursor blinking

/* I/O port functions */
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
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

/* Scroll the screen up by one line */
void terminal_scroll() {
    // Move all lines up by one line
    for (size_t y = 0; y < VGA_HEIGHT - 1; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t dest_index = y * VGA_WIDTH + x;
            const size_t src_index = (y + 1) * VGA_WIDTH + x;
            terminal_buffer[dest_index] = terminal_buffer[src_index];
        }
    }
    
    // Clear the last line
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        const size_t index = (VGA_HEIGHT - 1) * VGA_WIDTH + x;
        terminal_buffer[index] = make_vgaentry(' ', terminal_color);
    }
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

void terminal_putchar(char c) {
    if (c == '\n') {
        // Handle newline character
        terminal_column = 0;
        terminal_row++;
        
        // If we reached the bottom, scroll instead of wrapping
        if (terminal_row == VGA_HEIGHT) {
            terminal_scroll();
            terminal_row = VGA_HEIGHT - 1;
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
        terminal_column++;
        
        // Handle line wrapping
        if (terminal_column == VGA_WIDTH) {
            terminal_column = 0;
            terminal_row++;
            
            // If we reached the bottom, scroll instead of wrapping
            if (terminal_row == VGA_HEIGHT) {
                terminal_scroll();
                terminal_row = VGA_HEIGHT - 1;
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
void cmd_test_scroll();
void process_command();

/* Test the scrolling functionality */
void cmd_test_scroll() {
    terminal_writestring("Testing scrolling functionality...\n");
    
    for (int i = 1; i <= 30; i++) {
        char buffer[64];
        char num_str[16];
        
        // Build the string manually
        memcpy(buffer, "This is line ", 13);
        itoa(i, num_str, 10);
        
        // Calculate lengths
        size_t prefix_len = 13;
        size_t num_len = strlen(num_str);
        
        // Append number
        memcpy(buffer + prefix_len, num_str, num_len);
        
        // Append rest of message
        memcpy(buffer + prefix_len + num_len, " of the scrolling test.\n", 25);
        
        // Null-terminate
        buffer[prefix_len + num_len + 25] = '\0';
        
        // Display the line
        terminal_writestring(buffer);
        
        // Add a small delay to make the scrolling visible
        for (volatile int j = 0; j < 5000000; j++);
    }
    
    terminal_writestring("Scroll test complete!\n");
}

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
            terminal_writestring("> ");
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
    // Add null terminator to command
    command_buffer[command_length] = '\0';
    
    // Skip to first non-space character
    char* cmd = command_buffer;
    while (*cmd == ' ') cmd++;
    
    // Check commands
    if (strcmp(cmd, "help")) {
        cmd_help();
    } else if (strcmp(cmd, "clear")) {
        cmd_clear();
    } else if (strcmp(cmd, "hello")) {
        cmd_hello();
    } else if (strcmp(cmd, "scroll")) {
        cmd_test_scroll();
    } else if (strcmp(cmd, "fsinfo")) {
        cmd_fsinfo();
    } else if (strcmp(cmd, "ls")) {
        cmd_ls();
    } else if (strncmp(cmd, "cat ", 4)) {
        cmd_cat();
    } else if (strncmp(cmd, "write ", 6)) {
        cmd_write();
    } else if (strncmp(cmd, "mkdir ", 6)) {
        cmd_mkdir();
    } else if (strncmp(cmd, "rm ", 3)) {
        cmd_rm();
    } else {
        terminal_writestring("Unknown command: ");
        terminal_writestring(cmd);
        terminal_writestring("\n");
    }
}


// Updated help command to include file system commands
void cmd_help() {
    terminal_writestring("Available commands:\n");
    terminal_writestring("  help    - Show this help message\n");
    terminal_writestring("  clear   - Clear the screen\n");
    terminal_writestring("  hello   - Display a greeting\n");
    terminal_writestring("  fsinfo  - Display filesystem information\n");
    terminal_writestring("  ls      - List files in root directory\n");
    terminal_writestring("  cat <file> - Display file contents\n");
    terminal_writestring("  write <file> <content> - Write to a file\n");
    terminal_writestring("  mkdir <name> - Create a directory\n");
    terminal_writestring("  rm <file> - Delete a file or empty directory\n");
    terminal_writestring("  freespace - Display free space on the filesystem\n");
}

void cmd_clear() {
    clear_screen();
}

void cmd_hello() {
    terminal_writestring("Hello, user!\n");
}

/* Modified kernel_main function */
void kernel_main() {
    // Initialize terminal interface
    terminal_initialize();

    // Initialize keyboard and timer interrupts
    init_keyboard();

    terminal_writestring("Hello, kernel World!\n");
    terminal_writestring("Terminal now has scrolling support!\n");
    
    // Initialize filesystem
    init_filesystem();
    
    terminal_writestring("Type 'help' for a list of commands\n");
    terminal_writestring("Type 'scroll' to test scrolling functionality\n");
    
    // Display initial prompt
    terminal_writestring("> ");
    
    // Reset command buffer
    command_length = 0;
    
    // Main loop - just wait for interrupts
    while (1) {
        __asm__ volatile ("hlt");
    }
}