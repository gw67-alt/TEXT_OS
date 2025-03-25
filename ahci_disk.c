/*
 * AHCI Disk Implementation
 *
 * This file provides the core disk access functionality through AHCI.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include "stdio.h"
#include "io.h"
#include <string.h>

// HBA Memory Registers
#define HBA_CAP              0x00    // Host Capabilities
#define HBA_GHC              0x04    // Global Host Control
#define HBA_IS               0x08    // Interrupt Status
#define HBA_PI               0x0C    // Ports Implemented
#define HBA_VS               0x10    // Version
#define HBA_CCC_CTL          0x14    // Command Completion Coalescing Control
#define HBA_CCC_PORTS        0x18    // Command Completion Coalescing Ports
#define HBA_EM_LOC           0x1C    // Enclosure Management Location
#define HBA_EM_CTL           0x20    // Enclosure Management Control

// Port Registers (offset from port base)
#define PORT_CLB             0x00    // Command List Base Address
#define PORT_CLBU            0x04    // Command List Base Address Upper 32-bits
#define PORT_FB              0x08    // FIS Base Address
#define PORT_FBU             0x0C    // FIS Base Address Upper 32-bits
#define PORT_IS              0x10    // Interrupt Status
#define PORT_IE              0x14    // Interrupt Enable
#define PORT_CMD             0x18    // Command and Status
#define PORT_TFD             0x20    // Task File Data
#define PORT_SIG             0x24    // Signature
#define PORT_SSTS            0x28    // SATA Status
#define PORT_SCTL            0x2C    // SATA Control
#define PORT_SERR            0x30    // SATA Error
#define PORT_SACT            0x34    // SATA Active
#define PORT_CI              0x38    // Command Issue

// PORT_CMD bits
#define PORT_CMD_ST          0x0001  // Start
#define PORT_CMD_SUD         0x0002  // Spin-Up Device
#define PORT_CMD_POD         0x0004  // Power On Device
#define PORT_CMD_FRE         0x0010  // FIS Receive Enable
#define PORT_CMD_FR          0x4000  // FIS Receive Running
#define PORT_CMD_CR          0x8000  // Command List Running

// PORT_IS and PORT_IE bits
#define PORT_IS_DHRS         0x01    // Device to Host Register FIS Interrupt
#define PORT_IS_PSS          0x02    // PIO Setup FIS Interrupt
#define PORT_IS_DSS          0x04    // DMA Setup FIS Interrupt
#define PORT_IS_SDBS         0x08    // Set Device Bits FIS Interrupt
#define PORT_IS_UFS          0x10    // Unknown FIS Interrupt
#define PORT_IS_DPS          0x20    // Descriptor Processed
#define PORT_IS_PCS          0x40    // Port Connect Change Status
#define PORT_IS_DMPS         0x80    // Device Mechanical Presence Status

// FIS Types
#define FIS_TYPE_REG_H2D     0x27    // Register FIS - Host to Device
#define FIS_TYPE_REG_D2H     0x34    // Register FIS - Device to Host
#define FIS_TYPE_DMA_ACT     0x39    // DMA Activate FIS
#define FIS_TYPE_DMA_SETUP   0x41    // DMA Setup FIS
#define FIS_TYPE_DATA        0x46    // Data FIS
#define FIS_TYPE_BIST        0x58    // BIST Activate FIS
#define FIS_TYPE_PIO_SETUP   0x5F    // PIO Setup FIS
#define FIS_TYPE_DEV_BITS    0xA1    // Set Device Bits FIS

// HBA Port Structures
#define AHCI_PORT_SIZE       0x80    // Size of each port register space
#define HBA_PORT_IPM_ACTIVE  0x1     // Interface Power Management - Active
#define HBA_PORT_DET_PRESENT 0x3     // Device Detection - Device Present

// ATA Commands
#define ATA_CMD_READ_DMA_EX  0x25    // READ DMA EXT
#define ATA_CMD_WRITE_DMA_EX 0x35    // WRITE DMA EXT
#define ATA_CMD_IDENTIFY     0xEC    // IDENTIFY DEVICE

// Memory and DMA Structures
typedef struct {
    uint8_t command_fis[64];         // Command FIS buffer
    uint8_t prdt_entry[16];          // Physical Region Descriptor Table entry
} __attribute__((packed)) ahci_cmd_table_t;

typedef struct {
    uint32_t prdtl       : 16;       // Physical region descriptor table length in entries
    uint32_t pmp         : 4;        // Port multiplier port
    uint32_t clear       : 1;        // Clear busy upon R_OK
    uint32_t bist        : 1;        // BIST portion
    uint32_t reset       : 1;        // Reset
    uint32_t prefetchable: 1;        // Prefetchable
    uint32_t write       : 1;        // Write (1: H2D, 0: D2H)
    uint32_t atapi       : 1;        // ATAPI
    uint32_t cfl         : 5;        // Command FIS length
    uint32_t ctba;                   // Command table descriptor base address
    uint32_t ctbau;                  // Command table descriptor base address upper 32 bits
    uint32_t reserved[4];
} __attribute__((packed)) ahci_cmd_header_t;

typedef struct {
    uint8_t  fis_type;               // FIS_TYPE_REG_H2D
    uint8_t  pmport:4;               // Port multiplier
    uint8_t  reserved0:3;            // Reserved
    uint8_t  c:1;                    // 1: Command, 0: Control
    uint8_t  command;                // Command register
    uint8_t  featurel;               // Feature register, 7:0
    uint8_t  lba0;                   // LBA low register, 7:0
    uint8_t  lba1;                   // LBA mid register, 15:8
    uint8_t  lba2;                   // LBA high register, 23:16
    uint8_t  device;                 // Device register
    uint8_t  lba3;                   // LBA register, 31:24
    uint8_t  lba4;                   // LBA register, 39:32
    uint8_t  lba5;                   // LBA register, 47:40
    uint8_t  featureh;               // Feature register, 15:8
    uint8_t  countl;                 // Count register, 7:0
    uint8_t  counth;                 // Count register, 15:8
    uint8_t  icc;                    // Isochronous command completion
    uint8_t  control;                // Control register
    uint8_t  reserved1[4];           // Reserved
} __attribute__((packed)) fis_reg_h2d_t;

typedef struct {
    uint32_t dba;                    // Data base address
    uint32_t dbau;                   // Data base address upper 32 bits
    uint32_t reserved0;              // Reserved
    uint32_t dbc:22;                 // Byte count, 4M max
    uint32_t reserved1:9;            // Reserved
    uint32_t i:1;                    // Interrupt on completion
} __attribute__((packed)) hba_prdt_entry_t;

// Global variables
static uint32_t ahci_base_address = 0;
static int active_port = 0;
static ahci_cmd_header_t* cmd_header = NULL;
static ahci_cmd_table_t* cmd_table = NULL;
static uint8_t* received_fis = NULL;

// Memory allocation function (very simple, just returns a pointer to a static buffer)
// In a real implementation, this would be a proper memory allocator
static void* ahci_allocate_aligned(uint32_t size, uint32_t alignment) {
    static uint8_t buffer[65536];
    static uint32_t next_address = 0;
    
    // Align the address
    uint32_t aligned_address = (next_address + alignment - 1) & ~(alignment - 1);
    
    // Check if we have enough space
    if (aligned_address + size > sizeof(buffer)) {
        return NULL;
    }
    
    // Save the next address and return the current aligned address
    next_address = aligned_address + size;
    return &buffer[aligned_address];
}

// Utility function to create an H2D FIS
static void create_command_fis(fis_reg_h2d_t* fis, uint8_t command, uint64_t lba, uint16_t count) {
    memset(fis, 0, sizeof(fis_reg_h2d_t));
    
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1;                  // Command
    fis->command = command;
    
    fis->lba0 = (uint8_t)(lba & 0xFF);
    fis->lba1 = (uint8_t)((lba >> 8) & 0xFF);
    fis->lba2 = (uint8_t)((lba >> 16) & 0xFF);
    fis->lba3 = (uint8_t)((lba >> 24) & 0xFF);
    fis->lba4 = (uint8_t)((lba >> 32) & 0xFF);
    fis->lba5 = (uint8_t)((lba >> 40) & 0xFF);
    
    fis->device = 1 << 6;        // LBA mode
    
    fis->countl = count & 0xFF;
    fis->counth = (count >> 8) & 0xFF;
}

// Initialize a port for use
bool ahci_initialize_port(int port_num) {
    if (ahci_base_address == 0 || port_num < 0) {
        return false;
    }
    
    uint32_t port_addr = ahci_base_address + 0x100 + (port_num * AHCI_PORT_SIZE);
    
    // Stop command processing
    uint32_t cmd = *(volatile uint32_t*)(port_addr + PORT_CMD);
    cmd &= ~PORT_CMD_ST;
    cmd &= ~PORT_CMD_FRE;
    *(volatile uint32_t*)(port_addr + PORT_CMD) = cmd;
    
    // Wait for command processing to stop
    while (1) {
        uint32_t status = *(volatile uint32_t*)(port_addr + PORT_CMD);
        if ((status & PORT_CMD_FR) == 0 && (status & PORT_CMD_CR) == 0) {
            break;
        }
    }
    
    // Allocate memory for command list (must be 1KB aligned)
    cmd_header = (ahci_cmd_header_t*)ahci_allocate_aligned(1024, 1024);
    if (!cmd_header) {
        return false;
    }
    
    // Allocate memory for received FIS (must be 256 byte aligned)
    received_fis = (uint8_t*)ahci_allocate_aligned(256, 256);
    if (!received_fis) {
        return false;
    }
    
    // Allocate memory for command table (must be 128 byte aligned)
    cmd_table = (ahci_cmd_table_t*)ahci_allocate_aligned(sizeof(ahci_cmd_table_t), 128);
    if (!cmd_table) {
        return false;
    }
    
    // Clear the command list and FIS buffer
    memset(cmd_header, 0, 1024);
    memset(received_fis, 0, 256);
    memset(cmd_table, 0, sizeof(ahci_cmd_table_t));
    
    // Set up command header's command table base address
    cmd_header[0].ctba = (uint32_t)cmd_table;
    cmd_header[0].ctbau = 0;     // Upper 32 bits are 0 in a 32-bit system
    
    // Set up port registers
    *(volatile uint32_t*)(port_addr + PORT_CLB) = (uint32_t)cmd_header;
    *(volatile uint32_t*)(port_addr + PORT_CLBU) = 0;
    *(volatile uint32_t*)(port_addr + PORT_FB) = (uint32_t)received_fis;
    *(volatile uint32_t*)(port_addr + PORT_FBU) = 0;
    
    // Clear pending interrupts
    *(volatile uint32_t*)(port_addr + PORT_IS) = (uint32_t)-1;
    
    // Enable FIS receive and start command processing
    cmd = *(volatile uint32_t*)(port_addr + PORT_CMD);
    cmd |= PORT_CMD_FRE;
    cmd |= PORT_CMD_ST;
    *(volatile uint32_t*)(port_addr + PORT_CMD) = cmd;
    
    active_port = port_num;
    return true;
}

// Find a port with a connected device
int ahci_find_device() {
    if (ahci_base_address == 0) {
        return -1;
    }
    
    // Get the ports implemented register
    uint32_t ports_implemented = *(volatile uint32_t*)(ahci_base_address + HBA_PI);
    
    // Scan each implemented port
    for (int i = 0; i < 32; i++) {
        if (ports_implemented & (1 << i)) {
            uint32_t port_addr = ahci_base_address + 0x100 + (i * AHCI_PORT_SIZE);
            
            // Check if a device is present
            uint32_t ssts = *(volatile uint32_t*)(port_addr + PORT_SSTS);
            uint8_t ipm = (ssts >> 8) & 0x0F;
            uint8_t det = ssts & 0x0F;
            
            if (det == HBA_PORT_DET_PRESENT && ipm == HBA_PORT_IPM_ACTIVE) {
                return i;
            }
        }
    }
    
    return -1;
}

// Wait for a port to be ready to accept commands
bool ahci_port_wait_ready(int port_num) {
    if (ahci_base_address == 0 || port_num < 0) {
        return false;
    }
    
    uint32_t port_addr = ahci_base_address + 0x100 + (port_num * AHCI_PORT_SIZE);
    
    // Wait for port to not be busy
    uint32_t timeout = 1000000;
    while (--timeout) {
        uint32_t tfd = *(volatile uint32_t*)(port_addr + PORT_TFD);
        if (!(tfd & 0x80)) {  // BSY bit
            return true;
        }
    }
    
    return false;
}

// Issue a command to the port
bool ahci_issue_command(int port_num, int slot) {
    if (ahci_base_address == 0 || port_num < 0) {
        return false;
    }
    
    uint32_t port_addr = ahci_base_address + 0x100 + (port_num * AHCI_PORT_SIZE);
    
    // Wait for port to be ready
    if (!ahci_port_wait_ready(port_num)) {
        return false;
    }
    
    // Set command issue bit
    *(volatile uint32_t*)(port_addr + PORT_CI) = 1 << slot;
    
    // Wait for command to complete
    uint32_t timeout = 1000000;
    while (--timeout) {
        if ((*(volatile uint32_t*)(port_addr + PORT_CI) & (1 << slot)) == 0) {
            // Check if there was an error
            uint32_t tfd = *(volatile uint32_t*)(port_addr + PORT_TFD);
            if (tfd & 0x01) {  // ERR bit
                printf("AHCI command error: status 0x%X", (tfd >> 8) & 0xFF);
                printf("\n");
                return false;
            }
            return true;
        }
    }
    
    printf("AHCI command timeout");
    printf("\n");
    return false;
}

// Read sectors from disk using AHCI
bool ahci_read_sectors(uint32_t lba, uint32_t count, void* buffer) {
    if (ahci_base_address == 0 || active_port < 0 || count == 0 || buffer == NULL) {
        return false;
    }
    
    // Set up command header
    cmd_header[0].cfl = sizeof(fis_reg_h2d_t) / 4;  // Command FIS size in dwords
    cmd_header[0].write = 0;  // This is a read
    cmd_header[0].prdtl = 1;  // One PRDT entry
    
    // Set up the command table
    memset(cmd_table, 0, sizeof(ahci_cmd_table_t));
    
    // Set up the command FIS (H2D Register FIS)
    fis_reg_h2d_t* cmd_fis = (fis_reg_h2d_t*)cmd_table->command_fis;
    create_command_fis(cmd_fis, ATA_CMD_READ_DMA_EX, lba, count);
    
    // Set up PRDT entry
    hba_prdt_entry_t* prdt = (hba_prdt_entry_t*)cmd_table->prdt_entry;
    prdt->dba = (uint32_t)buffer;
    prdt->dbau = 0;  // Upper 32 bits are 0 in a 32-bit system
    prdt->dbc = (count * 512) - 1;  // 512 bytes per sector, -1 per spec
    prdt->i = 1;  // Interrupt on completion
    
    // Issue the command
    return ahci_issue_command(active_port, 0);
}

// Write sectors to disk using AHCI
bool ahci_write_sectors(uint32_t lba, uint32_t count, const void* buffer) {
    if (ahci_base_address == 0 || active_port < 0 || count == 0 || buffer == NULL) {
        return false;
    }
    
    // Set up command header
    cmd_header[0].cfl = sizeof(fis_reg_h2d_t) / 4;  // Command FIS size in dwords
    cmd_header[0].write = 1;  // This is a write
    cmd_header[0].prdtl = 1;  // One PRDT entry
    
    // Set up the command table
    memset(cmd_table, 0, sizeof(ahci_cmd_table_t));
    
    // Set up the command FIS (H2D Register FIS)
    fis_reg_h2d_t* cmd_fis = (fis_reg_h2d_t*)cmd_table->command_fis;
    create_command_fis(cmd_fis, ATA_CMD_WRITE_DMA_EX, lba, count);
    
    // Set up PRDT entry
    hba_prdt_entry_t* prdt = (hba_prdt_entry_t*)cmd_table->prdt_entry;
    prdt->dba = (uint32_t)buffer;
    prdt->dbau = 0;  // Upper 32 bits are 0 in a 32-bit system
    prdt->dbc = (count * 512) - 1;  // 512 bytes per sector, -1 per spec
    prdt->i = 1;  // Interrupt on completion
    
    // Issue the command
    return ahci_issue_command(active_port, 0);
}

// Initialize the AHCI driver
bool ahci_init(uint32_t base_addr) {
    // Save the AHCI base address
    ahci_base_address = base_addr;
    
    if (ahci_base_address == 0) {
        printf("Invalid AHCI base address");
        printf("\n");
        return false;
    }
    
    // Reset the HBA
    uint32_t ghc = *(volatile uint32_t*)(ahci_base_address + HBA_GHC);
    ghc |= (1 << 0);  // HBA reset
    *(volatile uint32_t*)(ahci_base_address + HBA_GHC) = ghc;
    
    // Wait for reset to complete
    uint32_t timeout = 1000000;
    while (--timeout) {
        ghc = *(volatile uint32_t*)(ahci_base_address + HBA_GHC);
        if ((ghc & (1 << 0)) == 0) {
            break;
        }
    }
    
    if (timeout == 0) {
        printf("AHCI HBA reset timeout");
        printf("\n");
        return false;
    }
    
    // Enable AHCI mode
    ghc = *(volatile uint32_t*)(ahci_base_address + HBA_GHC);
    ghc |= (1 << 31);  // AHCI enable
    *(volatile uint32_t*)(ahci_base_address + HBA_GHC) = ghc;
    
    // Find a port with a connected device
    int port = ahci_find_device();
    if (port < 0) {
        printf("No AHCI devices found");
        printf("\n");
        return false;
    }
    
    printf("Found AHCI device on port %d", port);
    printf("\n");
    
    // Initialize the port
    if (!ahci_initialize_port(port)) {
        printf("Failed to initialize AHCI port %d", port);
        printf("\n");
        return false;
    }
    
    printf("AHCI port %d initialized", port);
    printf("\n");
    return true;
}

// Identify an ATA device
bool ahci_identify_device(uint16_t* identify_data) {
    if (ahci_base_address == 0 || active_port < 0 || identify_data == NULL) {
        return false;
    }
    
    // Set up command header
    cmd_header[0].cfl = sizeof(fis_reg_h2d_t) / 4;  // Command FIS size in dwords
    cmd_header[0].write = 0;  // This is a read
    cmd_header[0].prdtl = 1;  // One PRDT entry
    
    // Set up the command table
    memset(cmd_table, 0, sizeof(ahci_cmd_table_t));
    
    // Set up the command FIS (H2D Register FIS)
    fis_reg_h2d_t* cmd_fis = (fis_reg_h2d_t*)cmd_table->command_fis;
    memset(cmd_fis, 0, sizeof(fis_reg_h2d_t));
    
    cmd_fis->fis_type = FIS_TYPE_REG_H2D;
    cmd_fis->c = 1;  // Command
    cmd_fis->command = ATA_CMD_IDENTIFY;
    
    // Set up PRDT entry
    hba_prdt_entry_t* prdt = (hba_prdt_entry_t*)cmd_table->prdt_entry;
    prdt->dba = (uint32_t)identify_data;
    prdt->dbau = 0;  // Upper 32 bits are 0 in a 32-bit system
    prdt->dbc = 512 - 1;  // 512 bytes for identify data, -1 per spec
    prdt->i = 1;  // Interrupt on completion
    
    // Issue the command
    return ahci_issue_command(active_port, 0);
}

// Get device information
void ahci_print_device_info() {
    if (ahci_base_address == 0 || active_port < 0) {
        printf("AHCI not initialized");
        printf("\n");
        return;
    }
    
    // Buffer for IDENTIFY data
    uint16_t identify_data[256];
    
    if (!ahci_identify_device(identify_data)) {
        printf("Failed to identify AHCI device");
        printf("\n");
        return;
    }
    
    // Extract model name (words 27-46)
    char model[41];
    for (int i = 0; i < 20; i++) {
        // ATA strings are byte-swapped
        model[i*2] = (identify_data[27+i] >> 8) & 0xFF;
        model[i*2+1] = identify_data[27+i] & 0xFF;
    }
    model[40] = '\0';
    
    // Trim trailing spaces
    int len = 40;
    while (len > 0 && model[len-1] == ' ') {
        model[--len] = '\0';
    }
    
    // Extract serial number (words 10-19)
    char serial[21];
    for (int i = 0; i < 10; i++) {
        serial[i*2] = (identify_data[10+i] >> 8) & 0xFF;
        serial[i*2+1] = identify_data[10+i] & 0xFF;
    }
    serial[20] = '\0';
    
    // Trim trailing spaces
    len = 20;
    while (len > 0 && serial[len-1] == ' ') {
        serial[--len] = '\0';
    }
    
    // Get LBA capacity (words 100-103 for 48-bit LBA, words 60-61 for 28-bit LBA)
    uint64_t capacity;
    if (identify_data[83] & (1 << 10)) {
        // 48-bit LBA supported
        capacity = 
            ((uint64_t)identify_data[100]) |
            ((uint64_t)identify_data[101] << 16) |
            ((uint64_t)identify_data[102] << 32) |
            ((uint64_t)identify_data[103] << 48);
    } else {
        // 28-bit LBA
        capacity = ((uint32_t)identify_data[61] << 16) | identify_data[60];
    }
    
    // Display information
    printf("AHCI Device Information:");
    printf("\n");
    printf("  Model: %s", model);
    printf("\n");
    printf("  Serial: %s", serial);
    printf("\n");
    printf("  Capacity: %d sectors (%d MB)", 
           (uint32_t)capacity, (uint32_t)(capacity / 2048));
    printf("\n");
    
    // Check if device supports SMART
    if (identify_data[82] & (1 << 0)) {
        printf("  SMART: Supported");
        printf("\n");

    }
    
    // Check if device is an SSD
    if (identify_data[217] == 1) {
        printf("  Type: Solid State Device (SSD)");
        printf("\n");

    } else {
        printf("  Type: Hard Disk Drive (HDD)");
    printf("\n");

    }
}

// Test reading and writing
bool ahci_test_rw() {
    if (ahci_base_address == 0 || active_port < 0) {
        printf("AHCI not initialized");
        printf("\n");

        return false;
    }
    
    // Allocate buffers
    uint8_t* write_buffer = (uint8_t*)ahci_allocate_aligned(512, 512);
    uint8_t* read_buffer = (uint8_t*)ahci_allocate_aligned(512, 512);
    
    if (!write_buffer || !read_buffer) {
        printf("Failed to allocate buffers");
        printf("\n");

        return false;
    }
    
    // Initialize write buffer with a pattern
    for (int i = 0; i < 512; i++) {
        write_buffer[i] = (uint8_t)i;
    }
    
    // We'll use LBA 100000 for testing (well beyond MBR and partition table)
    uint32_t test_lba = 100000;
    
    printf("Testing AHCI read/write at LBA %d", test_lba);
    printf("\n");

    
    // Read the sector first to save its content
    uint8_t* backup_buffer = (uint8_t*)ahci_allocate_aligned(512, 512);
    if (!backup_buffer) {
        printf("Failed to allocate backup buffer");
        printf("\n");

        return false;
    }
    
    if (!ahci_read_sectors(test_lba, 1, backup_buffer)) {
        printf("Failed to read original sector for backup");
        printf("\n");

        return false;
    }
    
    // Write test data
    if (!ahci_write_sectors(test_lba, 1, write_buffer)) {
        printf("Write test failed");
        printf("\n");

        return false;
    }
    
    // Read back the data
    if (!ahci_read_sectors(test_lba, 1, read_buffer)) {
        printf("Read test failed");
        printf("\n");

        return false;
    }
    
    // Verify the data
    bool success = true;
    for (int i = 0; i < 512; i++) {
        if (read_buffer[i] != write_buffer[i]) {
            printf("Data verification failed at byte %d: expected %d, got %d",
                   i, write_buffer[i], read_buffer[i]);
                   printf("\n");
            success = false;
            break;
        }
    }
    
    // Restore the original sector content
    if (!ahci_write_sectors(test_lba, 1, backup_buffer)) {
        printf("Failed to restore original sector");
        printf("\n");

        return false;
    }
    
    if (success) {
        printf("AHCI read/write test passed");
        printf("\n");

    }
    
    return success;
}

// Replace the existing ahci_disk_init function with this enhanced version
bool ahci_disk_init() {
    extern uint32_t find_ahci_controller();
    
    printf("\n==== Starting AHCI Disk Initialization ====\n");
    
    // Get the AHCI controller base address
    uint32_t ahci_base = find_ahci_controller();
    if (ahci_base == 0) {
        printf("ERROR: No AHCI controller found");
        printf("\n");

        return false;
    }
    
    printf("Found AHCI controller at 0x%X", ahci_base);
    printf("\n");

    
    // Save the AHCI base address
    ahci_base_address = ahci_base;
    
    // Dump controller status before initialization
    ahci_dump_status();
    
    // HBA Reset and setup
    if (ahci_base_address == 0) {
        printf("ERROR: Invalid AHCI base address");
        printf("\n");

        return false;
    }
    
    // Read and display HBA capabilities
    uint32_t cap = *(volatile uint32_t*)(ahci_base_address + HBA_CAP);
    printf("HBA Capabilities: 0x%X", cap);
    printf("\n");

    printf("Supports %d ports, %s 64-bit addressing", 
           (cap & 0x1F) + 1, 
           (cap & (1 << 31)) ? "supports" : "does not support");
           printf("\n");
    
    // Reset the HBA
    printf("Attempting HBA reset...");
    printf("\n");

    uint32_t ghc = *(volatile uint32_t*)(ahci_base_address + HBA_GHC);
    ghc |= (1 << 0);  // HBA reset
    *(volatile uint32_t*)(ahci_base_address + HBA_GHC) = ghc;
    
    // Wait for reset to complete
    uint32_t timeout = 1000000;
    while (--timeout) {
        ghc = *(volatile uint32_t*)(ahci_base_address + HBA_GHC);
        if ((ghc & (1 << 0)) == 0) {
            break;
        }
    }
    
    if (timeout == 0) {
        printf("ERROR: AHCI HBA reset timeout");
        printf("\n");

        return false;
    }
    
    printf("HBA reset complete, timeout remaining: %d", timeout);
    printf("\n");

    
    // Enable AHCI mode
    ghc = *(volatile uint32_t*)(ahci_base_address + HBA_GHC);
    ghc |= (1 << 31);  // AHCI enable
    *(volatile uint32_t*)(ahci_base_address + HBA_GHC) = ghc;
    
    // Verify AHCI mode is enabled
    ghc = *(volatile uint32_t*)(ahci_base_address + HBA_GHC);
    if (!(ghc & (1 << 31))) {
        printf("ERROR: Failed to enable AHCI mode");
        printf("\n");

        return false;
    }
    
    printf("AHCI mode enabled successfully");
    printf("\n");

    
    // Display ports implemented register
    uint32_t pi = *(volatile uint32_t*)(ahci_base_address + HBA_PI);
    printf("Ports implemented: 0x%X", pi);
    printf("\n");

    
    // Check each implemented port
    printf("\n==== Checking All Implemented Ports ====");
    printf("\n");

    for (int i = 0; i < 32; i++) {
        if (pi & (1 << i)) {
            uint32_t port_addr = ahci_base_address + 0x100 + (i * AHCI_PORT_SIZE);
            uint32_t ssts = *(volatile uint32_t*)(port_addr + PORT_SSTS);
            uint8_t ipm = (ssts >> 8) & 0x0F;
            uint8_t det = ssts & 0x0F;
            
            printf("Port %d - SSTS: 0x%X (DET=%d, IPM=%d)", i, ssts, det, ipm);
            
            // Check the signature to determine device type
            uint32_t sig = *(volatile uint32_t*)(port_addr + PORT_SIG);
            printf(", Signature: 0x%X", sig);
            
            // Common signatures
            if (sig == 0x00000101) {
                printf(" (SATA device)");
            } else if (sig == 0xEB140101) {
                printf(" (ATAPI device)");
            } else if (sig == 0xC33C0101) {
                printf(" (SEMB device)");
            } else if (sig == 0x96690101) {
                printf(" (PM device)");
            } else {
                printf(" (Unknown device type)");
            }
            
            // Device presence and status
            if (det == HBA_PORT_DET_PRESENT && ipm == HBA_PORT_IPM_ACTIVE) {
                printf(" - ACTIVE DEVICE");
            }
            printf("\n");
        }
    }
    
    // Find a port with a connected device
    printf("\n==== Finding Connected Device ====");
    printf("\n");

    int port = ahci_find_device();
    if (port < 0) {
        printf("ERROR: No AHCI devices found");
        printf("\n");

        return false;
    }
    
    printf("Found AHCI device on port %d", port);
    printf("\n");

    
    // Initialize memory regions
    printf("\n==== Allocating Data Structures ====\n");
    cmd_header = (ahci_cmd_header_t*)ahci_allocate_aligned(1024, 1024);
    if (!cmd_header) {
        printf("ERROR: Failed to allocate command list memory");
        printf("\n");

        return false;
    }
    printf("Command list allocated at 0x%X", (uint32_t)cmd_header);
    printf("\n");

    
    received_fis = (uint8_t*)ahci_allocate_aligned(256, 256);
    if (!received_fis) {
        printf("ERROR: Failed to allocate FIS buffer memory");
        printf("\n");

        return false;
    }
    printf("FIS buffer allocated at 0x%X", (uint32_t)received_fis);
    printf("\n");

    
    cmd_table = (ahci_cmd_table_t*)ahci_allocate_aligned(sizeof(ahci_cmd_table_t), 128);
    if (!cmd_table) {
        printf("ERROR: Failed to allocate command table memory");
        printf("\n");

        return false;
    }
    printf("Command table allocated at 0x%X", (uint32_t)cmd_table);
    printf("\n");

    
    // Clear the command list and FIS buffer
    memset(cmd_header, 0, 1024);
    memset(received_fis, 0, 256);
    memset(cmd_table, 0, sizeof(ahci_cmd_table_t));
    
    // Initialize the port
    printf("\n");

    printf("==== Initializing Port %d ====", port);
    printf("\n");

    uint32_t port_addr = ahci_base_address + 0x100 + (port * AHCI_PORT_SIZE);
    
    // Stop command processing
    printf("Stopping command processing...");
    printf("\n");

    uint32_t cmd = *(volatile uint32_t*)(port_addr + PORT_CMD);
    cmd &= ~PORT_CMD_ST;
    cmd &= ~PORT_CMD_FRE;
    *(volatile uint32_t*)(port_addr + PORT_CMD) = cmd;
    
    // Wait for command processing to stop
    timeout = 1000000;
    printf("Waiting for command engine to stop...");
    printf("\n");

    while (timeout > 0) {
        uint32_t status = *(volatile uint32_t*)(port_addr + PORT_CMD);
        if ((status & PORT_CMD_FR) == 0 && (status & PORT_CMD_CR) == 0) {
            break;
        }
        timeout--;
        
        // Print status every 200,000 iterations
        if (timeout % 200000 == 0) {
            printf("Waiting for stop - CMD: 0x%X (CR=%d, FR=%d)", 
                  status, (status & PORT_CMD_CR) ? 1 : 0, (status & PORT_CMD_FR) ? 1 : 0);
        printf("\n");

        }
    }
    
    if (timeout == 0) {
        printf("ERROR: Timeout waiting for command processing to stop");
        printf("\n");

        return false;
    }
    
    printf("Command processing stopped, timeout remaining: %d", timeout);
    printf("\n");

    
    // Set up command header's command table base address
    cmd_header[0].ctba = (uint32_t)cmd_table;
    cmd_header[0].ctbau = 0;     // Upper 32 bits are 0 in a 32-bit system
    
    // Set up port registers
    printf("Setting up port memory regions...");
    printf("\n");

    *(volatile uint32_t*)(port_addr + PORT_CLB) = (uint32_t)cmd_header;
    *(volatile uint32_t*)(port_addr + PORT_CLBU) = 0;
    *(volatile uint32_t*)(port_addr + PORT_FB) = (uint32_t)received_fis;
    *(volatile uint32_t*)(port_addr + PORT_FBU) = 0;
    
    // Verify memory addresses were set
    uint32_t clb = *(volatile uint32_t*)(port_addr + PORT_CLB);
    uint32_t fb = *(volatile uint32_t*)(port_addr + PORT_FB);
    printf("Command List Base: 0x%X (expected 0x%X)", clb, (uint32_t)cmd_header);
    
    printf("\n");

    printf("FIS Base: 0x%X (expected 0x%X)", fb, (uint32_t)received_fis);
    printf("\n");

    
    if (clb != (uint32_t)cmd_header || fb != (uint32_t)received_fis) {
        printf("ERROR: Memory addresses do not match expected values");
        printf("\n");

        return false;
    }
    
    // Clear pending interrupts
    printf("Clearing port interrupts...");
    printf("\n");

    *(volatile uint32_t*)(port_addr + PORT_IS) = (uint32_t)-1;
    
    // Enable FIS receive
    printf("Enabling FIS receive...");
    printf("\n");

    cmd = *(volatile uint32_t*)(port_addr + PORT_CMD);
    cmd |= PORT_CMD_FRE;
    *(volatile uint32_t*)(port_addr + PORT_CMD) = cmd;
    
    // Check if FIS receive was enabled
    cmd = *(volatile uint32_t*)(port_addr + PORT_CMD);
    printf("After FRE enable - PORT_CMD: 0x%X (FRE=%d)", cmd, (cmd & PORT_CMD_FRE) ? 1 : 0);
    printf("\n");

    
    if (!(cmd & PORT_CMD_FRE)) {
        printf("ERROR: Failed to enable FIS receive");
        printf("\n");

        return false;
    }
    
    // Start command processing
    printf("Starting command processing...");
    printf("\n");

    cmd |= PORT_CMD_ST;
    *(volatile uint32_t*)(port_addr + PORT_CMD) = cmd;
    
    // Check if command processing was started
    cmd = *(volatile uint32_t*)(port_addr + PORT_CMD);
    printf("After ST enable - PORT_CMD: 0x%X (ST=%d)", cmd, (cmd & PORT_CMD_ST) ? 1 : 0);
    printf("\n");

    
    if (!(cmd & PORT_CMD_ST)) {
        printf("ERROR: Failed to start command processing");
        printf("\n");

        return false;
    }
    
    active_port = port;
    printf("Active port set to %d", active_port);
    printf("\n");

    
    // Dump controller status after initialization
    ahci_dump_status();
    
    // Print device information
    printf("\n==== Device Information ====");
    printf("\n");

    ahci_print_device_info();
    
    // Run a read/write test
    printf("\n==== Testing Read/Write Operations ====");
    printf("\n");

    if (ahci_test_rw()) {
        printf("SUCCESS: AHCI disk driver initialized successfully");
        printf("\n");

        return true;
    } else {
        printf("ERROR: AHCI disk driver initialization failed at read/write test");
        printf("\n");

        return false;
    }
}
// Enhanced AHCI initialization with detailed debugging
bool ahci_init_debug(uint32_t base_addr) {
    // Save the AHCI base address
    ahci_base_address = base_addr;
    
    if (ahci_base_address == 0) {
        printf("ERROR: Invalid AHCI base address");
        printf("\n");

        return false;
    }
    
    printf("DEBUG: AHCI base address: 0x%X", ahci_base_address);
    printf("\n");

    
    // Read and display HBA capabilities
    uint32_t cap = *(volatile uint32_t*)(ahci_base_address + HBA_CAP);
    printf("DEBUG: HBA Capabilities: 0x%X", cap);
    printf("\n");

    printf("DEBUG: Supports %d ports, %s 64-bit addressing", 
           (cap & 0x1F) + 1, 
           (cap & (1 << 31)) ? "supports" : "does not support");
    printf("\n");
    
    // Read version information
    uint32_t vs = *(volatile uint32_t*)(ahci_base_address + HBA_VS);
    printf("DEBUG: AHCI Version: %d.%d", (vs >> 16) & 0xFFFF, vs & 0xFFFF);
    printf("\n");

    
    // Read Global Host Control register
    uint32_t ghc = *(volatile uint32_t*)(ahci_base_address + HBA_GHC);
    printf("DEBUG: Initial GHC: 0x%X\n", ghc);
    
    // Reset the HBA
    printf("DEBUG: Attempting HBA reset...");
    printf("\n");

    ghc |= (1 << 0);  // HBA reset
    *(volatile uint32_t*)(ahci_base_address + HBA_GHC) = ghc;
    
    // Wait for reset to complete
    uint32_t timeout = 1000000;
    while (--timeout) {
        ghc = *(volatile uint32_t*)(ahci_base_address + HBA_GHC);
        if ((ghc & (1 << 0)) == 0) {
            break;
        }
    }
    
    if (timeout == 0) {
        printf("ERROR: AHCI HBA reset timeout");
        printf("\n");

        return false;
    }
    
    printf("DEBUG: HBA reset complete, timeout remaining: %d", timeout);
    printf("\n");

    
    // Enable AHCI mode
    ghc = *(volatile uint32_t*)(ahci_base_address + HBA_GHC);
    printf("DEBUG: GHC before AHCI enable: 0x%X", ghc);
    printf("\n");

    ghc |= (1 << 31);  // AHCI enable
    *(volatile uint32_t*)(ahci_base_address + HBA_GHC) = ghc;
    
    // Verify AHCI mode is enabled
    ghc = *(volatile uint32_t*)(ahci_base_address + HBA_GHC);
    printf("DEBUG: GHC after AHCI enable: 0x%X", ghc);
    printf("\n");

    if (!(ghc & (1 << 31))) {
        printf("ERROR: Failed to enable AHCI mode");
        printf("\n");

        return false;
    }
    
    printf("DEBUG: AHCI mode enabled successfully");
    printf("\n");

    
    // Display ports implemented register
    uint32_t pi = *(volatile uint32_t*)(ahci_base_address + HBA_PI);
    printf("DEBUG: Ports implemented: 0x%X", pi);
    printf("\n");

    
    // Count implemented ports
    int port_count = 0;
    for (int i = 0; i < 32; i++) {
        if (pi & (1 << i)) {
            port_count++;
        }
    }
    printf("DEBUG: Number of implemented ports: %d", port_count);
    printf("\n");

    
    if (port_count == 0) {
        printf("ERROR: No ports implemented");
        printf("\n");

        return false;
    }
    
    // Find a port with a connected device
    printf("DEBUG: Scanning for connected devices...");
    printf("\n");

    int port = ahci_find_device();
    if (port < 0) {
        printf("ERROR: No AHCI devices found");
        printf("\n");

        
        // Debug: Let's check each port's status
        for (int i = 0; i < 32; i++) {
            if (pi & (1 << i)) {
                uint32_t port_addr = ahci_base_address + 0x100 + (i * AHCI_PORT_SIZE);
                uint32_t ssts = *(volatile uint32_t*)(port_addr + PORT_SSTS);
                uint8_t ipm = (ssts >> 8) & 0x0F;
                uint8_t det = ssts & 0x0F;
                
                printf("DEBUG: Port %d - SSTS: 0x%X (DET=%d, IPM=%d)", i, ssts, det, ipm);
                printf("\n");

                
                // Check the signature to determine device type
                uint32_t sig = *(volatile uint32_t*)(port_addr + PORT_SIG);
                printf("DEBUG: Port %d - Signature: 0x%X", i, sig);
                
                // Common signatures
                if (sig == 0x00000101) {
                    printf(" (SATA device)");
                    printf("\n");

                } else if (sig == 0xEB140101) {
                    printf(" (ATAPI device)");
                    printf("\n");

                } else if (sig == 0xC33C0101) {
                    printf(" (SEMB device)");
                    printf("\n");

                } else if (sig == 0x96690101) {
                    printf(" (PM device)");
                    printf("\n");

                } else {
                    printf(" (Unknown device type)");
                    printf("\n");

                }
            }
        }
        return false;
    }
    
    printf("DEBUG: Found AHCI device on port %d", port);
    printf("\n");

    
    // Read port status registers for the found device
    uint32_t port_addr = ahci_base_address + 0x100 + (port * AHCI_PORT_SIZE);
    uint32_t ssts = *(volatile uint32_t*)(port_addr + PORT_SSTS);
    uint32_t cmd = *(volatile uint32_t*)(port_addr + PORT_CMD);
    uint32_t tfd = *(volatile uint32_t*)(port_addr + PORT_TFD);
    
    printf("DEBUG: Port %d - SSTS: 0x%X, CMD: 0x%X, TFD: 0x%X\n", port, ssts, cmd, tfd);
    
    // Initialize the port
    printf("DEBUG: Initializing port %d...", port);
    printf("\n");

    
    // Check memory allocation for structures
    printf("DEBUG: Allocating command list...");
    printf("\n");

    cmd_header = (ahci_cmd_header_t*)ahci_allocate_aligned(1024, 1024);
    if (!cmd_header) {
        printf("ERROR: Failed to allocate command list memory");
        printf("\n");

        return false;
    }
    
    printf("DEBUG: Allocating received FIS buffer...");
    printf("\n");

    received_fis = (uint8_t*)ahci_allocate_aligned(256, 256);
    if (!received_fis) {
        printf("ERROR: Failed to allocate FIS buffer memory");
        printf("\n");

        return false;
    }
    
    printf("DEBUG: Allocating command table...");
    printf("\n");

    cmd_table = (ahci_cmd_table_t*)ahci_allocate_aligned(sizeof(ahci_cmd_table_t), 128);
    if (!cmd_table) {
        printf("ERROR: Failed to allocate command table memory");
        printf("\n");

        return false;
    }
    
    printf("DEBUG: Memory allocation successful");
    printf("\n");

    
    if (!ahci_initialize_port(port)) {
        printf("ERROR: Failed to initialize AHCI port %d", port);
        printf("\n");

        
        // Check port status after failed initialization
        cmd = *(volatile uint32_t*)(port_addr + PORT_CMD);
        tfd = *(volatile uint32_t*)(port_addr + PORT_TFD);
        uint32_t is = *(volatile uint32_t*)(port_addr + PORT_IS);
        
        printf("DEBUG: After init attempt - CMD: 0x%X, TFD: 0x%X, IS: 0x%X", cmd, tfd, is);
        printf("\n");

        return false;
    }
    
    printf("DEBUG: AHCI port %d initialized successfully", port);
    printf("\n");

    return true;
}

// Enhanced port initialization with detailed debugging
bool ahci_initialize_port_debug(int port_num) {
    if (ahci_base_address == 0 || port_num < 0) {
        printf("ERROR: Invalid base address or port number");
        printf("\n");

        return false;
    }
    
    uint32_t port_addr = ahci_base_address + 0x100 + (port_num * AHCI_PORT_SIZE);
    printf("DEBUG: Port %d address: 0x%X", port_num, port_addr);
    printf("\n");

    
    // Read current command register value
    uint32_t cmd = *(volatile uint32_t*)(port_addr + PORT_CMD);
    printf("DEBUG: Initial PORT_CMD: 0x%X", cmd);
    printf("\n");

    
    // Stop command processing
    printf("DEBUG: Stopping command processing...");
    printf("\n");

    cmd &= ~PORT_CMD_ST;
    cmd &= ~PORT_CMD_FRE;
    *(volatile uint32_t*)(port_addr + PORT_CMD) = cmd;
    
    // Read command register after update
    cmd = *(volatile uint32_t*)(port_addr + PORT_CMD);
    printf("DEBUG: After stop commands - PORT_CMD: 0x%X", cmd);
    printf("\n");

    
    // Wait for command processing to stop
    uint32_t timeout = 1000000;
    while (timeout > 0) {
        uint32_t status = *(volatile uint32_t*)(port_addr + PORT_CMD);
        if ((status & PORT_CMD_FR) == 0 && (status & PORT_CMD_CR) == 0) {
            break;
        }
        timeout--;
        
        // Print status every 200,000 iterations
        if (timeout % 200000 == 0) {
            printf("DEBUG: Waiting for stop - CMD: 0x%X (CR=%d, FR=%d)\n", 
                   status, (status & PORT_CMD_CR) ? 1 : 0, (status & PORT_CMD_FR) ? 1 : 0);
        }
    }
    
    if (timeout == 0) {
        printf("ERROR: Timeout waiting for command processing to stop");
        printf("\n");

        return false;
    }
    
    printf("DEBUG: Command processing stopped, timeout remaining: %d", timeout);
    printf("\n");

    
    // Setup memory regions for port
    printf("DEBUG: Setting up port memory regions...");
    printf("\n");

    *(volatile uint32_t*)(port_addr + PORT_CLB) = (uint32_t)cmd_header;
    *(volatile uint32_t*)(port_addr + PORT_CLBU) = 0;
    *(volatile uint32_t*)(port_addr + PORT_FB) = (uint32_t)received_fis;
    *(volatile uint32_t*)(port_addr + PORT_FBU) = 0;
    
    // Verify memory addresses were set
    uint32_t clb = *(volatile uint32_t*)(port_addr + PORT_CLB);
    uint32_t fb = *(volatile uint32_t*)(port_addr + PORT_FB);
    printf("DEBUG: Command List Base: 0x%X (expected 0x%X)", clb, (uint32_t)cmd_header);
    printf("\n");

    printf("DEBUG: FIS Base: 0x%X (expected 0x%X)", fb, (uint32_t)received_fis);
    printf("\n");

    
    if (clb != (uint32_t)cmd_header || fb != (uint32_t)received_fis) {
        printf("ERROR: Memory addresses do not match expected values");
        printf("\n");

        return false;
    }
    
    // Clear any pending interrupts
    printf("DEBUG: Clearing port interrupts...");

    printf("\n");

    *(volatile uint32_t*)(port_addr + PORT_IS) = (uint32_t)-1;
    
    // Enable FIS receive and start command processing
    printf("DEBUG: Starting command processing...");
    printf("\n");
    cmd = *(volatile uint32_t*)(port_addr + PORT_CMD);
    cmd |= PORT_CMD_FRE;
    *(volatile uint32_t*)(port_addr + PORT_CMD) = cmd;
    
    // Verify FIS receive is enabled
    cmd = *(volatile uint32_t*)(port_addr + PORT_CMD);
    printf("DEBUG: After FRE enable - PORT_CMD: 0x%X (FRE=%d)", cmd, (cmd & PORT_CMD_FRE) ? 1 : 0);
    printf("\n");
    
    if (!(cmd & PORT_CMD_FRE)) {
        printf("ERROR: Failed to enable FIS receive");
        printf("\n");
        return false;
    }
    
    // Start command processing
    cmd |= PORT_CMD_ST;
    *(volatile uint32_t*)(port_addr + PORT_CMD) = cmd;
    
    // Verify command processing is started
    cmd = *(volatile uint32_t*)(port_addr + PORT_CMD);
    printf("DEBUG: After ST enable - PORT_CMD: 0x%X (ST=%d)", cmd, (cmd & PORT_CMD_ST) ? 1 : 0);
    printf("\n");
    
    if (!(cmd & PORT_CMD_ST)) {
        printf("ERROR: Failed to start command processing");
        printf("\n");
        return false;
    }
    
    active_port = port_num;
    printf("DEBUG: Active port set to %d", active_port);
    printf("\n");
    return true;
}

// Enhanced main initialization function
bool ahci_disk_init_debug() {
    extern uint32_t find_ahci_controller();
    
    printf("DEBUG: Starting AHCI disk initialization...");
    printf("\n");
    
    // Get the AHCI controller base address
    uint32_t ahci_base = find_ahci_controller();
    if (ahci_base == 0) {
        printf("ERROR: No AHCI controller found");
        printf("\n");
        return false;
    }
    
    printf("DEBUG: Found AHCI controller at 0x%X", ahci_base);
    printf("\n");
    
    // Initialize the AHCI driver
    bool init_result = ahci_init(ahci_base);
    
    
    if (!init_result) {
        printf("ERROR: Failed to initialize AHCI driver");
        printf("\n");
        return false;
    }
    
    printf("DEBUG: AHCI driver initialized successfully");
    printf("\n");
    
    // Print device information
    ahci_print_device_info();
    
    // Run a read/write test
    if (ahci_test_rw()) {
        printf("SUCCESS: AHCI disk driver initialized successfully");
        printf("\n");
        return true;
    } else {
        printf("ERROR: AHCI disk driver initialization failed at read/write test");
        printf("\n");
        return false;
    }
}

// Add this function to dump AHCI controller and port status
void ahci_dump_status() {
    if (ahci_base_address == 0) {
        printf("AHCI not initialized\n");
        return;
    }
    
    // Read global registers
    uint32_t cap = *(volatile uint32_t*)(ahci_base_address + HBA_CAP);
    uint32_t ghc = *(volatile uint32_t*)(ahci_base_address + HBA_GHC);
    uint32_t is = *(volatile uint32_t*)(ahci_base_address + HBA_IS);
    uint32_t pi = *(volatile uint32_t*)(ahci_base_address + HBA_PI);
    uint32_t vs = *(volatile uint32_t*)(ahci_base_address + HBA_VS);
    printf("\n");
    printf("==== AHCI Controller Status ====");
    printf("\n");
    printf("Base Address: 0x%08X", ahci_base_address);
    printf("\n");
    printf("Version: %d.%d", (vs >> 16) & 0xFFFF, vs & 0xFFFF);
    printf("\n");
    printf("Capabilities: 0x%08X", cap);
    printf("\n");
    printf("  Number of Ports: %d", (cap & 0x1F) + 1);
    printf("\n");
    printf("  64-bit Addressing: %s", (cap & (1 << 31)) ? "Yes" : "No");
    printf("\n");
    printf("  NCQ Support: %s", (cap & (1 << 30)) ? "Yes" : "No");
    printf("\n");
    printf("  SATA III Speed: %s", (cap & (1 << 28)) ? "Yes" : "No");
    printf("\n");
    printf("Global Control: 0x%08X", ghc);
    printf("\n");
    printf("  AHCI Enable: %s", (ghc & (1 << 31)) ? "Yes" : "No");
    printf("\n");
    printf("  Interrupt Enable: %s", (ghc & (1 << 1)) ? "Yes" : "No");
    printf("\n");
    printf("Interrupt Status: 0x%08X", is);
    printf("\n");
    printf("Ports Implemented: 0x%08X", pi);

    
    // Check each implemented port
    for (int i = 0; i < 32; i++) {
        if (pi & (1 << i)) {
            uint32_t port_addr = ahci_base_address + 0x100 + (i * AHCI_PORT_SIZE);
            uint32_t cmd = *(volatile uint32_t*)(port_addr + PORT_CMD);
            uint32_t tfd = *(volatile uint32_t*)(port_addr + PORT_TFD);
            uint32_t ssts = *(volatile uint32_t*)(port_addr + PORT_SSTS);
            uint32_t sctl = *(volatile uint32_t*)(port_addr + PORT_SCTL);
            uint32_t serr = *(volatile uint32_t*)(port_addr + PORT_SERR);
            uint32_t port_is = *(volatile uint32_t*)(port_addr + PORT_IS);
            uint32_t port_ie = *(volatile uint32_t*)(port_addr + PORT_IE);
            uint32_t sig = *(volatile uint32_t*)(port_addr + PORT_SIG);
            printf("\n");
            printf("-- Port %d Status --", i);
            printf("\n");
            printf("Command: 0x%08X", cmd);
            printf("\n");
            printf("  Start (ST): %s", (cmd & PORT_CMD_ST) ? "Yes" : "No");
            printf("\n");
            printf("  FIS Receive Enable (FRE): %s", (cmd & PORT_CMD_FRE) ? "Yes" : "No");
            printf("\n");
            printf("  Command List Running (CR): %s", (cmd & PORT_CMD_CR) ? "Yes" : "No");
            printf("\n");
            printf("  FIS Receive Running (FR): %s", (cmd & PORT_CMD_FR) ? "Yes" : "No");

            printf("\n");
            printf("Task File Data: 0x%08X", tfd);
            printf("\n");
            printf("  Busy (BSY): %s", (tfd & 0x80) ? "Yes" : "No");
            printf("\n");
            printf("  Data Request (DRQ): %s", (tfd & 0x08) ? "Yes" : "No");
            printf("\n");
            printf("  Error (ERR): %s", (tfd & 0x01) ? "Yes" : "No");
            
            uint8_t det = ssts & 0x0F;
            uint8_t ipm = (ssts >> 8) & 0x0F;
            printf("SATA Status: 0x%08X", ssts);
            printf("\n");
            printf("  Device Detection (DET): %d - %s", det, 
                   det == 0 ? "No Device Detected" : 
                   det == 1 ? "Device Detected but PHY Not Established" : 
                   det == 3 ? "Device Present with PHY Communication Established" : 
                   det == 4 ? "PHY Offline" : "Unknown");
            printf("\n");
            printf("  Interface Power Management (IPM): %d - %s\n", ipm,
                   ipm == 0 ? "Not Present/Not Active" :
                   ipm == 1 ? "Active" :
                   ipm == 2 ? "Partial Power Management" :
                   ipm == 6 ? "Slumber Power Management" : "Unknown");
            printf("\n");
            printf("SATA Control: 0x%08X", sctl);
            printf("\n");
            printf("SATA Error: 0x%08X", serr);
            printf("\n");
            printf("Port Interrupt Status: 0x%08X", port_is);
            printf("\n");
            printf("Port Interrupt Enable: 0x%08X", port_ie);
            
            printf("Signature: 0x%08X - ", sig);
            if (sig == 0x00000101) {
                printf("SATA drive");
                printf("\n");
            } else if (sig == 0xEB140101) {
                printf("ATAPI devicn");
                printf("\n");
            } else if (sig == 0xC33C0101) {
                printf("Enclosure management bridge");
                printf("\n");
            } else if (sig == 0x96690101) {
                printf("Port multiplier");
            } else {
                printf("Unknown device type");
            }
            
            // Show command list and FIS base addresses
            uint32_t clb = *(volatile uint32_t*)(port_addr + PORT_CLB);
            uint32_t clbu = *(volatile uint32_t*)(port_addr + PORT_CLBU);
            uint32_t fb = *(volatile uint32_t*)(port_addr + PORT_FB);
            uint32_t fbu = *(volatile uint32_t*)(port_addr + PORT_FBU);
            
            printf("Command List Base: 0x%08X%08X", clbu, clb);
            printf("\n");
            printf("FIS Base: 0x%08X%08X", fbu, fb);
            printf("\n");
        }
    }
    
    printf("\nActive port: %d", active_port);
    printf("\n");
    printf("==========================");
    printf("\n");
    printf("\n");
}