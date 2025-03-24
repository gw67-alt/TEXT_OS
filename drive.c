#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include "driver.h"

// SATA controller I/O port base addresses
// These would need to be adjusted for your specific hardware
#define SATA_PRIMARY_CMD_BASE   0x1F0
#define SATA_PRIMARY_CTRL_BASE  0x3F6
#define SATA_SECONDARY_CMD_BASE 0x170
#define SATA_SECONDARY_CTRL_BASE 0x376

// SATA registers (offsets from command base)
#define SATA_REG_DATA       0x00
#define SATA_REG_ERROR      0x01
#define SATA_REG_FEATURES   0x01
#define SATA_REG_SECCOUNT   0x02
#define SATA_REG_LBA_LO     0x03
#define SATA_REG_LBA_MID    0x04
#define SATA_REG_LBA_HI     0x05
#define SATA_REG_DEVICE     0x06
#define SATA_REG_STATUS     0x07
#define SATA_REG_COMMAND    0x07

// SATA control register (offset from control base)
#define SATA_REG_CONTROL    0x00
#define SATA_REG_ALTSTATUS  0x00

// SATA status register bits
#define SATA_STATUS_BSY     0x80
#define SATA_STATUS_DRDY    0x40
#define SATA_STATUS_DRQ     0x08
#define SATA_STATUS_ERR     0x01

// SATA control register bits
#define SATA_CONTROL_SRST   0x04  // Software reset
#define SATA_CONTROL_nIEN   0x02  // Interrupt disable

// SATA device register bits
#define SATA_DEVICE_LBA     0x40  // Use LBA addressing

// SATA commands
#define SATA_CMD_IDENTIFY   0xEC
#define SATA_CMD_READ_SECTORS 0x20
#define SATA_CMD_WRITE_SECTORS 0x30

// Maximum number of SATA drives to support
#define MAX_SATA_DRIVES 4

// Drive information structure
typedef struct {
    bool present;
    uint16_t io_base;      // I/O base address
    uint16_t control_base; // Control base address
    char model[41];        // Model name (40 chars + null)
    char serial[21];       // Serial number (20 chars + null)
    uint32_t size_mb;      // Size in MB
} sata_drive_t;

// Global array of drives
sata_drive_t sata_drives[MAX_SATA_DRIVES];
uint8_t num_sata_drives = 0;

// Buffer for IDENTIFY command (256 words = 512 bytes)
uint16_t identify_data[256];

// Function prototypes
extern void terminal_writestring(const char* data);
static bool sata_reset_controller(uint16_t control_base);
static bool sata_wait_not_busy(uint16_t io_base);
static bool sata_select_drive(uint16_t io_base, uint8_t drive);
static bool sata_identify_drive(uint16_t io_base, uint16_t control_base, uint8_t drive);
static void extract_string(uint16_t* data, int offset, int length, char* output);
void sprintf(char* buffer, const char* format, ...);

// I/O port functions - these need to be defined or imported from your kernel
extern uint8_t inb(uint16_t port);
extern void outb(uint16_t port, uint8_t value);
extern uint16_t inw(uint16_t port);

// Read 16-bit value from I/O port if not defined elsewhere
#ifndef inw
inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
#endif

// Helper function for string formatting
void sprintf(char* buffer, const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    char* buf_ptr = buffer;
    
    while (*format != '\0') {
        if (*format == '%') {
            format++;
            
            // Handle different format specifiers
            switch (*format) {
                case 'd': {
                    // Decimal integer
                    int value = va_arg(args, int);
                    char temp[12]; // Enough for 32-bit int
                    itoa(value, temp, 10);
                    
                    // Copy to buffer
                    char* temp_ptr = temp;
                    while (*temp_ptr) {
                        *buf_ptr++ = *temp_ptr++;
                    }
                    break;
                }
                
                case 'x': {
                    // Hexadecimal (lowercase)
                    int value = va_arg(args, int);
                    char temp[12]; // Enough for 32-bit int
                    itoa(value, temp, 16);
                    
                    // Copy to buffer
                    char* temp_ptr = temp;
                    while (*temp_ptr) {
                        *buf_ptr++ = *temp_ptr++;
                    }
                    break;
                }
                
                case 'X': {
                    // Hexadecimal (uppercase)
                    int value = va_arg(args, int);
                    char temp[12]; // Enough for 32-bit int
                    itoa(value, temp, 16);
                    
                    // Convert to uppercase and copy to buffer
                    char* temp_ptr = temp;
                    while (*temp_ptr) {
                        *buf_ptr++ = toupper(*temp_ptr++);
                    }
                    break;
                }
                
                case 's': {
                    // String
                    char* str = va_arg(args, char*);
                    while (*str) {
                        *buf_ptr++ = *str++;
                    }
                    break;
                }
                
                case '%':
                    *buf_ptr++ = '%';
                    break;
            }
        } else {
            *buf_ptr++ = *format;
        }
        
        format++;
    }
    
    // Null-terminate the result
    *buf_ptr = '\0';
    
    va_end(args);
}

// Extract string from IDENTIFY data (handles byte swapping)
static void extract_string(uint16_t* data, int offset, int length, char* output) {
    int i;
    
    // IDENTIFY strings are byte-swapped (little endian)
    for (i = 0; i < length / 2; i++) {
        uint16_t word = data[offset + i];
        
        // Swap bytes (ATA strings are byte-swapped)
        output[i*2] = (word >> 8) & 0xFF;
        output[i*2 + 1] = word & 0xFF;
    }
    
    // Ensure null termination
    output[length] = '\0';
    
    // Trim trailing spaces
    for (i = length - 1; i >= 0; i--) {
        if (output[i] == ' ') {
            output[i] = '\0';
        } else {
            break;
        }
    }
}

// Reset SATA controller
static bool sata_reset_controller(uint16_t control_base) {
    // Set bit 2 (SRST) to 1 to initiate soft reset
    outb(control_base + SATA_REG_CONTROL, SATA_CONTROL_SRST);
    
    // Wait a bit (at least 5 microseconds)
    for (int i = 0; i < 1000; i++) {
        // Simple delay loop
    }
    
    // Clear SRST bit to 0 to end reset
    outb(control_base + SATA_REG_CONTROL, 0);
    
    // Wait for BSY to clear (up to 30 seconds)
    int timeout = 30000; // 30,000 milliseconds
    while (--timeout) {
        if ((inb(control_base + SATA_REG_ALTSTATUS) & SATA_STATUS_BSY) == 0) {
            return true;
        }
        
        // Delay 1ms (in a real system, you'd use a proper delay function)
        for (int i = 0; i < 10000; i++) {
            // Simple delay loop
        }
    }
    
    return false; // Timeout
}

// Wait for the selected drive to not be busy
static bool sata_wait_not_busy(uint16_t io_base) {
    // Wait for BSY to clear (timeout after about 30 seconds)
    int timeout = 30000; // 30,000 milliseconds
    while (--timeout) {
        if ((inb(io_base + SATA_REG_STATUS) & SATA_STATUS_BSY) == 0) {
            return true;
        }
        
        // Delay 1ms (in a real system, you'd use a proper delay function)
        for (int i = 0; i < 10000; i++) {
            // Simple delay loop
        }
    }
    
    return false; // Timeout
}

// Select a drive (master=0, slave=1)
static bool sata_select_drive(uint16_t io_base, uint8_t drive) {
    // Wait for drive to be ready
    if (!sata_wait_not_busy(io_base)) {
        return false;
    }
    
    // Select drive (bit 4 = 0 for master, 1 for slave)
    // Bit 6 is set for LBA mode, bit 5 is set for compatibility
    outb(io_base + SATA_REG_DEVICE, 0xA0 | (drive << 4));
    
    // Small delay to let drive respond
    for (int i = 0; i < 1000; i++) {
        // Simple delay loop
    }
    
    return true;
}

// Execute IDENTIFY command
static bool sata_identify_drive(uint16_t io_base, uint16_t control_base, uint8_t drive) {
    // Reset the controller first
    if (!sata_reset_controller(control_base)) {
        return false;
    }
    
    // Select the drive
    if (!sata_select_drive(io_base, drive)) {
        return false;
    }
    
    // Set count and LBA registers to 0 (not used for IDENTIFY)
    outb(io_base + SATA_REG_SECCOUNT, 0);
    outb(io_base + SATA_REG_LBA_LO, 0);
    outb(io_base + SATA_REG_LBA_MID, 0);
    outb(io_base + SATA_REG_LBA_HI, 0);
    
    // Send IDENTIFY command
    outb(io_base + SATA_REG_COMMAND, SATA_CMD_IDENTIFY);
    
    // Check if drive exists by reading status
    uint8_t status = inb(io_base + SATA_REG_STATUS);
    if (status == 0) {
        return false; // Drive doesn't exist
    }
    
    // Wait for data ready or error
    bool timeout = true;
    for (int i = 0; i < 1000; i++) { // Shorter timeout for initial response
        status = inb(io_base + SATA_REG_STATUS);
        
        if (status & SATA_STATUS_ERR) {
            return false; // Error occurred
        }
        
        if (status & SATA_STATUS_DRQ) {
            timeout = false;
            break; // Data is ready
        }
    }
    
    if (timeout) {
        return false; // Timeout waiting for data
    }
    
    // Read 256 words (512 bytes) of identify data
    for (int i = 0; i < 256; i++) {
        identify_data[i] = inw(io_base + SATA_REG_DATA);
    }
    
    return true;
}

// Initialize and detect SATA drives
void init_sata_drives() {
    terminal_writestring("Initializing SATA subsystem...\n");
    
    // Reset number of drives
    num_sata_drives = 0;
    
    // Initialize all drives to not present
    for (int i = 0; i < MAX_SATA_DRIVES; i++) {
        sata_drives[i].present = false;
    }
    
    // Check primary channel (master and slave)
    if (sata_identify_drive(SATA_PRIMARY_CMD_BASE, SATA_PRIMARY_CTRL_BASE, 0)) {
        // Master drive found
        if (num_sata_drives < MAX_SATA_DRIVES) {
            sata_drive_t* drive = &sata_drives[num_sata_drives];
            drive->present = true;
            drive->io_base = SATA_PRIMARY_CMD_BASE;
            drive->control_base = SATA_PRIMARY_CTRL_BASE;
            
            // Extract model and serial from identify data
            extract_string(identify_data, 27, 40, drive->model);
            extract_string(identify_data, 10, 20, drive->serial);
            
            // Calculate size (LBA sectors * 512 bytes / 1MB)
            // Words 60-61 for 28-bit LBA, words 100-103 for 48-bit LBA
            if (identify_data[83] & (1 << 10)) { // 48-bit LBA supported
                uint32_t sectors_low = identify_data[100] | (identify_data[101] << 16);
                uint32_t sectors_high = identify_data[102] | (identify_data[103] << 16);
                
                if (sectors_high > 0) {
                    drive->size_mb = 0xFFFFFFFF / 2048; // Limit to prevent overflow
                } else {
                    drive->size_mb = sectors_low / 2048; // 512 bytes * sectors / 1MB
                }
            } else {
                uint32_t sectors = identify_data[60] | (identify_data[61] << 16);
                drive->size_mb = sectors / 2048;
            }
            
            num_sata_drives++;
            
            char buffer[100];
            sprintf(buffer, "Detected Primary Master: %s\n", drive->model);
            terminal_writestring(buffer);
        }
    }
    
    if (sata_identify_drive(SATA_PRIMARY_CMD_BASE, SATA_PRIMARY_CTRL_BASE, 1)) {
        // Slave drive found
        if (num_sata_drives < MAX_SATA_DRIVES) {
            sata_drive_t* drive = &sata_drives[num_sata_drives];
            drive->present = true;
            drive->io_base = SATA_PRIMARY_CMD_BASE;
            drive->control_base = SATA_PRIMARY_CTRL_BASE;
            
            // Extract model and serial from identify data
            extract_string(identify_data, 27, 40, drive->model);
            extract_string(identify_data, 10, 20, drive->serial);
            
            // Calculate size (same as above)
            if (identify_data[83] & (1 << 10)) {
                uint32_t sectors_low = identify_data[100] | (identify_data[101] << 16);
                uint32_t sectors_high = identify_data[102] | (identify_data[103] << 16);
                
                if (sectors_high > 0) {
                    drive->size_mb = 0xFFFFFFFF / 2048;
                } else {
                    drive->size_mb = sectors_low / 2048;
                }
            } else {
                uint32_t sectors = identify_data[60] | (identify_data[61] << 16);
                drive->size_mb = sectors / 2048;
            }
            
            num_sata_drives++;
            
            char buffer[100];
            sprintf(buffer, "Detected Primary Slave: %s\n", drive->model);
            terminal_writestring(buffer);
        }
    }
    
    // Check secondary channel (master and slave)
    if (sata_identify_drive(SATA_SECONDARY_CMD_BASE, SATA_SECONDARY_CTRL_BASE, 0)) {
        // Secondary master drive found
        if (num_sata_drives < MAX_SATA_DRIVES) {
            sata_drive_t* drive = &sata_drives[num_sata_drives];
            drive->present = true;
            drive->io_base = SATA_SECONDARY_CMD_BASE;
            drive->control_base = SATA_SECONDARY_CTRL_BASE;
            
            // Extract model and serial from identify data
            extract_string(identify_data, 27, 40, drive->model);
            extract_string(identify_data, 10, 20, drive->serial);
            
            // Calculate size (same as above)
            if (identify_data[83] & (1 << 10)) {
                uint32_t sectors_low = identify_data[100] | (identify_data[101] << 16);
                uint32_t sectors_high = identify_data[102] | (identify_data[103] << 16);
                
                if (sectors_high > 0) {
                    drive->size_mb = 0xFFFFFFFF / 2048;
                } else {
                    drive->size_mb = sectors_low / 2048;
                }
            } else {
                uint32_t sectors = identify_data[60] | (identify_data[61] << 16);
                drive->size_mb = sectors / 2048;
            }
            
            num_sata_drives++;
            
            char buffer[100];
            sprintf(buffer, "Detected Secondary Master: %s\n", drive->model);
            terminal_writestring(buffer);
        }
    }
    
    if (sata_identify_drive(SATA_SECONDARY_CMD_BASE, SATA_SECONDARY_CTRL_BASE, 1)) {
        // Secondary slave drive found
        if (num_sata_drives < MAX_SATA_DRIVES) {
            sata_drive_t* drive = &sata_drives[num_sata_drives];
            drive->present = true;
            drive->io_base = SATA_SECONDARY_CMD_BASE;
            drive->control_base = SATA_SECONDARY_CTRL_BASE;
            
            // Extract model and serial from identify data
            extract_string(identify_data, 27, 40, drive->model);
            extract_string(identify_data, 10, 20, drive->serial);
            
            // Calculate size (same as above)
            if (identify_data[83] & (1 << 10)) {
                uint32_t sectors_low = identify_data[100] | (identify_data[101] << 16);
                uint32_t sectors_high = identify_data[102] | (identify_data[103] << 16);
                
                if (sectors_high > 0) {
                    drive->size_mb = 0xFFFFFFFF / 2048;
                } else {
                    drive->size_mb = sectors_low / 2048;
                }
            } else {
                uint32_t sectors = identify_data[60] | (identify_data[61] << 16);
                drive->size_mb = sectors / 2048;
            }
            
            num_sata_drives++;
            
            char buffer[100];
            sprintf(buffer, "Detected Secondary Slave: %s\n", drive->model);
            terminal_writestring(buffer);
        }
    }
    
    // Output summary
    char buffer[50];
    sprintf(buffer, "SATA initialization complete. Found %d drives.\n", num_sata_drives);
    terminal_writestring(buffer);

}

// Command to list detected SATA drives
void cmd_list_sata_drives() {
    if (num_sata_drives == 0) {
        terminal_writestring("No SATA drives found.\n");
        return;
    }
    
    terminal_writestring("Detected SATA drives:\n");
    terminal_writestring("--------------------\n");
    
    for (int i = 0; i < MAX_SATA_DRIVES; i++) {
        if (sata_drives[i].present) {
            char buffer[100];
            
            // Determine the drive's position
            const char* position;
            if (sata_drives[i].io_base == SATA_PRIMARY_CMD_BASE) {
                position = (sata_drives[i].io_base == 0) ? "Primary Master" : "Primary Slave";
            } else {
                position = (sata_drives[i].io_base == 0) ? "Secondary Master" : "Secondary Slave";
            }
            
            // Print drive info
            sprintf(buffer, "%d: %s (%s)\n", i, sata_drives[i].model, position);
            terminal_writestring(buffer);
            
            sprintf(buffer, "   Serial: %s\n", sata_drives[i].serial);
            terminal_writestring(buffer);
            
            if (sata_drives[i].size_mb >= 1024) {
                sprintf(buffer, "   Size: %d GB\n", sata_drives[i].size_mb / 1024);
            } else {
                sprintf(buffer, "   Size: %d MB\n", sata_drives[i].size_mb);
            }
            terminal_writestring(buffer);
            
            terminal_writestring("\n");
        }
    }
}