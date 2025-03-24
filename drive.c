#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include "hardware_specs.h"
#include "driver.h"

// PCI Configuration Space registers
#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

// PCI Configuration registers offsets
#define PCI_VENDOR_ID           0x00
#define PCI_DEVICE_ID           0x02
#define PCI_COMMAND             0x04
#define PCI_STATUS              0x06
#define PCI_REVISION_ID         0x08
#define PCI_PROG_IF             0x09
#define PCI_SUBCLASS            0x0A
#define PCI_CLASS               0x0B
#define PCI_CACHE_LINE_SIZE     0x0C
#define PCI_LATENCY_TIMER       0x0D
#define PCI_HEADER_TYPE         0x0E
#define PCI_BIST                0x0F
#define PCI_BAR0                0x10
#define PCI_BAR1                0x14
#define PCI_BAR2                0x18
#define PCI_BAR3                0x1C
#define PCI_BAR4                0x20
#define PCI_BAR5                0x24

// PCI Command Register bits
#define PCI_CMD_IO_SPACE        0x0001
#define PCI_CMD_MEMORY_SPACE    0x0002
#define PCI_CMD_BUS_MASTER      0x0004

// PCI Class codes
#define PCI_CLASS_MASS_STORAGE  0x01
#define PCI_SUBCLASS_SATA       0x06
#define PCI_PROG_IF_AHCI        0x01

// AHCI Register definitions
#define AHCI_GHC_HR             (1 << 0)    // HBA Reset
#define AHCI_GHC_IE             (1 << 1)    // Interrupt Enable
#define AHCI_GHC_AE             (1 << 31)   // AHCI Enable

// Port Commands
#define AHCI_PORT_CMD_ST        (1 << 0)    // Start
#define AHCI_PORT_CMD_FRE       (1 << 4)    // FIS Receive Enable
#define AHCI_PORT_CMD_FR        (1 << 14)   // FIS Receive Running
#define AHCI_PORT_CMD_CR        (1 << 15)   // Command List Running

// AHCI Port Status
#define AHCI_PORT_IPM_ACTIVE    0x1
#define AHCI_PORT_DET_PRESENT   0x3

// FIS Types
#define FIS_TYPE_REG_H2D        0x27
#define FIS_TYPE_REG_D2H        0x34
#define FIS_TYPE_DMA_ACT        0x39
#define FIS_TYPE_DMA_SETUP      0x41
#define FIS_TYPE_DATA           0x46
#define FIS_TYPE_BIST           0x58
#define FIS_TYPE_PIO_SETUP      0x5F
#define FIS_TYPE_DEV_BITS       0xA1

// ATA Commands
#define ATA_CMD_IDENTIFY        0xEC
#define ATA_CMD_IDENTIFY_PACKET 0xA1

// Memory structures sizes
#define AHCI_CMD_LIST_SIZE      1024    // Command list (1KB aligned)
#define AHCI_FIS_SIZE           256     // FIS buffer (256B aligned)
#define AHCI_CMD_TABLE_SIZE     256     // Command table (128B aligned + PRDT size)

// AHCI Controller limits
#define MAX_SATA_DEVICES        16
#define MAX_SATA_CONTROLLERS    8

// PRDT entry count for IDENTIFY command (512 bytes = 1 sector)
#define AHCI_PRDT_COUNT         1

// Command list structure
typedef struct {
    uint32_t dw0;      // Command Header DW0
    uint32_t dw1;      // Command Header DW1
    uint32_t ctba;     // Command Table Base Address
    uint32_t ctbau;    // Command Table Base Address Upper 32-bits
    uint32_t reserved[4];
} __attribute__((packed)) hba_cmd_header_t;

// FIS structures
typedef struct {
    uint8_t fis_type;          // FIS_TYPE_REG_H2D
    uint8_t pmport:4;          // Port multiplier
    uint8_t reserved0:3;       // Reserved
    uint8_t c:1;               // 1: Command, 0: Control
    uint8_t command;           // Command register
    uint8_t featurel;          // Feature register, 7:0
    uint8_t lba0;              // LBA low register, 7:0
    uint8_t lba1;              // LBA mid register, 15:8
    uint8_t lba2;              // LBA high register, 23:16
    uint8_t device;            // Device register
    uint8_t lba3;              // LBA register, 31:24
    uint8_t lba4;              // LBA register, 39:32
    uint8_t lba5;              // LBA register, 47:40
    uint8_t featureh;          // Feature register, 15:8
    uint8_t countl;            // Count register, 7:0
    uint8_t counth;            // Count register, 15:8
    uint8_t icc;               // Isochronous command completion
    uint8_t control;           // Control register
    uint8_t reserved1[4];      // Reserved
} __attribute__((packed)) fis_reg_h2d_t;

// Received FIS structure
typedef volatile struct {
    uint8_t dsfis[28];         // DMA Setup FIS
    uint8_t reserved0[4];
    uint8_t psfis[20];         // PIO Setup FIS
    uint8_t reserved1[12];
    uint8_t rfis[20];          // D2H Register FIS
    uint8_t reserved2[4];
    uint8_t sdbfis[8];         // Set Device Bits FIS
    uint8_t ufis[64];          // Unknown FIS
    uint8_t reserved3[96];
} __attribute__((packed)) hba_fis_t;

// Physical Region Descriptor Table Entry
typedef struct {
    uint32_t dba;              // Data Base Address
    uint32_t dbau;             // Data Base Address Upper 32-bits
    uint32_t reserved0;
    uint32_t dbc:22;           // Byte Count (bit 21:0)
    uint32_t reserved1:9;      // Reserved (bit 30:22)
    uint32_t i:1;              // Interrupt on Completion (bit 31)
} __attribute__((packed)) hba_prdt_entry_t;

// Command Table
typedef struct {
    uint8_t cfis[64];          // Command FIS
    uint8_t acmd[16];          // ATAPI Command
    uint8_t reserved[48];      // Reserved
    hba_prdt_entry_t prdt[AHCI_PRDT_COUNT]; // PRDT entries
} __attribute__((packed)) hba_cmd_tbl_t;

// HBA port registers (AHCI spec)
typedef volatile struct {
    uint32_t clb;       // Command List Base Address
    uint32_t clbu;      // Command List Base Address Upper 32 bits
    uint32_t fb;        // FIS Base Address
    uint32_t fbu;       // FIS Base Address Upper 32 bits
    uint32_t is;        // Interrupt Status
    uint32_t ie;        // Interrupt Enable
    uint32_t cmd;       // Command and Status
    uint32_t reserved0; // Reserved
    uint32_t tfd;       // Task File Data
    uint32_t sig;       // Signature
    uint32_t ssts;      // SATA Status (SCR0:SStatus)
    uint32_t sctl;      // SATA Control (SCR2:SControl)
    uint32_t serr;      // SATA Error (SCR1:SError)
    uint32_t sact;      // SATA Active (SCR3:SActive)
    uint32_t ci;        // Command Issue
    uint32_t sntf;      // SATA Notification
    uint32_t fbs;       // FIS-based Switching Control
    uint32_t reserved1[11]; // Reserved
    uint32_t vendor[4]; // Vendor specific
} __attribute__((packed)) hba_port_t;

// HBA Memory Registers (AHCI spec)
typedef volatile struct {
    // Generic Host Control
    uint32_t cap;       // Host Capabilities
    uint32_t ghc;       // Global Host Control
    uint32_t is;        // Interrupt Status
    uint32_t pi;        // Ports Implemented
    uint32_t vs;        // Version
    uint32_t ccc_ctl;   // Command Completion Coalescing Control
    uint32_t ccc_ports; // Command Completion Coalescing Ports
    uint32_t em_loc;    // Enclosure Management Location
    uint32_t em_ctl;    // Enclosure Management Control
    uint32_t cap2;      // Host Capabilities Extended
    uint32_t bohc;      // BIOS/OS Handoff Control and Status
    uint8_t reserved[0xA0-0x2C]; // Reserved
    uint8_t vendor[0x100-0xA0];  // Vendor specific
    hba_port_t ports[32]; // Port control registers
} __attribute__((packed)) hba_mem_t;

// SATA controller information
typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint32_t bar5;  // AHCI Base Memory Register (BAR5)
    hba_mem_t* abar; // Memory-mapped ABAR
} sata_controller_t;

// SATA device information
typedef struct {
    uint8_t controller_idx;
    uint8_t port;
    uint8_t type;   // 0 = not connected, 1 = SATA, 2 = SATAPI
    char model[41]; // Model string (40 characters + null terminator)
    char serial[21]; // Serial number (20 characters + null terminator)
    uint32_t size_mb; // Size in megabytes (approximate)
} sata_device_t;

// Command buffer structures - aligned properly
// Note: In a real OS, these would be dynamically allocated with proper alignment
// We're using static allocation for simplicity
__attribute__((aligned(1024))) uint8_t cmd_list_buffer[MAX_SATA_CONTROLLERS][32][AHCI_CMD_LIST_SIZE];
__attribute__((aligned(256))) uint8_t fis_buffer[MAX_SATA_CONTROLLERS][32][AHCI_FIS_SIZE];
__attribute__((aligned(128))) uint8_t cmd_table_buffer[MAX_SATA_CONTROLLERS][32][AHCI_CMD_TABLE_SIZE];

// IDENTIFY command data buffer (512 bytes)
__attribute__((aligned(16))) uint16_t identify_data[256];

// Global variables to store detected devices
sata_controller_t sata_controllers[MAX_SATA_CONTROLLERS];
uint8_t num_sata_controllers = 0;

sata_device_t sata_devices[MAX_SATA_DEVICES];
uint8_t num_sata_devices = 0;

// External functions from your kernel
extern void terminal_writestring(const char* data);
extern void terminal_setcolor(uint8_t color);

// Function declarations
static uint32_t pci_config_read_dword(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
static uint16_t pci_config_read_word(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
static uint8_t pci_config_read_byte(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
static void pci_config_write_dword(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value);
static bool ahci_port_detect_device(hba_port_t* port);
static uint8_t ahci_port_get_device_type(hba_port_t* port);
static int ahci_port_initialize(sata_controller_t* controller, int port_idx);
static void ahci_port_start(hba_port_t* port);
static void ahci_port_stop(hba_port_t* port);
static int ahci_port_send_command(sata_controller_t* controller, int port_idx, int slot, uint8_t cmd, uint64_t lba, uint16_t count, void* buffer);
static void ahci_port_identify(sata_controller_t* controller, int port_idx, sata_device_t* device);
static void ahci_init_controller(sata_controller_t* controller);
static void scan_pci_for_ahci();
static void extract_identify_string(uint16_t* identify_data, int offset, int length, char* output);
void sprintf(char* buffer, const char* format, ...);

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

// Function to read from PCI configuration space
static uint32_t pci_config_read_dword(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t address = (1UL << 31) | // Enable bit
                      ((uint32_t)bus << 16) |
                      ((uint32_t)device << 11) |
                      ((uint32_t)function << 8) |
                      (offset & 0xFC);
    
    outb(PCI_CONFIG_ADDRESS, address);
    return inb(PCI_CONFIG_DATA);
}

// Function to write to PCI configuration space
static void pci_config_write_dword(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value) {
    uint32_t address = (1UL << 31) | // Enable bit
                      ((uint32_t)bus << 16) |
                      ((uint32_t)device << 11) |
                      ((uint32_t)function << 8) |
                      (offset & 0xFC);
    
    outb(PCI_CONFIG_ADDRESS, address);
    outb(PCI_CONFIG_DATA, value);
}

// Function to read a word (16-bit value) from PCI configuration space
static uint16_t pci_config_read_word(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t dword = pci_config_read_dword(bus, device, function, offset);
    return (dword >> ((offset & 2) * 8)) & 0xFFFF;
}

// Function to read a byte (8-bit value) from PCI configuration space
static uint8_t pci_config_read_byte(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t dword = pci_config_read_dword(bus, device, function, offset);
    return (dword >> ((offset & 3) * 8)) & 0xFF;
}

// Check if a port has a device connected
static bool ahci_port_detect_device(hba_port_t* port) {
    uint32_t ssts = port->ssts;
    uint8_t ipm = (ssts >> 8) & 0x0F;  // Interface Power Management
    uint8_t det = ssts & 0x0F;         // Device Detection
    
    // Check if device is present and communication is established
    return (det == AHCI_PORT_DET_PRESENT && ipm == AHCI_PORT_IPM_ACTIVE);
}

// Determine device type (SATA or SATAPI) based on signature
static uint8_t ahci_port_get_device_type(hba_port_t* port) {
    uint32_t sig = port->sig;
    
    // Check signature value
    if (sig == 0x00000101) {
        return 1; // SATA drive
    } else if (sig == 0xEB140101) {
        return 2; // SATAPI drive
    } else {
        return 0; // Unknown or not present
    }
}

// Initialize a port for command processing
static int ahci_port_initialize(sata_controller_t* controller, int port_idx) {
    hba_port_t* port = &controller->abar->ports[port_idx];
    
    // Stop command processing first
    ahci_port_stop(port);
    
    // Set up command list and FIS buffers
    port->clb = (uint32_t)&cmd_list_buffer[controller - sata_controllers][port_idx];
    port->clbu = 0; // Upper 32 bits (we're in 32-bit mode)
    
    port->fb = (uint32_t)&fis_buffer[controller - sata_controllers][port_idx];
    port->fbu = 0; // Upper 32 bits
    
    // Clear command list buffer
    memset(&cmd_list_buffer[controller - sata_controllers][port_idx], 0, AHCI_CMD_LIST_SIZE);
    
    // Clear FIS buffer
    memset(&fis_buffer[controller - sata_controllers][port_idx], 0, AHCI_FIS_SIZE);
    
    // Initialize command headers
    hba_cmd_header_t* cmd_header = (hba_cmd_header_t*)&cmd_list_buffer[controller - sata_controllers][port_idx];
    for (int i = 0; i < 32; i++) {
        cmd_header[i].ctba = (uint32_t)&cmd_table_buffer[controller - sata_controllers][port_idx];
        cmd_header[i].ctbau = 0; // Upper 32 bits
        
        // Clear command table
        memset(&cmd_table_buffer[controller - sata_controllers][port_idx], 0, AHCI_CMD_TABLE_SIZE);
    }
    
    // Start command processing
    ahci_port_start(port);
    
    return 0;
}

// Start command processing on a port
static void ahci_port_start(hba_port_t* port) {
    // Wait for CR (Command List Running) to clear
    while (port->cmd & AHCI_PORT_CMD_CR);
    
    // Set FRE (FIS Receive Enable) and ST (Start)
    port->cmd |= AHCI_PORT_CMD_FRE;
    port->cmd |= AHCI_PORT_CMD_ST;
}

// Stop command processing on a port
static void ahci_port_stop(hba_port_t* port) {
    // Clear ST (Start)
    port->cmd &= ~AHCI_PORT_CMD_ST;
    
    // Wait for CR (Command List Running) to clear
    while (port->cmd & AHCI_PORT_CMD_CR);
    
    // Clear FRE (FIS Receive Enable)
    port->cmd &= ~AHCI_PORT_CMD_FRE;
    
    // Wait for FR (FIS Receive Running) to clear
    while (port->cmd & AHCI_PORT_CMD_FR);
}

// Send a command to a port
static int ahci_port_send_command(sata_controller_t* controller, int port_idx, int slot, uint8_t cmd, uint64_t lba, uint16_t count, void* buffer) {
    hba_port_t* port = &controller->abar->ports[port_idx];
    
    // Wait until command slot is available
    while ((port->tfd & (1 << 7)) && (port->tfd & (1 << 3)));
    
    // Set up command header
    hba_cmd_header_t* cmd_header = (hba_cmd_header_t*)&cmd_list_buffer[controller - sata_controllers][port_idx];
    cmd_header[slot].dw0 = (sizeof(fis_reg_h2d_t) / 4) | (1 << 7); // CFL = 5 (5 DWORDs), Write bit set if writing
    cmd_header[slot].dw1 = 0; // No special flags
    
    // One PRDT entry for now
    cmd_header[slot].dw0 |= AHCI_PRDT_COUNT << 16;
    
    // Set up command table
    hba_cmd_tbl_t* cmd_tbl = (hba_cmd_tbl_t*)&cmd_table_buffer[controller - sata_controllers][port_idx];
    memset(cmd_tbl, 0, sizeof(hba_cmd_tbl_t));
    
    // Set up PRDT (Physical Region Descriptor Table)
    cmd_tbl->prdt[0].dba = (uint32_t)buffer;
    cmd_tbl->prdt[0].dbau = 0; // Upper 32 bits (we're in 32-bit mode)
    cmd_tbl->prdt[0].dbc = 512 - 1; // 512 bytes (size - 1)
    cmd_tbl->prdt[0].i = 1; // Interrupt on completion
    
    // Set up command FIS (Frame Information Structure)
    fis_reg_h2d_t* cmdfis = (fis_reg_h2d_t*)&cmd_tbl->cfis;
    memset(cmdfis, 0, sizeof(fis_reg_h2d_t));
    
    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1; // This is a command
    cmdfis->command = cmd;
    
    // Set up LBA if needed (for read/write commands)
    cmdfis->lba0 = (uint8_t)lba;
    cmdfis->lba1 = (uint8_t)(lba >> 8);
    cmdfis->lba2 = (uint8_t)(lba >> 16);
    cmdfis->device = 1 << 6; // LBA mode
    
    cmdfis->lba3 = (uint8_t)(lba >> 24);
    cmdfis->lba4 = (uint8_t)(lba >> 32);
    cmdfis->lba5 = (uint8_t)(lba >> 40);
    
    // Set sector count if needed
    cmdfis->countl = count & 0xFF;
    cmdfis->counth = (count >> 8) & 0xFF;
    
    // Issue command
    port->ci = 1 << slot;
    
    // Wait for command completion
    while (1) {
        // Check if command is still being processed
        if ((port->ci & (1 << slot)) == 0) {
            break;
        }
        
        // Check for error
        if (port->is & (1 << 30)) {
            return -1; // Error occurred
        }
    }
    
    // Check for errors
    if (port->is & (1 << 30)) {
        return -1; // Error occurred
    }
    
    return 0;
}

// Extract string from IDENTIFY data (handles byte-swapping)
static void extract_identify_string(uint16_t* identify_data, int offset, int length, char* output) {
    // ATA strings are space-padded and may not be null-terminated
    int i;
    
    // IDENTIFY strings are byte-swapped (little endian)
    for (i = 0; i < length / 2; i++) {
        uint16_t word = identify_data[offset + i];
        
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

// Send IDENTIFY command to get drive info
static void ahci_port_identify(sata_controller_t* controller, int port_idx, sata_device_t* device) {
    // Clear the buffer
    memset(identify_data, 0, sizeof(identify_data));
    
    // Send the IDENTIFY command to the device
    int result = ahci_port_send_command(controller, port_idx, 0, 
                                       (device->type == 1) ? ATA_CMD_IDENTIFY : ATA_CMD_IDENTIFY_PACKET, 
                                       0, 0, identify_data);
    
    if (result == 0) {
        // Extract model name from words 27-46
        extract_identify_string(identify_data, 27, 40, device->model);
        
        // Extract serial number from words 10-19
        extract_identify_string(identify_data, 10, 20, device->serial);
        
        // Calculate approximate size
        // Words 60-61 contain the total number of 28-bit LBA addressable sectors
        // Words 100-103 contain the total number of 48-bit addressable sectors
        uint32_t sectors = 0;
        
        // Check if 48-bit LBA is supported
        if (identify_data[83] & (1 << 10)) {
            // Use 48-bit LBA sector count (words 100-103)
            sectors = identify_data[100] | (identify_data[101] << 16);
            
            // If upper 32 bits are non-zero, just set to max capacity to avoid overflow
            if (identify_data[102] || identify_data[103]) {
                device->size_mb = 0xFFFFFFFF / 2048; // Just show a large number
            } else {
                // Convert sectors to MB (sector = 512 bytes, MB = 1048576 bytes)
                device->size_mb = sectors / 2048;
            }
        } else {
            // Use 28-bit LBA sector count (words 60-61)
            sectors = identify_data[60] | (identify_data[61] << 16);
            
            // Convert sectors to MB
            device->size_mb = sectors / 2048;
        }
    } else {
        // If IDENTIFY command failed, set placeholder values
        strcpy(device->model, "Unknown Model");
        strcpy(device->serial, "Unknown Serial");
        device->size_mb = 0;
    }
}

// Initialize and detect SATA devices on an AHCI controller
static void ahci_init_controller(sata_controller_t* controller) {
    // Get the base address from BAR5
    uint32_t abar_addr = controller->bar5 & 0xFFFFFFF0; // Clear lower 4 bits (flags)
    
    // Map ABAR (in a real OS, this would involve memory mapping)
    // For demonstration purposes, we use the physical address directly
    controller->abar = (hba_mem_t*)abar_addr;
    
    // Enable PCI Bus Mastering
    uint16_t command = pci_config_read_word(controller->bus, controller->device, controller->function, PCI_COMMAND);
    command |= PCI_CMD_BUS_MASTER | PCI_CMD_MEMORY_SPACE;
    pci_config_write_dword(controller->bus, controller->device, controller->function, PCI_COMMAND, command);
    
    // Enable AHCI mode
    controller->abar->ghc |= AHCI_GHC_AE;
    
    // Check which ports are implemented
    uint32_t ports_implemented = controller->abar->pi;
    
    // Scan all 32 possible ports
    for (int port_idx = 0; port_idx < 32; port_idx++) {
        // Skip if this port is not implemented
        if (!(ports_implemented & (1 << port_idx))) {
            continue;
        }
        
        // Get pointer to the port structure
        hba_port_t* port = &controller->abar->ports[port_idx];
        
        // Check if a device is connected to this port
        if (ahci_port_detect_device(port)) {
            // Determine device type
            uint8_t device_type = ahci_port_get_device_type(port);
            
            // Skip if unknown device type
            if (device_type == 0) {
                continue;
            }
            
            // Initialize port for command processing
            ahci_port_initialize(controller, port_idx);
            
            // Store device information if we have space
            if (num_sata_devices < MAX_SATA_DEVICES) {
                sata_device_t* device = &sata_devices[num_sata_devices];
                
                device->controller_idx = controller - sata_controllers;
                device->port = port_idx;
                device->type = device_type;
                
                // Get detailed device information
                ahci_port_identify(controller, port_idx, device);
                
                num_sata_devices++;
            }
        }
    }
    
    // Log initialization completion
    char buffer[80];
    sprintf(buffer, "SATA controller initialized: VID=%04X, DID=%04X\n", 
            controller->vendor_id, controller->device_id);
    terminal_writestring(buffer);
}
// Scan PCI bus for AHCI controllers
static void scan_pci_for_ahci() {
    // Reset counter
    num_sata_controllers = 0;
    
    terminal_writestring("Scanning PCI bus for SATA controllers...\n");
    
    // Scan all PCI buses
    for (uint16_t bus = 0; bus < 256; bus++) {
        // Scan all devices on this bus
        for (uint8_t device = 0; device < 32; device++) {
            // Check only function 0 of each device for simplicity
            uint8_t function = 0;
            
            // Read vendor ID
            uint16_t vendor_id = pci_config_read_word(bus, device, function, PCI_VENDOR_ID);
            
            // If vendor ID is valid (not 0xFFFF), this device exists
            if (vendor_id != 0xFFFF) {
                // Read device ID
                uint16_t device_id = pci_config_read_word(bus, device, function, PCI_DEVICE_ID);
                
                // Read class code, subclass, and prog IF
                uint8_t class_code = pci_config_read_byte(bus, device, function, PCI_CLASS);
                uint8_t subclass = pci_config_read_byte(bus, device, function, PCI_SUBCLASS);
                uint8_t prog_if = pci_config_read_byte(bus, device, function, PCI_PROG_IF);
                
                // Check if this is an AHCI SATA controller
                if (class_code == PCI_CLASS_MASS_STORAGE && 
                    subclass == PCI_SUBCLASS_SATA && 
                    prog_if == PCI_PROG_IF_AHCI) {
                    
                    // Store this controller if we have space
                    if (num_sata_controllers < MAX_SATA_CONTROLLERS) {
                        sata_controller_t* controller = &sata_controllers[num_sata_controllers];
                        
                        controller->bus = bus;
                        controller->device = device;
                        controller->function = function;
                        controller->vendor_id = vendor_id;
                        controller->device_id = device_id;
                        controller->class_code = class_code;
                        controller->subclass = subclass;
                        controller->prog_if = prog_if;
                        
                        // Read BAR5 (AHCI Base Address Register)
                        controller->bar5 = pci_config_read_dword(bus, device, function, PCI_BAR5);
                        
                        // Output detected controller info
                        char buffer[100];
                        sprintf(buffer, "Found SATA controller at PCI %d:%d:%d (VID:0x%04X, DID:0x%04X)\n", 
                                bus, device, function, vendor_id, device_id);
                        terminal_writestring(buffer);
                        
                        num_sata_controllers++;
                    } else {
                        terminal_writestring("Warning: Maximum number of SATA controllers reached\n");
                        return;
                    }
                }
            }
        }
    }
    
    if (num_sata_controllers == 0) {
        terminal_writestring("No SATA controllers found on PCI bus\n");
    } else {
        char buffer[50];
        sprintf(buffer, "Found %d SATA controller(s)\n", num_sata_controllers);
        terminal_writestring(buffer);
    }
}

// Initialize SATA subsystem
void init_sata_drives() {
    // Reset device counter
    num_sata_devices = 0;
    
    // Output initialization start message
    terminal_writestring("Initializing SATA subsystem...\n");
    
    // Scan PCI bus for AHCI controllers
    scan_pci_for_ahci();
    
    if (num_sata_controllers == 0) {
        terminal_writestring("No SATA controllers found.\n");
        return;
    }
    
    // Initialize each SATA controller
    for (uint8_t i = 0; i < num_sata_controllers; i++) {
        ahci_init_controller(&sata_controllers[i]);
    }
    
    // Output initialization status
    char buffer[80];
    sprintf(buffer, "SATA: Found %d controllers, %d devices\n", num_sata_controllers, num_sata_devices);
    terminal_writestring(buffer);
    
    if (num_sata_devices > 0) {
        terminal_writestring("Drive detection complete. Use 'lsdrive' to view details.\n");
    }
}