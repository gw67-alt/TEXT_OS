#ifndef DRIVE_H
#define DRIVE_H

// Initialize SATA subsystem
void init_sata_drives();

// List detected SATA drives
void cmd_list_sata_drives();

#endif

/* I/O port functions */
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}