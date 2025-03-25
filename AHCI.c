#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "ahci_driver.h"

// Maximum number of command slots per port
#define MAX_CMD_SLOTS 32

// Memory base address for AHCI (used in the example implementation)
#define AHCI_BASE 0x400000  // 4M

/**
 * Start command engine on the port
 */
void start_command(HBA_PORT *port) {
    // Wait until command list is no longer running
    while (port->command & CommandListRunning);
    
    // Enable FIS receive and start command engine
    port->command |= FisReceiveEnable;
    port->command |= Start;
}

/**
 * Stop command engine on the port
 */
void stop_command(HBA_PORT *port) {
    // Clear start bit
    port->command &= ~Start;
    
    // Clear FIS receive enable
    port->command &= ~FisReceiveEnable;
    
    // Wait until command list and FIS receive are no longer running
    while (1) {
        if (port->command & FisReceiveRunning)
            continue;
        if (port->command & CommandListRunning)
            continue;
        break;
    }
}

/**
 * Find a free command slot in the port
 */
int find_command_slot(HBA_PORT *port, int max_slots) {
    // If not set in SACT and CI, the slot is free
    uint32_t slots = (port->sataActive | port->commandIssue);
    for (int i = 0; i < max_slots; i++) {
        if ((slots & 1) == 0)
            return i;
        slots >>= 1;
    }
    printf("Cannot find free command slot\n");
    return -1;
}

/**
 * Checks the type of device connected to the port
 */
int check_port_type(HBA_PORT *port) {
    uint32_t ssts = port->sataStatus;
    
    // Check device detection and interface power management status
    uint8_t ipm = (ssts >> 8) & 0x0F;
    uint8_t det = ssts & 0x0F;
    
    // Check if device is present and active
    if (det != 3)  // Device not present
        return 0;
    if (ipm != 1)  // Interface not active
        return 0;
    
    // Check device signature to determine type
    switch (port->signature) {
        case SATAPI:
            return 2;  // SATAPI device
        case EnclosureManagement:
            return 3;  // Enclosure management bridge
        case PortMultiplier:
            return 4;  // Port multiplier
        default:
            return 1;  // SATA device
    }
}

/**
 * Utility function to convert a string from the device (byte-swapped)
 */
void convert_string(char *str, int len) {
    // Swap every pair of bytes to correct the ATA string format
    for (int i = 0; i < len; i += 2) {
        char temp = str[i];
        str[i] = str[i + 1];
        str[i + 1] = temp;
    }
}

/**
 * Process IDENTIFY command data to extract device information
 * 
 * The identify command returns a 512-byte block of data that contains
 * detailed information about the connected device.
 */
void process_identify_data(uint8_t *buffer) {
    uint16_t *identify_data = (uint16_t*)buffer;
    
    // Extract model number (words 27-46)
    char model[41];
    memcpy(model, buffer + 54, 40);
    model[40] = '\0';
    convert_string(model, 40);
    
    // Trim trailing spaces
    for (int i = 39; i >= 0 && model[i] == ' '; i--) {
        model[i] = '\0';
    }
    
    // Extract serial number (words 10-19)
    char serial[21];
    memcpy(serial, buffer + 20, 20);
    serial[20] = '\0';
    convert_string(serial, 20);
    
    // Trim trailing spaces
    for (int i = 19; i >= 0 && serial[i] == ' '; i--) {
        serial[i] = '\0';
    }
    
    // Extract firmware version (words 23-26)
    char firmware[9];
    memcpy(firmware, buffer + 46, 8);
    firmware[8] = '\0';
    convert_string(firmware, 8);
    
    // Trim trailing spaces
    for (int i = 7; i >= 0 && firmware[i] == ' '; i--) {
        firmware[i] = '\0';
    }
    
    // Check if 48-bit LBA is supported (word 83, bit 10)
    bool lba48_supported = (identify_data[83] & (1 << 10)) != 0;
    
    // Get size information
    uint64_t sector_count;
    if (lba48_supported) {
        // 48-bit LBA support (words 100-103)
        sector_count = *(uint64_t*)&identify_data[100];
    } else {
        // 28-bit LBA support (words 60-61)
        sector_count = *(uint32_t*)&identify_data[60];
    }
    
    // Extract sector size information (word 106)
    uint16_t logical_sector_info = identify_data[106];
    uint32_t logical_sector_size = 512;  // Default sector size
    
    if ((logical_sector_info & 0x8000) != 0) {  // Word is valid
        if ((logical_sector_info & 0x1000) != 0) {  // Logical sector size > 512 bytes
            logical_sector_size = identify_data[117] | (identify_data[118] << 16);
        }
    }
    
    // Calculate capacity
    uint64_t capacity_mb = (sector_count * logical_sector_size) / (1024 * 1024);
    
    printf("Disk Information:\n");
    printf("  Model: %s\n", model);
    printf("  Serial: %s\n", serial);
    printf("  Firmware: %s\n", firmware);
    printf("  Capacity: %llu MB (%llu sectors, %u bytes per sector)\n", 
           capacity_mb, sector_count, logical_sector_size);
    printf("  LBA48 Support: %s\n", lba48_supported ? "Yes" : "No");
}

/**
 * Probe all implemented ports to find connected devices
 */
void probe_ports(HBA_MEM *abar) {
    // Get the bitmap of implemented ports
    uint32_t pi = abar->portsImplemented;
    
    // Iterate through all 32 possible ports
    for (int i = 0; i < 32; i++) {
        if (pi & (1 << i)) {
            // Port is implemented, check its type
            int device_type = check_port_type(&abar->ports[i]);
            
            // Print device type information
            switch (device_type) {
                case 0:
                    printf("Port %d: No device detected\n", i);
                    break;
                case 1:
                    printf("Port %d: SATA drive detected\n", i);
                    initialize_port(&abar->ports[i], i);
                    break;
                case 2:
                    printf("Port %d: SATAPI drive detected\n", i);
                    break;
                case 3:
                    printf("Port %d: Enclosure Management Bridge detected\n", i);
                    break;
                case 4:
                    printf("Port %d: Port Multiplier detected\n", i);
                    break;
            }
        }
    }
}

/**
 * Initialize the port by setting up command list and FIS structures
 */
void initialize_port(HBA_PORT *port, int portno) {
    // Stop command engine before configuring
    stop_command(port);
    
    // Set up command list (1K per port)
    port->commandListBase = AHCI_BASE + (portno << 10);
    port->commandListBaseUpper = 0;
    memset((void*)(uintptr_t)(port->commandListBase), 0, 1024);
    
    // Set up received FIS (256 bytes per port)
    port->fisBase = AHCI_BASE + (32 << 10) + (portno << 8);
    port->fisBaseUpper = 0;
    memset((void*)(uintptr_t)(port->fisBase), 0, 256);
    
    // Set up command tables (8K per port)
    HBA_CMD_HEADER *cmdheader = (HBA_CMD_HEADER*)(uintptr_t)(port->commandListBase);
    for (int i = 0; i < 32; i++) {
        cmdheader[i].prdtLength = 8;    // 8 PRDT entries per table
        // Command table is 256 bytes (64+16+48+128) per command
        cmdheader[i].commandTableBaseAddr = AHCI_BASE + (40 << 10) + (portno << 13) + (i << 8);
        cmdheader[i].commandTableBaseAddrUpper = 0;
        memset((void*)(uintptr_t)(cmdheader[i].commandTableBaseAddr), 0, 256);
    }
    
    // Start command engine
    start_command(port);
    
    // Set up interrupts for this port
    port->interruptEnable = DeviceToHostRegister | 
                            PioSetup |
                            DescriptorProcessed |
                            TaskFileError;
    
    // Identify the device on this port
    uint8_t identify_buffer[512];
    if (identify_device(port, identify_buffer)) {
        process_identify_data(identify_buffer);
    }
}

/**
 * Sends IDENTIFY DEVICE command to the specified port
 * 
 * This command will extract information about the device connected
 * to the specified port and store it in the provided buffer.
 */
bool identify_device(HBA_PORT *port, void *buffer) {
    port->interruptStatus = 0xFFFFFFFF;  // Clear pending interrupts
    
    // Find a free command slot
    int cmd_slot = find_command_slot(port, 32);
    if (cmd_slot == -1) {
        return false;
    }
    
    // Get command header and set up command
    HBA_CMD_HEADER *cmdheader = (HBA_CMD_HEADER*)(uintptr_t)(port->commandListBase);
    cmdheader += cmd_slot;
    
    cmdheader->commandFISLength = sizeof(FIS_REG_H2D) / sizeof(uint32_t);  // Size in DWORDs
    cmdheader->write = 0;  // Read operation
    cmdheader->prdtLength = 1;  // One PRDT entry
    
    // Set up command table
    HBA_CMD_TABLE *cmdtbl = (HBA_CMD_TABLE*)(uintptr_t)(cmdheader->commandTableBaseAddr);
    memset(cmdtbl, 0, sizeof(HBA_CMD_TABLE));
    
    // Set up the PRDT entry (Physical Region Descriptor Table)
    cmdtbl->prdtEntry[0].dataBaseAddress = (uint32_t)(uintptr_t)buffer;
    cmdtbl->prdtEntry[0].dataBaseAddressUpper = 0;
    cmdtbl->prdtEntry[0].byteCount = 512 - 1;  // 512 bytes (0-based)
    cmdtbl->prdtEntry[0].interruptOnCompletion = 1;
    
    // Set up the command FIS (Frame Information Structure)
    FIS_REG_H2D *cmdfis = (FIS_REG_H2D*)(&cmdtbl->commandFIS);
    memset(cmdfis, 0, sizeof(FIS_REG_H2D));
    
    cmdfis->fisType = RegisterHostToDevice;
    cmdfis->commandControl = 1;  // This is a command
    cmdfis->command = Identify;  // IDENTIFY command
    cmdfis->device = 0;  // Master device
    
    // Wait for port to be ready (not busy or DRQ)
    int spin = 0;
    while ((port->taskFileData & (Busy | 0x08)) && spin < 1000000) {
        spin++;
    }
    
    if (spin >= 1000000) {
        printf("Port is hung or busy\n");
        return false;
    }
    
    // Issue the command
    port->commandIssue = 1 << cmd_slot;
    
    // Wait for completion
    int timeout = 0;
    while (timeout < 1000000) {
        // Command completed (command issue bit cleared)
        if ((port->commandIssue & (1 << cmd_slot)) == 0)
            break;
            
        // Task file error
        if (port->interruptStatus & TaskFileError) {
            printf("Identify command error\n");
            return false;
        }
        
        timeout++;
    }
    
    // Check for timeout
    if (timeout >= 1000000) {
        printf("Identify command timed out\n");
        return false;
    }
    
    // Check for errors again
    if (port->interruptStatus & TaskFileError) {
        printf("Identify command error\n");
        return false;
    }
    
    printf("Identify device command completed successfully\n");
    return true;
}

/**
 * Read sectors from the drive through the specified port
 * 
 * @param port      Pointer to the port structure
 * @param start     Starting LBA address (can be 48-bit)
 * @param count     Number of sectors to read
 * @param buffer    Buffer to store the read data
 * @return          true if read was successful, false otherwise
 */
bool read_sectors(HBA_PORT *port, uint64_t start, uint32_t count, void *buffer) {
    // Clear pending interrupts
    port->interruptStatus = 0xFFFFFFFF;
    
    // Find a free command slot
    int slot = find_command_slot(port, 32);
    if (slot == -1) {
        return false;
    }
    
    // Setup command header
    HBA_CMD_HEADER *cmdheader = (HBA_CMD_HEADER*)(uintptr_t)(port->commandListBase);
    cmdheader += slot;
    
    cmdheader->commandFISLength = sizeof(FIS_REG_H2D) / sizeof(uint32_t);  // Command FIS size in DWORDs
    cmdheader->write = 0;    // Read operation
    
    // Calculate PRDT entries needed: each entry can handle up to 8K (16 sectors)
    // We need to split large transfers into multiple PRDTs
    const uint32_t SECTORS_PER_PRDT = 16;
    const uint32_t BYTES_PER_SECTOR = 512;
    
    uint32_t entries = (count + SECTORS_PER_PRDT - 1) / SECTORS_PER_PRDT;
    if (entries > 65535) {
        printf("Error: Too many PRDT entries required\n");
        return false;
    }
    
    cmdheader->prdtLength = entries;
    
    // Setup command table
    HBA_CMD_TABLE *cmdtbl = (HBA_CMD_TABLE*)(uintptr_t)(cmdheader->commandTableBaseAddr);
    memset(cmdtbl, 0, sizeof(HBA_CMD_TABLE) + (entries - 1) * sizeof(HBA_PRDT_ENTRY));
    
    // Setup PRDT entries
    uint8_t *buf_ptr = (uint8_t*)buffer;
    uint32_t remaining_sectors = count;
    
    for (uint32_t i = 0; i < entries - 1; i++) {
        cmdtbl->prdtEntry[i].dataBaseAddress = (uint32_t)(uintptr_t)buf_ptr;
        cmdtbl->prdtEntry[i].dataBaseAddressUpper = 0;
        cmdtbl->prdtEntry[i].byteCount = SECTORS_PER_PRDT * BYTES_PER_SECTOR - 1;  // 0-based
        cmdtbl->prdtEntry[i].interruptOnCompletion = 0;
        
        buf_ptr += SECTORS_PER_PRDT * BYTES_PER_SECTOR;
        remaining_sectors -= SECTORS_PER_PRDT;
    }
    
    // Setup last PRDT entry
    cmdtbl->prdtEntry[entries - 1].dataBaseAddress = (uint32_t)(uintptr_t)buf_ptr;
    cmdtbl->prdtEntry[entries - 1].dataBaseAddressUpper = 0;
    cmdtbl->prdtEntry[entries - 1].byteCount = remaining_sectors * BYTES_PER_SECTOR - 1;  // 0-based
    cmdtbl->prdtEntry[entries - 1].interruptOnCompletion = 1;  // Interrupt on completion of last PRDT
    
    // Setup command FIS
    FIS_REG_H2D *cmdfis = (FIS_REG_H2D*)(&cmdtbl->commandFIS);
    memset(cmdfis, 0, sizeof(FIS_REG_H2D));
    
    cmdfis->fisType = RegisterHostToDevice;
    cmdfis->commandControl = 1;         // This is a command
    cmdfis->command = ReadDma48;        // READ DMA EXT command
    cmdfis->device = 1 << 6;            // LBA mode
    
    // Set LBA address
    cmdfis->lba0 = start & 0xFF;
    cmdfis->lba1 = (start >> 8) & 0xFF;
    cmdfis->lba2 = (start >> 16) & 0xFF;
    cmdfis->lba3 = (start >> 24) & 0xFF;
    cmdfis->lba4 = (start >> 32) & 0xFF;
    cmdfis->lba5 = (start >> 40) & 0xFF;
    
    // Set sector count
    cmdfis->countLow = count & 0xFF;
    cmdfis->countHigh = (count >> 8) & 0xFF;
    
    // Wait for port to be ready
    int spin = 0;
    while ((port->taskFileData & (Busy | 0x08)) && spin < 1000000) {
        spin++;
    }
    
    if (spin >= 1000000) {
        printf("Port is hung or busy\n");
        return false;
    }
    
    // Issue command
    port->commandIssue = 1 << slot;
    
    // Wait for completion
    while (1) {
        if ((port->commandIssue & (1 << slot)) == 0) {
            break;  // Command completed
        }
        
        if (port->interruptStatus & TaskFileError) {
            printf("Read error: %08x\n", port->taskFileData);
            return false;
        }
    }
    
    // Check for errors
    if (port->interruptStatus & TaskFileError) {
        printf("Read error: %08x\n", port->taskFileData);
        return false;
    }
    
    return true;
}

/**
 * Get the number of command slots supported by the HBA
 */
int get_command_slots_count(HBA_MEM *abar) {
    // Extract the command slots field from the host capabilities register
    // Bits 8-12 of the register contain the 0-based count of command slots
    uint32_t caps = abar->hostCapability;
    uint32_t slots = ((caps >> 8) & 0x1F) + 1;  // Add 1 because it's 0-based
    
    if (slots > MAX_CMD_SLOTS) {
        // Limit to our defined maximum
        slots = MAX_CMD_SLOTS;
    }
    
    return slots;
}


/**
 * Simplified device identification for kernel integration
 * Assumes memory structures are already set up
 */
bool simple_identify_device(HBA_PORT* port, uint8_t* buffer) {
    // Clear pending interrupts
    port->interruptStatus = 0xFFFFFFFF;
    
    // Find a free command slot - use slot 0 for simplicity
    int slot = 0;
    
    // Get command header
    uint32_t port_num = ((uintptr_t)port - (uintptr_t)&(((HBA_MEM*)0)->ports[0])) / sizeof(HBA_PORT);
    HBA_CMD_HEADER* cmdheader = (HBA_CMD_HEADER*)(uintptr_t)(AHCI_BASE + (port_num << 10));
    cmdheader += slot;
    
    // Set up command header
    cmdheader->commandFISLength = 5; // FIS_REG_H2D size in DWORDs (20 bytes / 4)
    cmdheader->write = 0; // Read operation
    cmdheader->prdtLength = 1; // One PRDT entry
    
    // Set up command table
    HBA_CMD_TABLE* cmdtbl = (HBA_CMD_TABLE*)(uintptr_t)(cmdheader->commandTableBaseAddr);
    memset(cmdtbl, 0, sizeof(HBA_CMD_TABLE));
    
    // Set up the PRDT entry
    cmdtbl->prdtEntry[0].dataBaseAddress = (uint32_t)(uintptr_t)buffer;
    cmdtbl->prdtEntry[0].dataBaseAddressUpper = 0;
    cmdtbl->prdtEntry[0].byteCount = 512 - 1; // 512 bytes (0-based)
    cmdtbl->prdtEntry[0].interruptOnCompletion = 1;
    
    // Set up command FIS
    FIS_REG_H2D* cmdfis = (FIS_REG_H2D*)(&cmdtbl->commandFIS);
    memset(cmdfis, 0, sizeof(FIS_REG_H2D));
    
    cmdfis->fisType = RegisterHostToDevice;
    cmdfis->commandControl = 1; // This is a command
    cmdfis->command = Identify; // IDENTIFY DEVICE command
    cmdfis->device = 0; // Master device
    
    // Wait for port to be ready
    int timeout = 0;
    while ((port->taskFileData & (Busy | 0x08)) && timeout < 1000000) {
        timeout++;
        if (timeout >= 1000000) {
            printf("Port is hung or busy\n");
            return false;
        }
    }
    
    // Issue command
    port->commandIssue = 1 << slot;
    
    // Wait for completion
    timeout = 0;
    while (timeout < 1000000) {
        // Command completed (command issue bit cleared)
        if ((port->commandIssue & (1 << slot)) == 0)
            break;
            
        // Task file error
        if (port->interruptStatus & TaskFileError) {
            printf("Identify command error\n");
            return false;
        }
        
        timeout++;
    }
    
    // Check for timeout
    if (timeout >= 1000000) {
        printf("Identify command timed out\n");
        return false;
    }
    
    // Check for errors again
    if (port->interruptStatus & TaskFileError) {
        printf("Identify command error\n");
        return false;
    }
    
    return true;
}


/**
 * Entry point for AHCI driver demo when integrated in a kernel
 * 
 * This function provides a simplified demonstration of AHCI capabilities
 * without requiring user interaction or special privileges.
 */
void ahci_demo(void) {
    printf("AHCI Demo Starting\n");
    
    // Variables for our demo
    HBA_MEM* abar = NULL;
    uint32_t ports_implemented = 0;
    
    // In a real kernel, we would:
    // 1. Find the AHCI controller through PCI enumeration
    // 2. Map the memory-mapped registers
    // 3. Initialize the controller
    
    printf("Searching for AHCI controller via PCI...\n");
    
    // For demo purposes, assume we found the controller
    // and mapped its ABAR (this would be a physical memory mapping in a real kernel)
    // This would be the actual mapped address in a real implementation
    abar = (HBA_MEM*)0xFEDC0000;  // Example address - in a real kernel this would be mapped properly
    
    // Check if we have a valid ABAR pointer
    if (!abar) {
        printf("AHCI Demo: No AHCI controller found or ABAR not mapped\n");
        return;
    }
    
    // Print controller version
    printf("AHCI controller version: %d.%d\n", 
           (abar->version >> 16) & 0xFFFF,
           abar->version & 0xFFFF);
    
    // Get implemented ports bitmap
    ports_implemented = abar->portsImplemented;
    printf("Ports implemented bitmap: 0x%x\n", ports_implemented);
    
    // Count the number of implemented ports
    int port_count = 0;
    for (int i = 0; i < 32; i++) {
        if (ports_implemented & (1 << i)) {
            port_count++;
        }
    }
    printf("Number of implemented ports: %d\n", port_count);
    
    // Enumerate all implemented ports
    printf("\nScanning ports...\n");
    for (int i = 0; i < 32; i++) {
        if (ports_implemented & (1 << i)) {
            HBA_PORT* port = &abar->ports[i];
            
            // Check port signature to determine device type
            uint32_t signature = port->signature;
            const char* device_type = "Unknown";
            
            if (signature == SATA) {
                device_type = "SATA Drive";
            } else if (signature == SATAPI) {
                device_type = "SATAPI Drive";
            } else if (signature == PortMultiplier) {
                device_type = "Port Multiplier";
            } else if (signature == EnclosureManagement) {
                device_type = "Enclosure Management Bridge";
            }
            
            //printf("Port %d - Status: %08X, Signature: %08X, Type: %s\n", i, port->sataStatus, signature, device_type);
            
            // Check if device is present and active
            uint8_t ipm = (port->sataStatus >> 8) & 0x0F;
            uint8_t det = port->sataStatus & 0x0F;
            
            if (det == 3 && ipm == 1) {
				printf("  Device is present and active\n");
				
				// 1. Initialize the port
				stop_command(port);  // Stop command engine before configuring
				
				// Set up command list (1K per port)
				port->commandListBase = AHCI_BASE + (i << 10);
				port->commandListBaseUpper = 0;
				memset((void*)(uintptr_t)(port->commandListBase), 0, 1024);
				
				// Set up received FIS (256 bytes per port)
				port->fisBase = AHCI_BASE + (32 << 10) + (i << 8);
				port->fisBaseUpper = 0;
				memset((void*)(uintptr_t)(port->fisBase), 0, 256);
				
				// Set up command tables (8K per port)
				HBA_CMD_HEADER *cmdheader = (HBA_CMD_HEADER*)(uintptr_t)(port->commandListBase);
				for (int j = 0; j < 32; j++) {
					cmdheader[j].prdtLength = 8;    // 8 PRDT entries per table
					// Command table is 256 bytes (64+16+48+128) per command
					cmdheader[j].commandTableBaseAddr = AHCI_BASE + (40 << 10) + (i << 13) + (j << 8);
					cmdheader[j].commandTableBaseAddrUpper = 0;
					memset((void*)(uintptr_t)(cmdheader[j].commandTableBaseAddr), 0, 256);
				}
				
				// Start command engine
				start_command(port);
				
				// Set up interrupts for this port
				port->interruptEnable = DeviceToHostRegister | 
										TaskFileError |
										DescriptorProcessed;
				
				// 2. Identify the device
				printf("  Identifying device on port %d\n", i);
				
				// Allocate a buffer for identify data
				uint8_t identify_buffer[512];
				memset(identify_buffer, 0, 512);
				
				if (simple_identify_device(port, identify_buffer)) {
					// Process identify data
					printf("  Device identified successfully\n");
					
					// Extract model and serial number
					char model[41];
					char serial[21];
					
					// Model is at word 27-46 (offset 54)
					memcpy(model, &identify_buffer[54], 40);
					model[40] = '\0';
					
					// Serial is at word 10-19 (offset 20)
					memcpy(serial, &identify_buffer[20], 20);
					serial[20] = '\0';
					
					// Convert ATA strings (byte-swapped)
					convert_string(model, 40);
					convert_string(serial, 20);
					
					// Trim trailing spaces
					for (int j = 39; j >= 0 && model[j] == ' '; j--)
						model[j] = '\0';
					
					for (int j = 19; j >= 0 && serial[j] == ' '; j--)
						serial[j] = '\0';
					
					printf("  Model: %s\n", model);
					printf("  Serial: %s\n", serial);
					
					// Extract size information
					uint16_t* identify_data = (uint16_t*)identify_buffer;
					
					// Check for 48-bit LBA support (word 83, bit 10)
					bool lba48_supported = (identify_data[83] & (1 << 10)) != 0;
					
					// Get size
					uint64_t sector_count;
					if (lba48_supported) {
						// 48-bit LBA support (words 100-103)
						sector_count = *(uint64_t*)&identify_data[100];
					} else {
						// 28-bit LBA support (words 60-61)
						sector_count = *(uint32_t*)&identify_data[60];
					}
					
					uint64_t capacity_mb = (sector_count * 512) / (1024 * 1024);
					printf("  Capacity: %llu MB (%llu sectors)\n", capacity_mb, sector_count);
					printf("  LBA48 Support: %s\n", lba48_supported ? "Yes" : "No");
					
					// 3. You could read sectors from the device here
					// For example, read the first sector (MBR)
					uint8_t read_buffer[512];
					if (read_sectors(port, 0, 1, read_buffer)) {
						printf("  Successfully read first sector\n");
						
						// Display first 16 bytes
						printf("  First 16 bytes: ");
						for (int j = 0; j < 16; j++) {
							printf("%02x ", read_buffer[j]);
						}
						printf("\n");
					}
				} else {
					printf("  Failed to identify device\n");
				}
			}



			else {
                //printf("  No active device on this port\n");
            }
            //printf("\n");
        }
    }
    
    printf("AHCI Demo Completed\n");
}







