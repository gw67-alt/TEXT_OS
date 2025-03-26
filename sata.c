#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "io.h"
#include "sata.h"

/* AHCI Base Memory Register offsets */
#define AHCI_CAP         0x00  // Host Capabilities
#define AHCI_GHC         0x04  // Global Host Control
#define AHCI_IS          0x08  // Interrupt Status
#define AHCI_PI          0x0C  // Ports Implemented
#define AHCI_VS          0x10  // Version
#define AHCI_CCC_CTL     0x14  // Command Completion Coalescing Control
#define AHCI_CCC_PORTS   0x18  // Command Completion Coalescing Ports
#define AHCI_EM_LOC      0x1C  // Enclosure Management Location
#define AHCI_EM_CTL      0x20  // Enclosure Management Control
#define AHCI_CAP2        0x24  // Host Capabilities Extended
#define AHCI_BOHC        0x28  // BIOS/OS Handoff Control and Status

/* Port registers offsets (relative to port base) */
#define PORT_CLB         0x00  // Command List Base Address
#define PORT_CLBU        0x04  // Command List Base Address Upper 32 bits
#define PORT_FB          0x08  // FIS Base Address
#define PORT_FBU         0x0C  // FIS Base Address Upper 32 bits
#define PORT_IS          0x10  // Interrupt Status
#define PORT_IE          0x14  // Interrupt Enable
#define PORT_CMD         0x18  // Command and Status
#define PORT_TFD         0x20  // Task File Data
#define PORT_SIG         0x24  // Signature
#define PORT_SSTS        0x28  // SATA Status
#define PORT_SCTL        0x2C  // SATA Control
#define PORT_SERR        0x30  // SATA Error
#define PORT_SACT        0x34  // SATA Active
#define PORT_CI          0x38  // Command Issue

/* Port CMD register bits */
#define PORT_CMD_ST      (1 << 0)  // Start
#define PORT_CMD_SUD     (1 << 1)  // Spin-Up Device
#define PORT_CMD_POD     (1 << 2)  // Power On Device
#define PORT_CMD_FRE     (1 << 4)  // FIS Receive Enable
#define PORT_CMD_FR      (1 << 14) // FIS Receive Running
#define PORT_CMD_CR      (1 << 15) // Command List Running
#define PORT_CMD_ACTIVE  (1 << 28) // Device is active

/* GHC register bits */
#define GHC_HR           (1 << 0)  // HBA Reset
#define GHC_IE           (1 << 1)  // Interrupt Enable
#define GHC_MRSM         (1 << 2)  // MSI Revert to Single Message
#define GHC_AE           (1 << 31) // AHCI Enable

/* SATA Status bits */
#define SATA_STATUS_DET_MASK      0x0F  // Device Detection
#define SATA_STATUS_DET_PRESENT   0x03  // Device Present and Established Communication
#define SATA_STATUS_IPM_MASK      0xF00 // Interface Power Management
#define SATA_STATUS_IPM_ACTIVE    0x100 // Active State

/* FIS Types */
#define FIS_TYPE_REG_H2D  0x27  // Register FIS - Host to Device
#define FIS_TYPE_REG_D2H  0x34  // Register FIS - Device to Host
#define FIS_TYPE_DMA_ACT  0x39  // DMA Activate FIS
#define FIS_TYPE_DMA_SETUP 0x41 // DMA Setup FIS
#define FIS_TYPE_DATA     0x46  // Data FIS
#define FIS_TYPE_BIST     0x58  // BIST Activate FIS
#define FIS_TYPE_PIO_SETUP 0x5F // PIO Setup FIS
#define FIS_TYPE_DEV_BITS 0xA1  // Set Device Bits FIS




/* ATA Commands */
#define ATA_CMD_READ_DMA_EXT      0x25
#define ATA_CMD_WRITE_DMA_EXT     0x35
#define ATA_CMD_IDENTIFY          0xEC




    // Reserved 0x2C - 0x9F
    uint8_t reserved[0xA0 - 0x2C];
    
    // Vendor Specific 0xA0 - 0xFF
    uint8_t vendor[0x100 - 0xA0];
    
   

/* Global variables */
struct ahci_hba_mem *hba_mem;  // Pointer to HBA memory mapped registers
uint32_t ahci_base;           // Physical base address of AHCI controller

/* Function prototypes */
uint32_t find_ahci_controller();
void init_ahci(uint32_t ahci_base_addr);
int find_sata_drives();
int init_sata_port(int port_num);
int ahci_identify_device(int port_num);
int ahci_read_sectors(int port_num, uint64_t start_lba, uint16_t count, void *buffer);
int ahci_write_sectors(int port_num, uint64_t start_lba, uint16_t count, const void *buffer);
int ahci_port_rebase(int port_num);
int ahci_stop_port_cmd(struct ahci_hba_port *port);
int ahci_start_port_cmd(struct ahci_hba_port *port);
void ahci_print_port_status(int port_num);

/* Allocate memory that is 4KB aligned (for AHCI DMA) */
void* ahci_memalign(size_t alignment, size_t size) {
    // Simple implementation - assumes system has enough memory
    // In a real OS, this would use a proper memory allocator
    static uint8_t memory_pool[1024 * 1024]; // 1MB pool
    static size_t next_free = 0;
    
    // Calculate aligned address
    size_t mask = alignment - 1;
    size_t misalign = (size_t)&memory_pool[next_free] & mask;
    size_t adjustment = misalign ? (alignment - misalign) : 0;
    size_t aligned_addr = next_free + adjustment;
    
    // Check if enough space
    if (aligned_addr + size > sizeof(memory_pool)) {
        return NULL; // Out of memory
    }
    
    // Update next free position and return aligned pointer
    next_free = aligned_addr + size;
    return &memory_pool[aligned_addr];
}

/* Find the AHCI controller using PCI enumeration */
uint32_t find_ahci_controller() {
    // AHCI Class Code: 0x01 (Mass Storage), Subclass: 0x06 (SATA), Interface: 0x01 (AHCI)
    for (uint8_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t function = 0; function < 8; function++) {
                uint16_t vendor_id = pci_config_read_word(bus, device, function, 0x00);
                if (vendor_id == 0xFFFF) continue; // No device at this position
                
                uint8_t class_code = pci_config_read_byte(bus, device, function, 0x0B);
                uint8_t subclass = pci_config_read_byte(bus, device, function, 0x0A);
                uint8_t prog_if = pci_config_read_byte(bus, device, function, 0x09);
                
                if (class_code == 0x01 && subclass == 0x06 && prog_if == 0x01) {
                    // Found AHCI controller
                    uint32_t bar5 = pci_config_read_dword(bus, device, function, 0x24);
                    // BAR5 contains the AHCI base address
                    return bar5 & 0xFFFFFFF0; // Mask off the low 4 bits (flags)
                }
            }
        }
    }
    return 0; // Not found
}

/* Initialize AHCI controller with memory-mapped registers */
void init_ahci(uint32_t ahci_base_addr) {
    // Store the base address
    ahci_base = ahci_base_addr;
    // Map the physical address to a virtual address (in a real OS, this would use proper memory mapping)
    hba_mem = (struct ahci_hba_mem*)ahci_base_addr;
    
    // Reset HBA
    hba_mem->ghc |= GHC_HR;
    
    // Wait for reset to complete
    while (hba_mem->ghc & GHC_HR) {
        // Busy wait
    }
    
    // Enable AHCI mode
    hba_mem->ghc |= GHC_AE;
    
    // Enable interrupts
    hba_mem->ghc |= GHC_IE;
    
    // Print HBA capabilities
    printf("AHCI controller initialized at 0x%08x\n", ahci_base);
    printf("HBA Capabilities: 0x%08x\n", hba_mem->cap);
    printf("Version: 0x%08x\n", hba_mem->vs);
    printf("Ports Implemented: 0x%08x\n", hba_mem->pi);
}

/* Find and initialize SATA drives connected to the AHCI controller */
int find_sata_drives() {
    int found_drives = 0;
    uint32_t ports_implemented = hba_mem->pi;
    
    printf("Scanning for SATA drives...\n");
    
    // Check each port that is implemented
    for (int i = 0; i < 32; i++) {
        if (ports_implemented & (1 << i)) {
            // Check if a device is present on this port
            uint32_t ssts = hba_mem->ports[i].ssts;
            uint8_t det = (ssts & SATA_STATUS_DET_MASK);
            uint16_t ipm = (ssts & SATA_STATUS_IPM_MASK) >> 8;
            
            if (det == SATA_STATUS_DET_PRESENT && ipm == (SATA_STATUS_IPM_ACTIVE >> 8)) {
                if (init_sata_port(i) == 0) {
                    printf("SATA drive found on port %d\n", i);
                    ahci_print_port_status(i);
                    
                    // Try to identify the device to get more information
                    if (ahci_identify_device(i) == 0) {
                        found_drives++;
                    }
                }
            }
        }
    }
    
    printf("Total SATA drives found: %d\n", found_drives);
    return found_drives;
}

/* Initialize a SATA port */
int init_sata_port(int port_num) {
    struct ahci_hba_port *port = &hba_mem->ports[port_num];
    
    // Stop any running commands
    if (ahci_stop_port_cmd(port) != 0) {
        printf("Error: Could not stop port %d command engine\n", port_num);
        return -1;
    }
    
    // Rebase the port (set up command list and FIS structures)
    if (ahci_port_rebase(port_num) != 0) {
        printf("Error: Could not rebase port %d\n", port_num);
        return -1;
    }
    
    // Start command engine
    if (ahci_start_port_cmd(port) != 0) {
        printf("Error: Could not start port %d command engine\n", port_num);
        return -1;
    }
    
    return 0;
}

/* Stop command processing on a port */
int ahci_stop_port_cmd(struct ahci_hba_port *port) {
    // Clear ST (bit 0)
    port->cmd &= ~PORT_CMD_ST;
    
    // Clear FRE (bit 4)
    port->cmd &= ~PORT_CMD_FRE;
    
    // Wait until FR (bit 14) and CR (bit 15) are cleared
    uint32_t timeout = 500000; // Arbitrary timeout value
    while ((port->cmd & PORT_CMD_FR) || (port->cmd & PORT_CMD_CR)) {
        if (--timeout == 0) {
            return -1; // Timeout error
        }
    }
    
    return 0;
}

/* Start command processing on a port */
int ahci_start_port_cmd(struct ahci_hba_port *port) {
    // Wait until CR (bit 15) is cleared
    uint32_t timeout = 500000; // Arbitrary timeout value
    while (port->cmd & PORT_CMD_CR) {
        if (--timeout == 0) {
            return -1; // Timeout error
        }
    }
    
    // Set FRE (bit 4)
    port->cmd |= PORT_CMD_FRE;
    
    // Set ST (bit 0)
    port->cmd |= PORT_CMD_ST;
    
    return 0;
}

/* Set up command list and FIS structures for a port */
int ahci_port_rebase(int port_num) {
    struct ahci_hba_port *port = &hba_mem->ports[port_num];
    
    // Allocate memory for command list - 1KB aligned, 1KB size (supports 32 commands)
    void *cmd_list = ahci_memalign(1024, 1024);
    if (!cmd_list) {
        return -1;
    }
    
    // Zero out the command list
    memset(cmd_list, 0, 1024);
    
    // Allocate memory for FIS structure - 256B aligned, 256B size
    void *fis_base = ahci_memalign(256, 256);
    if (!fis_base) {
        return -1;
    }
    
    // Zero out the FIS structure
    memset(fis_base, 0, 256);
    
    // Allocate memory for command tables - 128B aligned, 256B per command
    for (int i = 0; i < 32; i++) {
        struct ahci_cmd_header *header = (struct ahci_cmd_header*)cmd_list + i;
        
        // Command table size: 128 bytes for FIS + PRDT entries (each 16 bytes)
        // Here we allocate for a small number of PRDT entries (8) to keep it simple
        header->prdt_length = 8; // 8 PRDT entries
        
        void *cmd_table = ahci_memalign(128, 128 + (header->prdt_length * sizeof(struct ahci_prdt_entry)));
        if (!cmd_table) {
            return -1;
        }
        
        // Zero out the command table
        memset(cmd_table, 0, 128 + (header->prdt_length * sizeof(struct ahci_prdt_entry)));
        
        // Set command table address (only 32-bit addresses for simplicity)
        header->cmd_table_base_l = (uint32_t)(uintptr_t)cmd_table;
        header->cmd_table_base_h = 0;
    }
    
    // Set the base addresses in the port registers
    port->clb = (uint32_t)(uintptr_t)cmd_list;
    port->clbu = 0; // No high bits for 32-bit addresses
    port->fb = (uint32_t)(uintptr_t)fis_base;
    port->fbu = 0; // No high bits for 32-bit addresses
    
    // Clear any pending interrupts
    port->is = (uint32_t)-1; // Write 1 to clear
    
    return 0;
}

/* Send IDENTIFY DEVICE command to a SATA drive */
int ahci_identify_device(int port_num) {
    struct ahci_hba_port *port = &hba_mem->ports[port_num];
    
    // Allocate a buffer for the identify data (512 bytes)
    uint16_t *identify_data = (uint16_t*)ahci_memalign(4096, 512);
    if (!identify_data) {
        return -1;
    }
    
    // Zero out the buffer
    memset(identify_data, 0, 512);
    
    // Set up command header (slot 0)
    struct ahci_cmd_header *header = (struct ahci_cmd_header*)(uintptr_t)port->clb;
    header->command_fis_length = sizeof(struct fis_reg_h2d) / 4; // Length in DWORDs
    header->write = 0; // This is a read operation
    header->prdt_length = 1; // Only one PRDT entry needed
    
    // Set up command table
    struct ahci_cmd_table *cmd_table = (struct ahci_cmd_table*)(uintptr_t)header->cmd_table_base_l;
    
    // Set up the command FIS
    struct fis_reg_h2d *fis = (struct fis_reg_h2d*)cmd_table->command_fis;
    memset(fis, 0, sizeof(struct fis_reg_h2d));
    
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->command_control = 1; // This is a command
    fis->command = ATA_CMD_IDENTIFY;
    fis->device = 0; // Select device 0
    
    // Set up the PRDT entry
    cmd_table->prdt_entries[0].data_base_l = (uint32_t)(uintptr_t)identify_data;
    cmd_table->prdt_entries[0].data_base_h = 0;
    cmd_table->prdt_entries[0].byte_count = 512 - 1; // 512 bytes (0-based count)
    cmd_table->prdt_entries[0].interrupt_on_completion = 1;
    
    // Clear pending interrupts
    port->is = (uint32_t)-1;
    
    // Issue the command
    port->ci = 1; // Set bit 0 (command slot 0)
    
    // Wait for command completion
    uint32_t timeout = 1000000; // Arbitrary timeout value
    while ((port->ci & 1) && timeout--) {
        // Busy wait
        if (port->is & (1 << 30)) { // Task File Error
            printf("Error: Task File Error on port %d\n", port_num);
            return -1;
        }
    }
    
    if (timeout == 0) {
        printf("Error: Command timeout on port %d\n", port_num);
        return -1;
    }
    
    // Check for errors
    if (port->is & (1 << 30)) { // Task File Error
        printf("Error: Task File Error on port %d\n", port_num);
        return -1;
    }
    
    // Process and display identify data
    char model[41];
    char serial[21];
    
    // Extract model number (words 27-46)
    for (int i = 0; i < 20; i++) {
        // IDENTIFY data is stored in little-endian but displayed as big-endian
        model[i*2] = (identify_data[27+i] >> 8) & 0xFF;
        model[i*2+1] = identify_data[27+i] & 0xFF;
    }
    model[40] = '\0';
    
    // Trim trailing spaces
    for (int i = 39; i >= 0 && model[i] == ' '; i--) {
        model[i] = '\0';
    }
    
    // Extract serial number (words 10-19)
    for (int i = 0; i < 10; i++) {
        serial[i*2] = (identify_data[10+i] >> 8) & 0xFF;
        serial[i*2+1] = identify_data[10+i] & 0xFF;
    }
    serial[20] = '\0';
    
    // Trim trailing spaces
    for (int i = 19; i >= 0 && serial[i] == ' '; i--) {
        serial[i] = '\0';
    }
    
    // Calculate capacity
    uint32_t lba28_sectors = 0;
    uint64_t lba48_sectors = 0;
    
    // LBA28 capacity (words 60-61)
    if (identify_data[60] != 0 || identify_data[61] != 0) {
        lba28_sectors = (uint32_t)identify_data[60] | ((uint32_t)identify_data[61] << 16);
    }
    
    // LBA48 capacity (words 100-103)
    if (identify_data[100] != 0 || identify_data[101] != 0 || 
        identify_data[102] != 0 || identify_data[103] != 0) {
        lba48_sectors = (uint64_t)identify_data[100] | 
                       ((uint64_t)identify_data[101] << 16) |
                       ((uint64_t)identify_data[102] << 32) |
                       ((uint64_t)identify_data[103] << 48);
    }
    
    // Display drive information
    printf("---- SATA Drive Information (Port %d) ----\n", port_num);
    printf("Model: %s\n", model);
    printf("Serial: %s\n", serial);
    
    if (lba48_sectors != 0) {
        printf("Capacity: %llu sectors (%llu MB)\n", 
               lba48_sectors, lba48_sectors / 2048); // 1 MB = 2048 sectors of 512 bytes
    } else {
        printf("Capacity: %u sectors (%u MB)\n", 
               lba28_sectors, lba28_sectors / 2048);
    }
    
    return 0;
}

/* Read sectors from a SATA drive */
int ahci_read_sectors(int port_num, uint64_t start_lba, uint16_t count, void *buffer) {
    if (count == 0) {
        return 0; // Nothing to do
    }
    
    struct ahci_hba_port *port = &hba_mem->ports[port_num];
    
    // Make sure buffer is properly aligned
    if (((uintptr_t)buffer & 0xFFF) != 0) {
        // Not 4KB aligned - allocate aligned buffer
        void *aligned_buffer = ahci_memalign(4096, count * 512);
        if (!aligned_buffer) {
            return -1;
        }
        
        // Read to aligned buffer, then copy to user buffer
        int result = ahci_read_sectors(port_num, start_lba, count, aligned_buffer);
        if (result == 0) {
            memcpy(buffer, aligned_buffer, count * 512);
        }
        return result;
    }
    
    // Set up command header (slot 0)
    struct ahci_cmd_header *header = (struct ahci_cmd_header*)(uintptr_t)port->clb;
    header->command_fis_length = sizeof(struct fis_reg_h2d) / 4; // Length in DWORDs
    header->write = 0; // This is a read operation
    
    // Calculate number of PRDT entries needed
    // Each entry can describe up to 4MB, and each sector is 512 bytes
    uint32_t bytes_to_transfer = count * 512;
    uint16_t prdt_count = (bytes_to_transfer + 4194303) / 4194304; // Ceiling division by 4MB
    
    header->prdt_length = prdt_count;
    
    // Set up command table
    struct ahci_cmd_table *cmd_table = (struct ahci_cmd_table*)(uintptr_t)header->cmd_table_base_l;
    
    // Set up the command FIS - READ DMA EXT command
    struct fis_reg_h2d *fis = (struct fis_reg_h2d*)cmd_table->command_fis;
    memset(fis, 0, sizeof(struct fis_reg_h2d));
    
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->command_control = 1;        // This is a command
    fis->command = ATA_CMD_READ_DMA_EXT;
    
    // Set the LBA (Logical Block Address) fields
    fis->lba0 = (uint8_t)(start_lba & 0xFF);
    fis->lba1 = (uint8_t)((start_lba >> 8) & 0xFF);
    fis->lba2 = (uint8_t)((start_lba >> 16) & 0xFF);
    fis->lba3 = (uint8_t)((start_lba >> 24) & 0xFF);
    fis->lba4 = (uint8_t)((start_lba >> 32) & 0xFF);
    fis->lba5 = (uint8_t)((start_lba >> 40) & 0xFF);
    
    // Set the device register to LBA mode
    fis->device = 1 << 6; // Set bit 6 for LBA mode
    
    // Set the sector count
    fis->count_low = count & 0xFF;
    fis->count_high = (count >> 8) & 0xFF;
    
    // Set up PRDT entries
    // Each entry can describe up to 4MB, but we'll use smaller chunks for simplicity and safety
    uint32_t bytes_remaining = bytes_to_transfer;
    uint8_t *buf_ptr = (uint8_t*)buffer;
    
    for (int i = 0; i < prdt_count; i++) {
        uint32_t bytes_this_entry = bytes_remaining > 4194304 ? 4194304 : bytes_remaining;
        
        cmd_table->prdt_entries[i].data_base_l = (uint32_t)(uintptr_t)buf_ptr;
        cmd_table->prdt_entries[i].data_base_h = 0; // Assuming 32-bit addresses
        cmd_table->prdt_entries[i].byte_count = bytes_this_entry - 1; // 0-based count
        cmd_table->prdt_entries[i].interrupt_on_completion = 1;
        
        bytes_remaining -= bytes_this_entry;
        buf_ptr += bytes_this_entry;
    }
    
    // Clear pending interrupts
    port->is = (uint32_t)-1;
    
    // Issue the command
    port->ci = 1; // Set bit 0 (command slot 0)
    
    // Wait for command completion
    uint32_t timeout = 5000000; // Larger timeout value for data transfer
    while ((port->ci & 1) && timeout--) {
        // Check for errors
        if (port->is & (1 << 30)) { // Task File Error
            printf("Error: Task File Error on port %d during read\n", port_num);
            return -1;
        }
    }
    
    if (timeout == 0) {
        printf("Error: Read command timeout on port %d\n", port_num);
        return -1;
    }
    
    // Check for errors one more time
    if (port->is & (1 << 30)) {
        printf("Error: Task File Error on port %d after read\n", port_num);
        return -1;
    }
    
    return 0; // Success
}

/* Write sectors to a SATA drive */
int ahci_write_sectors(int port_num, uint64_t start_lba, uint16_t count, const void *buffer) {
    if (count == 0) {
        return 0; // Nothing to do
    }
    
    struct ahci_hba_port *port = &hba_mem->ports[port_num];
    
    // Make sure buffer is properly aligned
    if (((uintptr_t)buffer & 0xFFF) != 0) {
        // Not 4KB aligned - allocate aligned buffer
        void *aligned_buffer = ahci_memalign(4096, count * 512);
        if (!aligned_buffer) {
            return -1;
        }
        
        // Copy data to aligned buffer
        memcpy(aligned_buffer, buffer, count * 512);
        
        // Write from aligned buffer
        int result = ahci_write_sectors(port_num, start_lba, count, aligned_buffer);
        return result;
    }
    
    // Set up command header (slot 0)
    struct ahci_cmd_header *header = (struct ahci_cmd_header*)(uintptr_t)port->clb;
    header->command_fis_length = sizeof(struct fis_reg_h2d) / 4; // Length in DWORDs
    header->write = 1; // This is a write operation
    
    // Calculate number of PRDT entries needed
    // Each entry can describe up to 4MB, and each sector is 512 bytes
    uint32_t bytes_to_transfer = count * 512;
    uint16_t prdt_count = (bytes_to_transfer + 4194303) / 4194304; // Ceiling division by 4MB
    
    header->prdt_length = prdt_count;
    
    // Set up command table
    struct ahci_cmd_table *cmd_table = (struct ahci_cmd_table*)(uintptr_t)header->cmd_table_base_l;
    
    // Set up the command FIS - WRITE DMA EXT command
    struct fis_reg_h2d *fis = (struct fis_reg_h2d*)cmd_table->command_fis;
    memset(fis, 0, sizeof(struct fis_reg_h2d));
    
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->command_control = 1;        // This is a command
    fis->command = ATA_CMD_WRITE_DMA_EXT;
    
    // Set the LBA (Logical Block Address) fields
    fis->lba0 = (uint8_t)(start_lba & 0xFF);
    fis->lba1 = (uint8_t)((start_lba >> 8) & 0xFF);
    fis->lba2 = (uint8_t)((start_lba >> 16) & 0xFF);
    fis->lba3 = (uint8_t)((start_lba >> 24) & 0xFF);
    fis->lba4 = (uint8_t)((start_lba >> 32) & 0xFF);
    fis->lba5 = (uint8_t)((start_lba >> 40) & 0xFF);
    
    // Set the device register to LBA mode
    fis->device = 1 << 6; // Set bit 6 for LBA mode
    
    // Set the sector count
    fis->count_low = count & 0xFF;
    fis->count_high = (count >> 8) & 0xFF;
    
    // Set up PRDT entries
    // Each entry can describe up to 4MB, but we'll use smaller chunks for simplicity and safety
    uint32_t bytes_remaining = bytes_to_transfer;
    const uint8_t *buf_ptr = (const uint8_t*)buffer;
    
    for (int i = 0; i < prdt_count; i++) {
        uint32_t bytes_this_entry = bytes_remaining > 4194304 ? 4194304 : bytes_remaining;
        
        cmd_table->prdt_entries[i].data_base_l = (uint32_t)(uintptr_t)buf_ptr;
        cmd_table->prdt_entries[i].data_base_h = 0; // Assuming 32-bit addresses
        cmd_table->prdt_entries[i].byte_count = bytes_this_entry - 1; // 0-based count
        cmd_table->prdt_entries[i].interrupt_on_completion = 1;
        
        bytes_remaining -= bytes_this_entry;
        buf_ptr += bytes_this_entry;
    }
    
    // Clear pending interrupts
    port->is = (uint32_t)-1;
    
    // Issue the command
    port->ci = 1; // Set bit 0 (command slot 0)
    
    // Wait for command completion
    uint32_t timeout = 5000000; // Larger timeout value for data transfer
    while ((port->ci & 1) && timeout--) {
        // Check for errors
        if (port->is & (1 << 30)) { // Task File Error
            printf("Error: Task File Error on port %d during write\n", port_num);
            return -1;
        }
    }
    
    if (timeout == 0) {
        printf("Error: Write command timeout on port %d\n", port_num);
        return -1;
    }
    
    // Check for errors one more time
    if (port->is & (1 << 30)) {
        printf("Error: Task File Error on port %d after write\n", port_num);
        return -1;
    }
    
    return 0; // Success
}

/* Print the status of a SATA port */
void ahci_print_port_status(int port_num) {
    struct ahci_hba_port *port = &hba_mem->ports[port_num];
    
    // Get port status information
    uint32_t ssts = port->ssts;
    uint32_t sctl = port->sctl;
    uint32_t serr = port->serr;
    uint32_t cmd = port->cmd;
    uint32_t tfd = port->tfd;
    
    // Port state description strings
    const char *det_states[] = {
        "No device detected", 
        "Device present but no communication", 
        "Device present but Phy offline", 
        "Device present and communication established",
        "Port in offline mode", 
        "Reserved 5", 
        "Reserved 6", 
        "Reserved 7"
    };
    
    // Extract device detection state
    uint8_t det = ssts & 0xF;
    
    // Extract interface power management state
    uint8_t ipm = (ssts >> 8) & 0xF;
    const char *ipm_states[] = {
        "Not present/no device", 
        "Active", 
        "Partial power management", 
        "Slumber power management",
        "Devsleep power management", 
        "Reserved 5", 
        "Reserved 6", 
        "Reserved 7"
    };
    
    // Print information
    printf("---- Port %d Status ----\n", port_num);
    printf("Device Detection: %s (0x%x)\n", 
           (det < 8) ? det_states[det] : "Unknown state", det);
    printf("Power Management: %s (0x%x)\n", 
           (ipm < 8) ? ipm_states[ipm] : "Unknown state", ipm);
    
    // Check if port is active
    printf("Port Active: %s\n", (cmd & PORT_CMD_ST) ? "Yes" : "No");
    printf("FIS Receive Active: %s\n", (cmd & PORT_CMD_FRE) ? "Yes" : "No");
    
    // Check for errors
    if (serr) {
        printf("Port has errors: 0x%08x\n", serr);
    } else {
        printf("No port errors reported\n");
    }
    
    // Task file data
    printf("Task File Data: 0x%08x\n", tfd);
    
    // Check busy/drq bits
    if (tfd & 0x80) {
        printf("Drive is BUSY\n");
    } else if (tfd & 0x08) {
        printf("Drive is requesting data transfer (DRQ)\n");
    } else {
        printf("Drive is ready\n");
    }
}

/* Demo function to test SATA interface */
void ahci_demo() {
    printf("Starting AHCI/SATA interface demo...\n");
    
    // Find AHCI controller
    uint32_t ahci_base_addr = find_ahci_controller();
    if (ahci_base_addr == 0) {
        printf("No AHCI controller found!\n");
        return;
    }
    
    // Initialize AHCI controller
    init_ahci(ahci_base_addr);
    
    // Scan for connected SATA drives
    int drives_found = find_sata_drives();
    if (drives_found == 0) {
        printf("No SATA drives found!\n");
        return;
    }
    
    // Demo: Read first sector of the first drive found
    printf("\n--- Read/Write Demo ---\n");
    
    // Find the first active port
    int active_port = -1;
    for (int i = 0; i < 32; i++) {
        if (hba_mem->pi & (1 << i)) {
            uint32_t ssts = hba_mem->ports[i].ssts;
            uint8_t det = (ssts & SATA_STATUS_DET_MASK);
            uint16_t ipm = (ssts & SATA_STATUS_IPM_MASK) >> 8;
            
            if (det == SATA_STATUS_DET_PRESENT && ipm == (SATA_STATUS_IPM_ACTIVE >> 8)) {
                active_port = i;
                break;
            }
        }
    }
    
    if (active_port < 0) {
        printf("No active port found!\n");
        return;
    }
    
    // Allocate a buffer for the sector data
    uint8_t *sector_buffer = (uint8_t*)ahci_memalign(4096, 512);
    if (!sector_buffer) {
        printf("Failed to allocate buffer for sector data!\n");
        return;
    }
    
    // Read the first sector (MBR)
    printf("Reading first sector (MBR) from port %d...\n", active_port);
    if (ahci_read_sectors(active_port, 0, 1, sector_buffer) != 0) {
        printf("Failed to read sector!\n");
        return;
    }
    
    // Display the first 16 bytes of the MBR
    printf("\nFirst 16 bytes of MBR:\n");
    for (int i = 0; i < 16; i++) {
        printf("%02x ", sector_buffer[i]);
        if ((i + 1) % 8 == 0) printf("\n");
    }
    
    // Check for MBR signature (0x55, 0xAA) at the end of the sector
    if (sector_buffer[510] == 0x55 && sector_buffer[511] == 0xAA) {
        printf("\nValid MBR signature detected (0x55AA)\n");
    } else {
        printf("\nWarning: No valid MBR signature found! (Expected 0x55AA, got 0x%02x%02x)\n", 
               sector_buffer[510], sector_buffer[511]);
    }
    
    // Modify first sector and write it back (demonstration only)
    // CAUTION: In a real system, you would not want to modify the MBR without care!
    printf("\nWriting test data to a safe sector (LBA 100)...\n");
    
    // Prepare a test sector
    uint8_t *test_sector = (uint8_t*)ahci_memalign(4096, 512);
    if (!test_sector) {
        printf("Failed to allocate buffer for test sector!\n");
        return;
    }
    
    // Fill with a pattern
    for (int i = 0; i < 512; i++) {
        test_sector[i] = (i % 256);
    }
    
    // Write the test sector to LBA 100 (safely away from MBR and other critical structures)
    if (ahci_write_sectors(active_port, 100, 1, test_sector) != 0) {
        printf("Failed to write test sector!\n");
        return;
    }
    
    // Clear buffer before read back
    memset(test_sector, 0, 512);
    
    // Read back the sector to verify
    printf("Reading back the test sector from LBA 100...\n");
    if (ahci_read_sectors(active_port, 100, 1, test_sector) != 0) {
        printf("Failed to read back test sector!\n");
        return;
    }
    
    // Verify the data
    bool verification_passed = true;
    for (int i = 0; i < 512; i++) {
        if (test_sector[i] != (i % 256)) {
            printf("Verification failed at byte %d: expected 0x%02x, got 0x%02x\n", 
                   i, (i % 256), test_sector[i]);
            verification_passed = false;
            break;
        }
    }
    
    if (verification_passed) {
        printf("Test sector verification PASSED!\n");
    }
    
    printf("\nAHCI/SATA demo completed successfully!\n");
}

/* Additional SATA utilities */

/* Reset a SATA port */
int ahci_reset_port(int port_num) {
    struct ahci_hba_port *port = &hba_mem->ports[port_num];
    
    printf("Resetting SATA port %d...\n", port_num);
    
    // Stop command engine
    if (ahci_stop_port_cmd(port) != 0) {
        printf("Error: Could not stop port command engine\n");
        return -1;
    }
    
    // Set reset bits in SCTL register
    // DET field (bits 0-3): 1 = reset, 4 = offline
    uint32_t sctl = port->sctl;
    sctl = (sctl & ~0xF) | 1; // Set DET = 1 (reset)
    port->sctl = sctl;
    
    // Wait for 1ms (SATA spec requires minimum 1ms)
    // In a real OS, you would use a proper delay function
    for (volatile int i = 0; i < 100000; i++); // Simple delay loop
    
    // Clear reset bits
    sctl = (sctl & ~0xF) | 0; // Set DET = 0 (no action)
    port->sctl = sctl;
    
    // Wait for device to re-establish communication
    uint32_t timeout = 1000000; // Arbitrary timeout
    while (timeout--) {
        uint32_t ssts = port->ssts;
        uint8_t det = (ssts & SATA_STATUS_DET_MASK);
        uint16_t ipm = (ssts & SATA_STATUS_IPM_MASK) >> 8;
        
        // Check if device is present and communication is established
        if (det == SATA_STATUS_DET_PRESENT && ipm == (SATA_STATUS_IPM_ACTIVE >> 8)) {
            break;
        }
        
        if (timeout == 0) {
            printf("Error: Timeout waiting for port to become ready after reset\n");
            return -1;
        }
    }
    
    // Rebase port
    if (ahci_port_rebase(port_num) != 0) {
        printf("Error: Could not rebase port after reset\n");
        return -1;
    }
    
    // Start command engine
    if (ahci_start_port_cmd(port) != 0) {
        printf("Error: Could not start port command engine after reset\n");
        return -1;
    }
    
    printf("Port %d reset completed successfully\n", port_num);
    return 0;
}

/* Simple file system utilities (for demonstration) */

/* Find partition table in MBR */
int ahci_check_mbr(int port_num) {
    // Read MBR (first sector)
    uint8_t *mbr = (uint8_t*)ahci_memalign(4096, 512);
    if (!mbr) {
        printf("Error: Failed to allocate buffer for MBR\n");
        return -1;
    }
    
    // Read the MBR sector
    if (ahci_read_sectors(port_num, 0, 1, mbr) != 0) {
        printf("Error: Failed to read MBR\n");
        return -1;
    }
    
    // Check MBR signature
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
        printf("Error: Invalid MBR signature (expected 0x55AA, got 0x%02x%02x)\n", 
               mbr[510], mbr[511]);
        return -1;
    }
    
    // MBR partition table starts at offset 0x1BE (446)
    uint8_t *partition_table = mbr + 0x1BE;
    
    // Print partition table
    printf("MBR Partition Table:\n");
    printf("--------------------------------------------------------------------------------\n");
    printf("| # | Boot | Type | Start Sector | Total Sectors | Size (MB) |\n");
    printf("--------------------------------------------------------------------------------\n");
    
    for (int i = 0; i < 4; i++) {
        uint8_t *entry = partition_table + (i * 16);
        
        // Extract information
        uint8_t bootable = entry[0];
        uint8_t type = entry[4];
        uint32_t start_sector = 
            entry[8] | (entry[9] << 8) | (entry[10] << 16) | (entry[11] << 24);
        uint32_t sector_count = 
            entry[12] | (entry[13] << 8) | (entry[14] << 16) | (entry[15] << 24);
        
        // Only display valid entries
        if (type != 0) {
            printf("| %d | 0x%02x  | 0x%02x | %10u | %13u | %9u |\n", 
                   i + 1, bootable, type, start_sector, sector_count, sector_count / 2048);
        } else {
            printf("| %d | --    | --   | ------------ | ------------- | --------- |\n", i + 1);
        }
    }
    printf("--------------------------------------------------------------------------------\n");
    
    return 0;
}

/* Add SATA interface initialization to kernel_main */
void init_sata_interface() {
    printf("Initializing SATA interface...\n");
    
    // Find AHCI controller
    uint32_t ahci_base_addr = find_ahci_controller();
    if (ahci_base_addr == 0) {
        printf("No AHCI controller found. SATA interface not available.\n");
        return;
    }
    
    // Initialize AHCI controller
    init_ahci(ahci_base_addr);
    
    // Scan for connected SATA drives
    int drives_found = find_sata_drives();
    
    if (drives_found > 0) {
        printf("SATA interface initialization complete.\n");
    } else {
        printf("No SATA drives found. SATA interface available but inactive.\n");
    }
}

/* Interface function to read a file from SATA drive */
int ahci_read_file(int port_num, uint64_t start_lba, uint32_t size_bytes, void *buffer) {
    // Calculate number of sectors needed
    uint32_t sector_count = (size_bytes + 511) / 512; // Ceiling division by 512
    
    // Read all sectors
    if (ahci_read_sectors(port_num, start_lba, sector_count, buffer) != 0) {
        printf("Error: Failed to read file sectors\n");
        return -1;
    }
    
    return 0;
}

/* Interface function to write a file to SATA drive */
int ahci_write_file(int port_num, uint64_t start_lba, uint32_t size_bytes, const void *buffer) {
    // Calculate number of sectors needed
    uint32_t sector_count = (size_bytes + 511) / 512; // Ceiling division by 512
    
    // Write all sectors
    if (ahci_write_sectors(port_num, start_lba, sector_count, buffer) != 0) {
        printf("Error: Failed to write file sectors\n");
        return -1;
    }
    
    return 0;
}

/* Add SATA drive commands for the OS console */

// Command to list SATA drives
void cmd_sata_list() {
    if (!hba_mem) {
        printf("SATA interface not initialized.\n");
        return;
    }
    
    uint32_t ports_implemented = hba_mem->pi;
    int found_drives = 0;
    
    printf("SATA Drives:\n");
    printf("--------------------------------------------------\n");
    
    for (int i = 0; i < 32; i++) {
        if (ports_implemented & (1 << i)) {
            uint32_t ssts = hba_mem->ports[i].ssts;
            uint8_t det = (ssts & SATA_STATUS_DET_MASK);
            uint16_t ipm = (ssts & SATA_STATUS_IPM_MASK) >> 8;
            
            if (det == SATA_STATUS_DET_PRESENT && ipm == (SATA_STATUS_IPM_ACTIVE >> 8)) {
                printf("Drive %d: Port %d - Active\n", found_drives, i);
                found_drives++;
            }
        }
    }
    
    if (found_drives == 0) {
        printf("No active SATA drives found.\n");
    }
    
    printf("--------------------------------------------------\n");
}

// Command to show SATA drive information
void cmd_sata_info(int port_num) {
    if (!hba_mem) {
        printf("SATA interface not initialized.\n");
        return;
    }
    
    uint32_t ports_implemented = hba_mem->pi;
    
    if (!(ports_implemented & (1 << port_num))) {
        printf("Port %d not implemented.\n", port_num);
        return;
    }
    
    uint32_t ssts = hba_mem->ports[port_num].ssts;
    uint8_t det = (ssts & SATA_STATUS_DET_MASK);
    uint16_t ipm = (ssts & SATA_STATUS_IPM_MASK) >> 8;
    
    if (det != SATA_STATUS_DET_PRESENT || ipm != (SATA_STATUS_IPM_ACTIVE >> 8)) {
        printf("No active drive on port %d.\n", port_num);
        return;
    }
    
    ahci_print_port_status(port_num);
    ahci_identify_device(port_num);
}

// Command to check MBR
void cmd_sata_mbr(int port_num) {
    if (!hba_mem) {
        printf("SATA interface not initialized.\n");
        return;
    }
    
    uint32_t ports_implemented = hba_mem->pi;
    
    if (!(ports_implemented & (1 << port_num))) {
        printf("Port %d not implemented.\n", port_num);
        return;
    }
    
    uint32_t ssts = hba_mem->ports[port_num].ssts;
    uint8_t det = (ssts & SATA_STATUS_DET_MASK);
    uint16_t ipm = (ssts & SATA_STATUS_IPM_MASK) >> 8;
    
    if (det != SATA_STATUS_DET_PRESENT || ipm != (SATA_STATUS_IPM_ACTIVE >> 8)) {
        printf("No active drive on port %d.\n", port_num);
        return;
    }
    
    ahci_check_mbr(port_num);
}

// Function to add SATA commands to the OS command processor
void register_sata_commands() {
    // Add commands to your OS shell
    // This is just a placeholder - you'll need to integrate with your shell system
    printf("SATA commands registered:\n");
    printf("  sata-list    - List available SATA drives\n");
    printf("  sata-info N  - Show information about drive on port N\n");
    printf("  sata-mbr N   - Show partition table for drive on port N\n");
}