#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* External references to kernel functions and variables */
extern void terminal_writestring(const char* data);
extern void terminal_putchar(char c);
extern uint8_t make_color(enum vga_color fg, enum vga_color bg);
extern void terminal_setcolor(uint8_t color);
extern char command_buffer;
extern int command_length;
extern void cmd_help();
extern void cmd_clear();
extern void cmd_hello();
extern void itoa(int n, char s, int b);
extern void* memset(void* s, int c, size_t n);

/* SATA I/O port definitions (these are typical for AHCI controllers) */
#define SATA_HBA_BASE_ADDR          0x400000      // Example base address for SATA controller
#define SATA_HBA_PORT_OFFSET(x)      (0x100 + (x) * 0x80)    // Port-specific registers

/* SATA HBA (Host Bus Adapter) Registers */
#define SATA_HBA_CAP                (SATA_HBA_BASE_ADDR + 0x00)  // Capabilities
#define SATA_HBA_GHC                (SATA_HBA_BASE_ADDR + 0x04)  // Global Host Control
#define SATA_HBA_IS                 (SATA_HBA_BASE_ADDR + 0x08)  // Interrupt Status
#define SATA_HBA_PI                 (SATA_HBA_BASE_ADDR + 0x0C)  // Ports Implemented

/* SATA Port Registers (per-port, example for Port 0) */
#define SATA_PORT_CLB               (SATA_HBA_BASE_ADDR + 0x100)    // Command List Base Address
#define SATA_PORT_CLBU              (SATA_HBA_BASE_ADDR + 0x104)    // Command List Base Address Upper 32-bits
#define SATA_PORT_FB                (SATA_HBA_BASE_ADDR + 0x108)    // FIS Base Address
#define SATA_PORT_FBU               (SATA_HBA_BASE_ADDR + 0x10C)    // FIS Base Address Upper 32-bits
#define SATA_PORT_IS                (SATA_HBA_BASE_ADDR + 0x110)    // Interrupt Status
#define SATA_PORT_IE                (SATA_HBA_BASE_ADDR + 0x114)    // Interrupt Enable
#define SATA_PORT_CMD               (SATA_HBA_BASE_ADDR + 0x118)    // Command and Status
#define SATA_PORT_TFD               (SATA_HBA_BASE_ADDR + 0x120)    // Task File Data
#define SATA_PORT_SIG               (SATA_HBA_BASE_ADDR + 0x124)    // Signature
#define SATA_PORT_SSTS              (SATA_HBA_BASE_ADDR + 0x128)    // SATA Status
#define SATA_PORT_SERR              (SATA_HBA_BASE_ADDR + 0x12C)    // SATA Error
#define SATA_PORT_SACT              (SATA_HBA_BASE_ADDR + 0x130)    // SATA Active
#define SATA_PORT_CI                (SATA_HBA_BASE_ADDR + 0x138)    // Command Issue

/* SATA Commands */
#define SATA_CMD_READ_DMA_EXT       0x25    // 48-bit LBA read
#define SATA_CMD_WRITE_DMA_EXT      0x35    // 48-bit LBA write
#define SATA_CMD_IDENTIFY           0xEC    // Identify device

/* SATA Status and Control Bits */
#define SATA_PORT_CMD_ST            (1 << 0)    // Start DMA engine
#define SATA_PORT_CMD_FRE           (1 << 4)    // FIS Receive Enable
#define SATA_PORT_CMD_FR            (1 << 14)   // FIS Receive Running
#define SATA_PORT_CMD_CR            (1 << 15)   // Command List Running

/* Buffer for disk I/O */
#define SECTOR_SIZE 512

/* Forward declarations */
typedef struct ahci_port ahci_port_t;

/* AHCI Command List Header */
typedef struct ahci_cmd_hdr {
    uint8_t flags;          // [7:5] PRDT Length, [4:0] Command Table Entries
    uint8_t a[3];           // Reserved
    uint32_t command_table_base_low;
    uint32_t command_table_base_high;
    uint32_t data_transfer_length;
    uint32_t p[5];           // Reserved
} __attribute__((packed)) ahci_cmd_hdr_t;

/* AHCI Command Table */
typedef struct ahci_cmd_tbl {
    uint8_t cfis[64];        // Command FIS
    uint8_t aca[32];         // ATA Command Argument
    uint8_t reserved[48];
    uint32_t prdt_entry_count; // Physical Region Descriptor Table Entry Count
    // Physical Region Descriptor Table follows here
} __attribute__((packed)) ahci_cmd_tbl_t;

/* AHCI Physical Region Descriptor Table Entry */
typedef struct ahci_prdt_entry {
    uint32_t data_base_low;
    uint32_t data_base_high;
    uint32_t byte_count;
    uint32_t reserved;
} __attribute__((packed)) ahci_prdt_entry_t;

/* AHCI Port Structure */
typedef struct ahci_port {
    uint32_t clb;           // Command List Base Address
    uint32_t clbu;          // Command List Base Address Upper
    uint32_t fb;            // FIS Base Address
    uint32_t fbu;           // FIS Base Address Upper
    uint32_t is;            // Interrupt Status
    uint32_t ie;            // Interrupt Enable
    uint32_t cmd;           // Command and Status
    uint32_t reserved0;
    uint32_t tfd;           // Task File Data
    uint32_t sig;           // Signature
    uint32_t ssts;          // SATA Status
    uint32_t serr;          // SATA Error
    uint32_t sact;          // SATA Active
    uint32_t ci;            // Command Issue
    uint32_t sntf;          // SATA Notification
    uint32_t reserved1[3];
    uint32_t vendor_specific[4];
} __attribute__((packed)) ahci_port_t;

/* MMIO read/write functions (replace with your actual memory-mapped I/O implementation) */
static inline uint32_t mmio_read32(uintptr_t addr) {
    return *((volatile uint32_t*)(addr));
}

static inline void mmio_write32(uintptr_t addr, uint32_t value) {
    *((volatile uint32_t*)(addr)) = value;
}

static inline uint8_t mmio_read8(uintptr_t addr) {
    return *((volatile uint8_t*)(addr));
}

static inline void mmio_write8(uintptr_t addr, uint8_t value) {
    *((volatile uint8_t*)(addr)) = value;
}

/* Helper function to get the port structure */
static inline ahci_port_t* get_sata_port(int port_num) {
    return (ahci_port_t*)(SATA_HBA_BASE_ADDR + SATA_HBA_PORT_OFFSET(port_num));
}

/* Wait for a bit in a port register to be cleared */
bool sata_wait_port_clear(int port_num, uint32_t reg_offset, uint32_t bitmask, uint32_t timeout_ms) {
    uint32_t timeout = timeout_ms * 1000; // Convert ms to microseconds (assuming a rough loop time)
    ahci_port_t* port = get_sata_port(port_num);
    while ((mmio_read32((uintptr_t)&port->cmd + reg_offset) & bitmask) && timeout--) {
        // Simple busy wait
        for (volatile int i = 0; i < 100; ++i); // Introduce a small delay
    }
    return timeout > 0;
}

/* Wait for SATA port to be ready */
bool sata_wait_ready(int port_num) {
    ahci_port_t* port = get_sata_port(port_num);
    uint32_t timeout = 1000000; // Arbitrary timeout value

    // Wait for command list and FIS receive to be stopped
    while (((port->cmd & (SATA_PORT_CMD_CR | SATA_PORT_CMD_FR)) || (port->ssts & 0xF)) && timeout--) {
        // Simple busy wait
        for (volatile int i = 0; i < 100; ++i);
    }

    return timeout > 0;
}

/* Start the SATA port */
bool sata_port_start(int port_num) {
    ahci_port_t* port = get_sata_port(port_num);

    // Ensure the port is stopped
    port->cmd &= ~(SATA_PORT_CMD_ST | SATA_PORT_CMD_FRE);

    if (!sata_wait_ready(port_num)) {
        terminal_writestring("SATA port failed to stop\n");
        return false;
    }

    // Enable FIS receive and start the port
    port->cmd |= SATA_PORT_CMD_FRE;
    port->cmd |= SATA_PORT_CMD_ST;

    return true;
}

/* Stop the SATA port */
bool sata_port_stop(int port_num) {
    ahci_port_t* port = get_sata_port(port_num);
    port->cmd &= ~(SATA_PORT_CMD_ST | SATA_PORT_CMD_FRE);
    return sata_wait_ready(port_num);
}

/* Find the first active command slot */
int find_cmd_slot(ahci_port_t* port) {
    uint32_t slots = (port->sact | port->ci);
    for (int i = 0; i < 32; i++) {
        if (!(slots & 1)) {
            return i;
        }
        slots >>= 1;
    }
    terminal_writestring("No free command slots!\n");
    return -1;
}

/* Send an ATA command */
bool sata_send_command(int port_num, uint64_t lba, uint16_t sector_count, uint8_t command, uint8_t* buffer, bool write) {
    ahci_port_t* port = get_sata_port(port_num);
    int slot = find_cmd_slot(port);
    if (slot == -1) {
        return false;
    }

    // Allocate memory for command list and command table (in a real OS, you'd use memory allocation)
    volatile ahci_cmd_hdr_t* cmd_list = (volatile ahci_cmd_hdr_t*)0x800000 + (port_num * 32 * sizeof(ahci_cmd_hdr_t)); // Example memory location
    volatile ahci_cmd_tbl_t* cmd_table = (volatile ahci_cmd_tbl_t*)0x801000 + (port_num * 32 * sizeof(ahci_cmd_tbl_t)); // Example memory location

    memset((void*)&cmd_list[slot], 0, sizeof(ahci_cmd_hdr_t));
    memset((void*)&cmd_table[slot], 0, sizeof(ahci_cmd_tbl_t) + sizeof(ahci_prdt_entry_t)); // Assuming 1 PRDT entry for now

    // Command header setup
    cmd_list[slot].flags = (1 << 5); // 1 PRDT entry
    cmd_list[slot].command_table_base_low = (uint32_t)((uintptr_t)&cmd_table[slot]);
    cmd_list[slot].command_table_base_high = (uint32_t)(((uintptr_t)&cmd_table[slot]) >> 32);
    cmd_list[slot].data_transfer_length = sector_count * SECTOR_SIZE;

    // Command table setup
    ahci_cmd_tbl_t* tbl = (ahci_cmd_tbl_t*)&cmd_table[slot];
    memset((void*)tbl, 0, sizeof(ahci_cmd_tbl_t));
    tbl->prdt_entry_count = 1;

    // PRDT entry setup
    ahci_prdt_entry_t* prdt = (ahci_prdt_entry_t*)((uintptr_t)tbl + sizeof(ahci_cmd_tbl_t));
    prdt->data_base_low = (uint32_t)((uintptr_t)buffer);
    prdt->data_base_high = (uint32_t)(((uintptr_t)buffer) >> 32);
    prdt->byte_count = sector_count * SECTOR_SIZE - 1; // Byte count is 0-based
    prdt->reserved = 0;

    // Command FIS setup
    uint8_t* cfis = tbl->cfis;
    cfis[0] = 0x27; // REG_H2D
    cfis[1] = 0x08; // Command
    cfis[2] = command;

    if (command == SATA_CMD_IDENTIFY) {
        cfis[3] = 0;
        cfis[4] = 0;
        cfis[5] = 0;
        cfis[6] = 0;
        cfis[7] = 0;
        cfis[8] = 0;
        cfis[9] = 0;
        cfis[10] = 0;
        cfis[11] = 0;
        cfis[12] = 0;
        cfis[13] = 0;
        cfis[14] = 0;
        cfis[15] = 0;
    } else {
        // LBA setup (48-bit)
        cfis[3] = (uint8_t)lba;
        cfis[4] = (uint8_t)(lba >> 8);
        cfis[5] = (uint8_t)(lba >> 16);
        cfis[7] = (uint8_t)(lba >> 24);
        cfis[8] = (uint8_t)(lba >> 32);
        cfis[9] = (uint8_t)(lba >> 40);

        cfis[6] = (uint8_t)sector_count;
        cfis[10] = (uint8_t)(sector_count >> 8);

        cfis[11] = 0; // Device control
        cfis[14] = 0; // Features
        cfis[15] = 0; // Features high
    }

    // Set write bit if it's a write command
    if (write && command != SATA_CMD_IDENTIFY) {
        cfis[2] |= (1 << 0); // Set Write FUA (Force Unit Access)
    }

    // Issue command
    port->ci = (1 << slot);

    // Wait for command completion (polling)
    while (port->ci & (1 << slot)) {
        if (port->is) {
            terminal_writestring("SATA error occurred!\n");
            terminal_writestring("Port IS: 0x");
            char is_str[9];
            ultoa(port->is, is_str, 16);
            terminal_writestring(is_str);
            terminal_putchar('\n');
            port->is = port->is; // Clear interrupt status
            return false;
        }
    }

    // Check Task File Data for errors
    if (port->tfd & (1 << 0)) { // BSY bit
        terminal_writestring("SATA device busy after command!\n");
        return false;
    }
    if (port->tfd & (1 << 3)) { // ERR bit
        terminal_writestring("SATA command error!\n");
        terminal_writestring("Port TFD: 0x");
        char tfd_str[9];
        ultoa(port->tfd, tfd_str, 16);
        terminal_writestring(tfd_str);
        terminal_putchar('\n');
        return false;
    }

    return true;
}

/* SATA Initialization */
bool sata_init() {
    terminal_writestring("Initializing SATA controller...\n");

    // Check if any ports are implemented
    uint32_t ports_implemented = mmio_read32(SATA_HBA_PI);
    if (ports_implemented == 0) {
        terminal_writestring("No SATA ports detected\n");
        return false;
    }

    // Identify the first available port
    int port = 0;
    while (!(ports_implemented & (1 << port))) {
        port++;
        if (port >= 32) {
            terminal_writestring("No usable SATA ports\n");
            return false;
        }
    }

    terminal_writestring("Detected SATA port: ");
    char port_str[4];
    itoa(port, port_str, 10);
    terminal_writestring(port_str);
    terminal_writestring("\n");

    // Stop the port
    if (!sata_port_stop(port)) {
        return false;
    }

    // Allocate memory for command list and FIS (in a real OS, you'd use memory allocation)
    uintptr_t command_list_base = 0x800000 + (port * 0x1000); // Example memory location
    uintptr_t fis_base = 0x802000 + (port * 0x1000);        // Example memory location

    // Initialize port registers
    ahci_port_t* sata_port = get_sata_port(port);
    sata_port->clb = (uint32_t)command_list_base;
    sata_port->clbu = (uint32_t)(command_list_base >> 32);
    sata_port->fb = (uint32_t)fis_base;
    sata_port->fbu = (uint32_t)(fis_base >> 32);

    // Clear interrupt status
    sata_port->is = sata_port->is;

    // Enable interrupts (optional for polling)
    sata_port->ie = 0xFFFFFFFF;

    // Start the port
    if (!sata_port_start(port)) {
        return false;
    }

    // Send IDENTIFY command
    terminal_writestring("Sending IDENTIFY command...\n");
    uint8_t identify_buffer[SECTOR_SIZE];
    if (sata_send_command(port, 0, 1, SATA_CMD_IDENTIFY, identify_buffer, false)) {
        terminal_writestring("IDENTIFY command successful\n");
        // You can now parse the identify_buffer to get device information
    } else {
        terminal_writestring("IDENTIFY command failed\n");
        return false;
    }

    return true;
}

/* Read a sector from SATA disk */
bool sata_read_sector(uint64_t lba, uint8_t* buffer) {
    terminal_writestring("Reading sector: ");
    char lba_str[21];
    ultoa(lba, lba_str, 10);
    terminal_writestring(lba_str);
    terminal_writestring("\n");

    // Assuming we found the first port during initialization
    int port = 0; // Replace with your actual port detection logic

    return sata_send_command(port, lba, 1, SATA_CMD_READ_DMA_EXT, buffer, false);
}

/* Write a sector to SATA disk */
bool sata_write_sector(uint64_t lba, uint8_t* buffer) {
    terminal_writestring("Writing sector: ");
    char lba_str[21];
    ultoa(lba, lba_str, 10);
    terminal_writestring(lba_str);
    terminal_writestring("\n");

    // Assuming we found the first port during initialization
    int port = 0; // Replace with your actual port detection logic

    return sata_send_command(port, lba, 1, SATA_CMD_WRITE_DMA_EXT, buffer, true);
}

/* --- Dummy FAT32 Implementation (Expanded) --- */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Define some dummy FAT32 structures and constants
#define FAT32_BOOTSECTOR_SIZE 512
#define FAT32_CLUSTER_SIZE    4096 // Example cluster size

typedef struct fat32_bootsector {
    uint8_t jump[3];
    char oem_id[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t fat_count;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t media_type;
    uint16_t sectors_per_fat_16;
    uint16_t sectors_per_track;
    uint16_t head_count;
    uint32_t hidden_sector_count;
    uint32_t total_sectors_32;
    uint32_t sectors_per_fat_32;
    uint16_t fat_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info_sector;
    uint16_t backup_boot_sector;
    uint8_t reserved0[12];
    uint8_t drive_number;
    uint8_t reserved1;
    uint8_t boot_signature;
    char volume_id[4];
    char volume_label[11];
    char fs_type[8];
    uint8_t boot_code[420];
    uint16_t signature; // 0xAA55
} __attribute__((packed)) fat32_bootsector_t;

// Dummy global boot sector
fat32_bootsector_t boot_sector;

bool fat32_init() {
    terminal_writestring("Initializing FAT32...\n");

    // Read the boot sector (sector 0) using the SATA driver
    if (!sata_read_sector(0, (uint8_t*)&boot_sector)) {
        terminal_writestring("Failed to read boot sector\n");
        return false;
    }

    if (boot_sector.signature != 0xAA55) {
        terminal_writestring("Invalid FAT32 signature\n");
        return false;
    }

    terminal_writestring("FAT32 boot sector read successfully\n");

    // Now let's parse some more fields
    terminal_writestring("Bytes per sector: ");
    char bps_str[6];
    itoa(boot_sector.bytes_per_sector, bps_str, 10);
    terminal_writestring(bps_str);
    terminal_putchar('\n');

    terminal_writestring("Sectors per cluster: ");
    char spc_str[4];
    itoa(boot_sector.sectors_per_cluster, spc_str, 10);
    terminal_writestring(spc_str);
    terminal_putchar('\n');

    terminal_writestring("Reserved sector count: ");
    char rsc_str[6];
    itoa(boot_sector.reserved_sector_count, rsc_str, 10);
    terminal_writestring(rsc_str);
    terminal_putchar('\n');

    terminal_writestring("Number of FATs: ");
    char fat_count_str[2];
    itoa(boot_sector.fat_count, fat_count_str, 10);
    terminal_writestring(fat_count_str);
    terminal_putchar('\n');

    terminal_writestring("Sectors per FAT (32-bit): ");
    char spf32_str[11];
    ultoa(boot_sector.sectors_per_fat_32, spf32_str, 10);
    terminal_writestring(spf32_str);
    terminal_putchar('\n');

    terminal_writestring("Root cluster: ");
    char root_cluster_str[11];
    ultoa(boot_sector.root_cluster, root_cluster_str, 10);
    terminal_writestring(root_cluster_str);
    terminal_putchar('\n');

    return true;
}

typedef struct fat32_file_handle {
    // Add necessary fields for file handles (e.g., current cluster, offset, etc.)
    bool in_use;
    uint32_t current_cluster;
    uint32_t offset_in_cluster;
    // ... other file metadata
} fat32_file_handle_t;

#define MAX_FILE_HANDLES 10
fat32_file_handle_t file_handles[MAX_FILE_HANDLES];

void fat32_init_file_handles() {
    terminal_writestring("Initializing FAT32 file handles...\n");
    memset(file_handles, 0, sizeof(file_handles));
}

// Example function to read a cluster
bool fat32_read_cluster(uint32_t cluster_number, uint8_t* buffer) {
    if (cluster_number < 2) {
        terminal_writestring("Invalid cluster number\n");
        return false;
    }

    // Calculate the LBA of the first sector of the cluster
    uint32_t first_data_sector = boot_sector.reserved_sector_count + (boot_sector.fat_count * boot_sector.sectors_per_fat_32);
    uint64_t lba = (uint64_t)first_data_sector + (cluster_number - 2) * boot_sector.sectors_per_cluster;

    // Read all sectors in the cluster
    for (uint8_t i = 0; i < boot_sector.sectors_per_cluster; ++i) {
        if (!sata_read_sector(lba + i, buffer + (i * boot_sector.bytes_per_sector))) {
            terminal_writestring("Error reading cluster\n");
            return false;
        }
    }
    return true;
}

// Example function to write a cluster
bool fat32_write_cluster(uint32_t cluster_number, const uint8_t* buffer) {
    if (cluster_number < 2) {
        terminal_writestring("Invalid cluster number for writing\n");
        return false;
    }

    // Calculate the LBA of the first sector of the cluster
    uint32_t first_data_sector = boot_sector.reserved_sector_count + (boot_sector.fat_count * boot_sector.sectors_per_fat_32);
    uint64_t lba = (uint64_t)first_data_sector + (cluster_number - 2) * boot_sector.sectors_per_cluster;

    // Write all sectors in the cluster
    for (uint8_t i = 0; i < boot_sector.sectors_per_cluster; ++i) {
        if (!sata_write_sector(lba + i, (uint8_t*)buffer + (i * boot_sector.bytes_per_sector))) {
            terminal_writestring("Error writing cluster\n");
            return false;
        }
    }
    return true;
}

/* --- End of Dummy FAT32 Implementation (Expanded) --- */

/* Entry point to initialize filesystem - call this from your kernel_main */
void init_filesystem() {
    // Initialize SATA controller
    if (sata_init()) {
        // Initialize FAT32 filesystem (using SATA as the underlying storage)
        if (fat32_init()) {
            // Initialize file handles
            fat32_init_file_handles();
            terminal_writestring("FAT32 filesystem initialized successfully (using SATA)\n");

            // Example usage: Read the first cluster of the root directory
            uint8_t root_dir_buffer[FAT32_CLUSTER_SIZE];
            if (fat32_read_cluster(boot_sector.root_cluster, root_dir_buffer)) {
                terminal_writestring("Root directory cluster read successfully.\n");
                // You can now parse the root_dir_buffer to find files and directories
            } else {
                terminal_writestring("Failed to read root directory cluster.\n");
            }

            // Example usage: You could potentially write to a cluster as well
            // uint8_t write_buffer[FAT32_CLUSTER_SIZE];
            // memset(write_buffer, 0xAA, FAT32_CLUSTER_SIZE);
            // if (fat32_write_cluster(3, write_buffer)) {
            //     terminal_writestring("Cluster 3 written successfully.\n");
            // } else {
            //     terminal_writestring("Failed to write cluster 3.\n");
            // }

        } else {
            terminal_writestring("Failed to initialize FAT32 filesystem\n");
        }
    } else {
        terminal_writestring("Failed to initialize SATA controller\n");
    }
}