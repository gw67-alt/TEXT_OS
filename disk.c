#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>


// Define structures needed for AHCI commands
// PCI Configuration Space Access Ports
#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

// PCI Class Codes
#define PCI_CLASS_STORAGE             0x01
#define PCI_SUBCLASS_SATA             0x06
#define PCI_PROG_IF_AHCI              0x01

// HBA Port Command Bits
#define HBA_PxCMD_ST    0x0001  // Start
#define HBA_PxCMD_FRE   0x0010  // FIS Receive Enable
#define HBA_PxCMD_FR    0x4000  // FIS Receive Running
#define HBA_PxCMD_CR    0x8000  // Command List Running

// Port Status Values
#define HBA_PORT_IPM_ACTIVE 1
#define HBA_PORT_DET_PRESENT 3

// FIS Types
#define FIS_TYPE_REG_H2D    0x27    // Register FIS - Host to Device
#define FIS_TYPE_REG_D2H    0x34    // Register FIS - Device to Host
#define FIS_TYPE_DMA_ACT    0x39    // DMA Activate FIS
#define FIS_TYPE_DMA_SETUP  0x41    // DMA Setup FIS
#define FIS_TYPE_DATA       0x46    // Data FIS
#define FIS_TYPE_BIST       0x58    // BIST Activate FIS
#define FIS_TYPE_PIO_SETUP  0x5F    // PIO Setup FIS
#define FIS_TYPE_DEV_BITS   0xA1    // Set Device Bits FIS

// ATA Commands
#define ATA_CMD_IDENTIFY    0xEC    // IDENTIFY DEVICE
// Register - Host to Device FIS structure
// First define all the typedefs for the structures we'll use
struct hba_prdt_entry;
struct hba_cmd_header;
struct hba_cmd_tbl;
struct hba_mem;
struct fis_reg_h2d;

typedef struct hba_prdt_entry hba_prdt_entry_t;
typedef struct hba_cmd_header hba_cmd_header_t;
typedef struct hba_cmd_tbl hba_cmd_tbl_t;
typedef struct hba_mem hba_mem_t;
typedef struct fis_reg_h2d fis_reg_h2d_t;

// Now define the structure contents
struct hba_prdt_entry {
    uint32_t dba;           // Data Base Address
    uint32_t dbau;          // Data Base Address Upper 32 bits
    uint32_t rsv0;          // Reserved
    
    // DWORD 3
    uint32_t dbc:22;        // Byte Count, 4M max
    uint32_t rsv1:9;        // Reserved
    uint32_t i:1;           // Interrupt on completion
};

struct fis_reg_h2d {
    // DWORD 0
    uint8_t  fis_type;      // FIS Type
    uint8_t  pmport:4;      // Port Multiplier Port
    uint8_t  rsv0:3;        // Reserved
    uint8_t  c:1;           // 1: Command, 0: Control
    uint8_t  command;       // Command Register
    uint8_t  featurel;      // Feature Register Low
    
    // DWORD 1
    uint8_t  lba0;          // LBA Low Register
    uint8_t  lba1;          // LBA Mid Register
    uint8_t  lba2;          // LBA High Register
    uint8_t  device;        // Device Register
    
    // DWORD 2
    uint8_t  lba3;          // LBA Register 3
    uint8_t  lba4;          // LBA Register 4
    uint8_t  lba5;          // LBA Register 5
    uint8_t  featureh;      // Feature Register High
    
    // DWORD 3
    uint8_t  countl;        // Count Register Low
    uint8_t  counth;        // Count Register High
    uint8_t  icc;           // Isochronous Command Completion
    uint8_t  control;       // Control Register
    
    // DWORD 4
    uint8_t  rsv1[4];       // Reserved
};

struct hba_cmd_header {
    // DWORD 0
    uint8_t  cfl:5;         // Command FIS Length in DWORDS, 2 ~ 16
    uint8_t  a:1;           // ATAPI
    uint8_t  w:1;           // Write, 1: H2D, 0: D2H
    uint8_t  p:1;           // Prefetchable
    
    uint8_t  r:1;           // Reset
    uint8_t  b:1;           // BIST
    uint8_t  c:1;           // Clear Busy upon R_OK
    uint8_t  rsv0:1;        // Reserved
    uint8_t  pmp:4;         // Port Multiplier Port
    
    uint16_t prdtl;         // Physical Region Descriptor Table Length in entries
    
    // DWORD 1
    volatile uint32_t prdbc; // Physical Region Descriptor Byte Count Transferred
    
    // DWORD 2-3
    uint32_t ctba;          // Command Table Descriptor Base Address
    uint32_t ctbau;         // Command Table Descriptor Base Address Upper 32 bits
    
    // DWORD 4-7
    uint32_t rsv1[4];       // Reserved
};

struct hba_cmd_tbl {
    // 0x00
    uint8_t  cfis[64];      // Command FIS
    
    // 0x40
    uint8_t  acmd[16];      // ATAPI command, 12 or 16 bytes
    
    // 0x50
    uint8_t  rsv[48];       // Reserved
    
    // 0x80
    struct hba_prdt_entry prdt_entry[1]; // Physical Region Descriptor Table Entries, 0 ~ 65535
};

struct hba_mem {
    // 0x00 - 0x2B, Generic Host Control
    uint32_t cap;        // 0x00, Host Capabilities
    uint32_t ghc;        // 0x04, Global Host Control
    uint32_t is;         // 0x08, Interrupt Status
    uint32_t pi;         // 0x0C, Ports Implemented
    uint32_t vs;         // 0x10, Version
    uint32_t ccc_ctl;    // 0x14, Command Completion Coalescing Control
    uint32_t ccc_pts;    // 0x18, Command Completion Coalescing Ports
    uint32_t em_loc;     // 0x1C, Enclosure Management Location
    uint32_t em_ctl;     // 0x20, Enclosure Management Control
    uint32_t cap2;       // 0x24, Host Capabilities Extended
    uint32_t bohc;       // 0x28, BIOS/OS Handoff Control and Status
    
    // 0x2C - 0xFF, Reserved
    uint8_t reserved[0xA0 - 0x2C];
    
    // 0xA0 - 0xFF, Vendor Specific registers
    uint8_t vendor[0x100 - 0xA0];
    
    // 0x100 - 0x10FF, Port control registers
    // Each port has 128 bytes of register space
    struct {
        uint32_t clb;       // 0x00, Command List Base Address
        uint32_t clbu;      // 0x04, Command List Base Address Upper 32-bits
        uint32_t fb;        // 0x08, FIS Base Address
        uint32_t fbu;       // 0x0C, FIS Base Address Upper 32-bits
        uint32_t is;        // 0x10, Interrupt Status
        uint32_t ie;        // 0x14, Interrupt Enable
        uint32_t cmd;       // 0x18, Command and Status
        uint32_t reserved0; // 0x1C, Reserved
        uint32_t tfd;       // 0x20, Task File Data
        uint32_t sig;       // 0x24, Signature
        uint32_t ssts;      // 0x28, SATA Status (SCR0:SStatus)
        uint32_t sctl;      // 0x2C, SATA Control (SCR2:SControl)
        uint32_t serr;      // 0x30, SATA Error (SCR1:SError)
        uint32_t sact;      // 0x34, SATA Active (SCR3:SActive)
        uint32_t ci;        // 0x38, Command Issue
        uint32_t sntf;      // 0x3C, SATA Notification (SCR4:SNotification)
        uint32_t fbs;       // 0x40, FIS-based Switching Control
        
        // 0x44 - 0x6F, Reserved
        uint32_t reserved1[11];
        
        // 0x70 - 0x7F, Port Vendor Specific
        uint32_t vendor[4];
    } ports[32];
};
// Helper function to find a free command slot
int find_cmdslot(hba_mem_t *hba, int port) {
    // If not set in SACT and CI, the slot is free
    uint32_t slots = (hba->ports[port].sact | hba->ports[port].ci);
    
    for (int i = 0; i < 32; i++) {
        if ((slots & (1 << i)) == 0) {
            return i;
        }
    }
    
    printf("Cannot find free command slot\n");
    return -1;
}

// Function to stop port command engine
void stop_cmd(hba_mem_t *hba, int port) {
    // Clear ST bit
    hba->ports[port].cmd &= ~HBA_PxCMD_ST;
    
    // Wait until FR and CR are cleared
    while (1) {
        if ((hba->ports[port].cmd & HBA_PxCMD_FR) ||
            (hba->ports[port].cmd & HBA_PxCMD_CR))
            continue;
        break;
    }
}

// Function to start port command engine
void start_cmd(hba_mem_t *hba, int port) {
    // Wait until CR is cleared
    while (hba->ports[port].cmd & HBA_PxCMD_CR);
    
    // Set FRE and ST
    hba->ports[port].cmd |= HBA_PxCMD_FRE;
    hba->ports[port].cmd |= HBA_PxCMD_ST;
}

// Allocate memory for command list, FIS, command table and PRD entries
void setup_port_memory(hba_mem_t *hba, int port) {
    // Allocate command list (1KB aligned)
    void *cmdlist = aligned_alloc(1024, 1024);
    memset(cmdlist, 0, 1024);
    hba->ports[port].clb = (uint32_t)(uintptr_t)cmdlist;
    hba->ports[port].clbu = 0; // Assuming 32-bit address space
    
    // Allocate FIS buffer (256B aligned)
    void *fb = aligned_alloc(256, 256);
    memset(fb, 0, 256);
    hba->ports[port].fb = (uint32_t)(uintptr_t)fb;
    hba->ports[port].fbu = 0; // Assuming 32-bit address space
    
    // Allocate command table for each command header (128B aligned)
    hba_cmd_header_t *cmdheader = (hba_cmd_header_t*)((uintptr_t)hba->ports[port].clb);
    
    for (int i = 0; i < 32; i++) {
        cmdheader[i].prdtl = 8; // 8 PRD entries per table
        
        void *cmdtbl = aligned_alloc(128, 256 + (cmdheader[i].prdtl * sizeof(hba_prdt_entry_t)));
        memset(cmdtbl, 0, 256 + (cmdheader[i].prdtl * sizeof(hba_prdt_entry_t)));
        
        cmdheader[i].ctba = (uint32_t)(uintptr_t)cmdtbl;
        cmdheader[i].ctbau = 0; // Assuming 32-bit address space
    }
}

// Destroy allocated memory for port
void cleanup_port_memory(hba_mem_t *hba, int port) {
    hba_cmd_header_t *cmdheader = (hba_cmd_header_t*)((uintptr_t)hba->ports[port].clb);
    
    for (int i = 0; i < 32; i++) {
        if (cmdheader[i].ctba) {
            free((void*)(uintptr_t)cmdheader[i].ctba);
        }
    }
    
    free((void*)(uintptr_t)hba->ports[port].clb);
    free((void*)(uintptr_t)hba->ports[port].fb);
}

// Issue IDENTIFY DEVICE command to get disk properties
bool identify_device(hba_mem_t *hba, int port, uint16_t *identify_data) {
    uint32_t spin = 0;
    int slot = find_cmdslot(hba, port);
    if (slot == -1) return false;
    
    hba_cmd_header_t *cmdheader = (hba_cmd_header_t*)((uintptr_t)hba->ports[port].clb);
    cmdheader[slot].cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t); // Command FIS size
    cmdheader[slot].w = 0;  // Read operation
    cmdheader[slot].prdtl = 1; // Only 1 PRD entry
    
    hba_cmd_tbl_t *cmdtbl = (hba_cmd_tbl_t*)((uintptr_t)cmdheader[slot].ctba);
    memset(cmdtbl, 0, sizeof(hba_cmd_tbl_t) + (cmdheader[slot].prdtl-1)*sizeof(hba_prdt_entry_t));
    
    // Setup PRD entry
    cmdtbl->prdt_entry[0].dba = (uint32_t)(uintptr_t)identify_data;
    cmdtbl->prdt_entry[0].dbau = 0; // Assuming 32-bit address space
    cmdtbl->prdt_entry[0].dbc = 512-1; // 512 bytes (size-1)
    cmdtbl->prdt_entry[0].i = 1; // Interrupt when complete
    
    // Setup command FIS
    fis_reg_h2d_t *cmdfis = (fis_reg_h2d_t*)cmdtbl->cfis;
    memset(cmdfis, 0, sizeof(fis_reg_h2d_t));
    
    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1;  // This is a command
    cmdfis->command = ATA_CMD_IDENTIFY;
    cmdfis->device = 0;  // LBA mode
    
    // Issue command
    stop_cmd(hba, port);
    start_cmd(hba, port);
    
    // Clear interrupt status
    hba->ports[port].is = 0xFFFFFFFF;
    
    // Issue command by setting command issue bit
    hba->ports[port].ci = 1 << slot;
    
    // Wait for completion
    while (1) {
        // Break if error occurred
        if (hba->ports[port].is & (1 << 30)) {
            printf("Error occurred while identifying device\n");
            return false;
        }
        
        // Check if command completed
        if ((hba->ports[port].ci & (1 << slot)) == 0) {
            break;
        }
        
        // Timeout mechanism
        spin++;
        if (spin > 1000000) {
            printf("Port hung\n");
            return false;
        }
    }
    
    // Check for error
    if (hba->ports[port].tfd & 0x01) {
        printf("Device error\n");
        return false;
    }
    
    return true;
}

// Parse model name from IDENTIFY data
void parse_model_name(uint16_t *identify_data, char *model) {
    // Model name starts at word 27 and is 40 bytes (20 words) long
    for (int i = 0; i < 20; i++) {
        // ATA strings are byte-swapped
        model[i*2] = (identify_data[27+i] >> 8) & 0xFF;
        model[i*2+1] = identify_data[27+i] & 0xFF;
    }
    model[40] = '\0';
    
    // Trim trailing spaces
    for (int i = 39; i >= 0; i--) {
        if (model[i] == ' ')
            model[i] = '\0';
        else
            break;
    }
}

// Parse serial number from IDENTIFY data
void parse_serial_number(uint16_t *identify_data, char *serial) {
    // Serial number starts at word 10 and is 20 bytes (10 words) long
    for (int i = 0; i < 10; i++) {
        // ATA strings are byte-swapped
        serial[i*2] = (identify_data[10+i] >> 8) & 0xFF;
        serial[i*2+1] = identify_data[10+i] & 0xFF;
    }
    serial[20] = '\0';
    
    // Trim trailing spaces
    for (int i = 19; i >= 0; i--) {
        if (serial[i] == ' ')
            serial[i] = '\0';
        else
            break;
    }
}

// Calculate capacity from IDENTIFY data
uint64_t calculate_capacity(uint16_t *identify_data) {
    uint64_t sectors = 0;
    
    // Check if the device supports 48-bit LBA
    if (identify_data[83] & (1 << 10)) {
        // Words 100-103 contain the 48-bit address of the maximum LBA
        sectors = ((uint64_t)identify_data[100]) |
                  ((uint64_t)identify_data[101] << 16) |
                  ((uint64_t)identify_data[102] << 32) |
                  ((uint64_t)identify_data[103] << 48);
    } else {
        // Words 60-61 contain the 28-bit address of the maximum LBA
        sectors = ((uint32_t)identify_data[60]) |
                  ((uint32_t)identify_data[61] << 16);
    }
    
    // Convert sectors to bytes (512 bytes per sector)
    return sectors * 512;
}

// Parse supported features from IDENTIFY data
void parse_features(uint16_t *identify_data, char *features, size_t size) {
    int pos = 0;
    int rem = size;
    
    // Check for SMART support
    if (identify_data[82] & (1 << 0)) {
        int written = snprintf(features + pos, rem, "SMART, ");
        pos += written;
        rem -= written;
    }
    
    // Check for 48-bit LBA
    if (identify_data[83] & (1 << 10)) {
        int written = snprintf(features + pos, rem, "48-bit LBA, ");
        pos += written;
        rem -= written;
    }
    
    // Check for NCQ
    if (identify_data[76] & (1 << 8)) {
        int written = snprintf(features + pos, rem, "NCQ, ");
        pos += written;
        rem -= written;
    }
    
    // Check for TRIM (SSD)
    if (identify_data[169] & (1 << 0)) {
        int written = snprintf(features + pos, rem, "TRIM, ");
        pos += written;
        rem -= written;
    }
    
    // Remove trailing comma and space if any features were added
    if (pos > 0) {
        features[pos-2] = '\0';
    } else {
        features[0] = '\0';
    }
}
const char* get_device_type(uint32_t signature) {
    switch (signature) {
        case 0x00000101: return "SATA";
        case 0xEB140101: return "ATAPI";
        case 0xC33C0101: return "SEMB";  // Enclosure management bridge
        case 0x96690101: return "PM";    // Port multiplier
        default: return "Unknown";
    }
}
bool is_port_active(hba_mem_t* hba, int port_num) {
    uint32_t ssts = hba->ports[port_num].ssts;
    uint8_t ipm = (ssts >> 8) & 0x0F;  // Interface Power Management
    uint8_t det = ssts & 0x0F;         // Device Detection
    
    // Check if device is present and active
    return (det == HBA_PORT_DET_PRESENT && ipm == HBA_PORT_IPM_ACTIVE);
}
// Main function to identify disks
void identify_disks(uint32_t abar) {
    hba_mem_t *hba = (hba_mem_t*)abar;
    uint32_t pi = hba->pi;  // Ports Implemented
    printf("\n");
    printf("Scanning AHCI ports for disk drives...\n");
    
    for (int i = 0; i < 32; i++) {
        // Check if port is implemented
        if (!(pi & (1 << i))) {
            continue;
        }
        
        // Check if port has a device
        if (is_port_active(hba, i)) {
            uint32_t signature = hba->ports[i].sig;
            const char* device_type = get_device_type(signature);
            
            printf("Port %d: ", i);
            printf("%s device found", device_type);
			printf("\n");
            // For SATA drives, issue ATA IDENTIFY DEVICE command
            if (strcmp(device_type, "SATA") == 0) {
                // Setup memory structures
                setup_port_memory(hba, i);
                
                // Allocate buffer for IDENTIFY data
                uint16_t *identify_data = aligned_alloc(2, 512);
                memset(identify_data, 0, 512);
                
                if (identify_device(hba, i, identify_data)) {
                    // Parse model name
                    char model[41];
                    parse_model_name(identify_data, model);
                    printf(identify_data);
                    // Parse serial number
                    char serial[21];
                    parse_serial_number(identify_data, serial);
                    
                    // Calculate capacity
                    uint64_t capacity = calculate_capacity(identify_data);
                    
                    // Parse supported features
                    char features[256];
                    parse_features(identify_data, features, sizeof(features));
                    
                    // Print disk information
                    printf("  Model: %s", model);
								printf("\n");

                    printf("  Serial: %s", serial);
								printf("\n");

                    printf("  Capacity: %d bytes (% GB)\n", 
                           capacity, (double)capacity / (1024*1024*1024));
						   			printf("");

                    printf("  Supported features: %s", features);
								printf("\n");

                } else {
                    printf("  Failed to identify device\n");
                }
                
                // Free memory
                free(identify_data);
                cleanup_port_memory(hba, i);
            }
        }
    }
}