#include "interrupts.h"
#include "terminal_hooks.h"
#include "iostream_wrapper.h"
#ifndef MAX_COMMAND_LENGTH
#define MAX_COMMAND_LENGTH 256
#endif

// IDT and GDT structures
struct idt_entry idt[256];
struct idt_ptr idtp;
struct gdt_entry gdt[3];
struct gdt_ptr gdtp;


// Keyboard input buffer and state variables (static to this file)
static bool shift_pressed = false; // Tracks if a shift key is currently pressed

// Scancodes for Shift keys (PS/2 Scan Code Set 1/2 Make codes)
#define SCANCODE_LSHIFT_PRESS   0x2A
#define SCANCODE_LSHIFT_RELEASE 0xAA
#define SCANCODE_RSHIFT_PRESS   0x36
#define SCANCODE_RSHIFT_RELEASE 0xB6

// Scancodes for UP/DOWN arrows (example, ensure these are correct for your setup)
#define SCANCODE_UP   0x48  // Common code after E0 prefix
#define SCANCODE_DOWN 0x50  // Common code after E0 prefix
// Keyboard scancode tables
const char scancode_to_ascii[128] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '-',
    0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// New scancode table for shifted characters
const char scancode_to_ascii_shifted[128] = {
    0,    0,  '!',  '@',  '#',  '$',  '%',  '^',  '&',  '*',  /* 0-9 */
    '(',  ')',  '_',  '+', '\b', '\t', 'Q',  'W',  'E',  'R',  /* 10-19 */
    'T',  'Y',  'U',  'I',  'O',  'P',  '{',  '}', '\n',   0,  /* 20-29 */
    'A',  'S',  'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',  /* 30-39 */
    '"',  '~',    0,  '|',  'Z',  'X',  'C',  'V',  'B',  'N',  /* 40-49 */
    'M',  '<',  '>',  '?',    0,  '*',    0,  ' ',    0,    0,  /* 50-59 (Numpad * , Space) */
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,  /* 60-69 F-keys etc. */
    0,    0,    0,    0,  '-',    0,    0,    0,  '+',    0,  /* 70-79 (Numpad -, Numpad +) */
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,  /* 80-89 */
    // ... (fill remaining with 0 or appropriate shifted chars for other keys if needed) ...
    // Fill up to 127 with 0
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 90-109 */
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0      /* 110-127 */
};
const char extended_scancode_table[128] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '\n', 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* Keyboard interrupt handler */
extern "C" void keyboard_handler() {
    uint8_t scancode = inb(0x60);

    // 1. Handle 0xE0 prefix for extended keys
    if (scancode == 0xE0) {
        extended_key = true; // Set flag for the *next* scancode
        outb(0x20, 0x20);    // Send EOI to PIC
        return;
    }

    // 2. Handle Modifier Key States (Shift)
    if (scancode == SCANCODE_LSHIFT_PRESS || scancode == SCANCODE_RSHIFT_PRESS) {
        shift_pressed = true;
        outb(0x20, 0x20); // Send EOI
        return;
    }
    if (scancode == SCANCODE_LSHIFT_RELEASE || scancode == SCANCODE_RSHIFT_RELEASE) {
        shift_pressed = false;
        outb(0x20, 0x20); // Send EOI
        return;
    }
    // (Future: Add Ctrl/Alt state handling here if needed)

    // 3. Handle Key Release for non-modifier keys
    // If it's a release code (top bit set) and not a Shift release (already handled).
    if (scancode & 0x80) {
        if (extended_key) {
            // This is the release of an extended key (e.g., E0 followed by make_code + 0x80).
            // We don't typically "type" anything on release of arrow keys, etc.
            // The make code of the extended key would have already reset extended_key.
            // Or, if this is an unexpected E0 sequence (e.g. E0 then release of normal key),
            // resetting extended_key here is a safeguard.
            extended_key = false; 
        }
        // For any other non-modifier key release, just send EOI.
        outb(0x20, 0x20); // Send EOI
        return;
    }

    // 4. Handle Make Codes for Extended Keys (if `extended_key` is true)
    // At this point, `scancode` is a make code, and `extended_key` might be true.
    if (extended_key) {
        // `scancode` is the actual key code following the 0xE0 prefix.
        // The top bit of `scancode` should NOT be set if it's a make code here.
        switch (scancode) {
            case SCANCODE_UP:
                // cin.navigateHistory(true); // Uncomment if history navigation is desired
                break;
            case SCANCODE_DOWN:
                // cin.navigateHistory(false); // Uncomment if history navigation is desired
                break;
            // Handle other extended keys if necessary.
            // char ext_char = extended_scancode_table[scancode]; // Original had this table.
            // if (ext_char == '\n') { /* ... process like enter ... */ }
        }
        extended_key = false; // IMPORTANT: Reset after processing the *make code* of the extended key.
        outb(0x20, 0x20);     // Send EOI
        return;
    }

    // 5. Handle Make Codes for Normal Keys (non-extended, non-modifier make codes)
    // At this point, `scancode` is a make code for a normal key, and `extended_key` is false.
    char key_to_process;
    if (shift_pressed) {
        if (scancode < 128) key_to_process = scancode_to_ascii_shifted[scancode];
        else key_to_process = 0; // Invalid scancode index
    } else {
        if (scancode < 128) key_to_process = scancode_to_ascii[scancode];
        else key_to_process = 0; // Invalid scancode index
    }

    if (key_to_process != 0) { // If it's a printable character or \n, \b, \t
        if (key_to_process == '\n') {
            terminal_putchar(key_to_process);      // Display newline
            input_buffer[input_length] = '\0'; // Null-terminate the command

            // Notify TerminalInput about the command (assuming 'cin' is the global TerminalInput instance)
            cin.setInputReady(input_buffer);

            input_length = 0; // Reset for the next command
            // No need to clear input_buffer here as setInputReady copies it.
        } else if (key_to_process == '\b') { // Handle backspace
            if (input_length > 0) {
                input_length--;
                // input_buffer[input_length] = '\0'; // Buffer content updated
                terminal_putchar(key_to_process);   // Tell terminal to visually backspace
            }
        } else if (input_length < (MAX_COMMAND_LENGTH - 1)) { // Check buffer space (leave 1 for null)
            input_buffer[input_length++] = key_to_process;
            terminal_putchar(key_to_process); // Display the character
        }
    }

    outb(0x20, 0x20); // Send EOI to PIC
}

/* Timer interrupt handler */
extern "C" void timer_handler() {
    // Blink cursor
    update_cursor_state();

    // Send EOI to PIC
    outb(0x20, 0x20);
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