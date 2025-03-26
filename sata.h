#ifndef SATA_INTERFACE_H
#define SATA_INTERFACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

/* Command List structure - 32 bytes each, 32 max per port */
struct ahci_cmd_header {
    uint8_t command_fis_length:5;  // Command FIS length in DWORDS, 2 ~ 16
    uint8_t atapi:1;               // ATAPI command
    uint8_t write:1;               // Write = 1, Read = 0
    uint8_t prefetchable:1;        // Prefetchable

    uint8_t reset:1;               // Reset
    uint8_t bist:1;                // BIST
    uint8_t clear_busy:1;          // Clear busy upon R_OK
    uint8_t reserved0:1;           // Reserved
    uint8_t port_multiplier:4;     // Port multiplier port

    uint16_t prdt_length;          // Physical region descriptor table length in entries

    uint32_t prd_byte_count;       // Physical region descriptor byte count transferred

    uint32_t cmd_table_base_l;     // Command table descriptor base address (low)
    uint32_t cmd_table_base_h;     // Command table descriptor base address (high)

    uint32_t reserved1[4];         // Reserved
} __attribute__((packed));

/* Command Table: FIS structure for H2D (Host to Device) */
struct fis_reg_h2d {
    uint8_t fis_type;              // FIS_TYPE_REG_H2D
    
    uint8_t port_multiplier:4;     // Port multiplier
    uint8_t reserved0:3;           // Reserved
    uint8_t command_control:1;     // 1: Command, 0: Control
    
    uint8_t command;               // Command register
    uint8_t feature_low;           // Feature register (low byte)
    
    uint8_t lba0;                  // LBA low register
    uint8_t lba1;                  // LBA mid register
    uint8_t lba2;                  // LBA high register
    uint8_t device;                // Device register
    
    uint8_t lba3;                  // LBA register, extended
    uint8_t lba4;                  // LBA register, extended
    uint8_t lba5;                  // LBA register, extended
    uint8_t feature_high;          // Feature register (high byte)
    
    uint8_t count_low;             // Count register (low byte)
    uint8_t count_high;            // Count register (high byte)
    uint8_t icc;                   // Isochronous command completion
    uint8_t control;               // Control register
    
    uint8_t reserved1[4];          // Reserved
} __attribute__((packed));

/* Physical Region Descriptor Table (PRDT) entry */
struct ahci_prdt_entry {
    uint32_t data_base_l;          // Data base address (low)
    uint32_t data_base_h;          // Data base address (high)
    uint32_t reserved0;            // Reserved
    
    uint32_t byte_count:22;        // Byte count, 4MB max
    uint32_t reserved1:9;          // Reserved
    uint32_t interrupt_on_completion:1; // Interrupt on completion
} __attribute__((packed));

/* Command Table */
struct ahci_cmd_table {
    // Command FIS - 64 bytes
    uint8_t command_fis[64];
    
    // ATAPI command - 16 bytes
    uint8_t atapi_command[16];
    
    // Reserved - 48 bytes
    uint8_t reserved[48];
    
    // PRDT entries - varying size, up to 65535
    struct ahci_prdt_entry prdt_entries[1];  // Placeholder for at least one entry
} __attribute__((packed));

/* HBA Memory Registers */
struct ahci_hba_port {
    uint32_t clb;      // 0x00, Command List Base Address
    uint32_t clbu;     // 0x04, Command List Base Address Upper 32 bits
    uint32_t fb;       // 0x08, FIS Base Address
    uint32_t fbu;      // 0x0C, FIS Base Address Upper 32 bits
    uint32_t is;       // 0x10, Interrupt Status
    uint32_t ie;       // 0x14, Interrupt Enable
    uint32_t cmd;      // 0x18, Command and Status
    uint32_t reserved0; // 0x1C, Reserved
    uint32_t tfd;      // 0x20, Task File Data
    uint32_t sig;      // 0x24, Signature
    uint32_t ssts;     // 0x28, SATA Status
    uint32_t sctl;     // 0x2C, SATA Control
    uint32_t serr;     // 0x30, SATA Error
    uint32_t sact;     // 0x34, SATA Active
    uint32_t ci;       // 0x38, Command Issue
    uint32_t sntf;     // 0x3C, SATA Notification
    uint32_t fbs;      // 0x40, FIS-Based Switching Control
    
    // Reserved 0x44 - 0x6F
    uint8_t reserved1[0x70 - 0x44];
    
    // Vendor Specific 0x70 - 0x7F
    uint8_t vendor[0x80 - 0x70];
} __attribute__((packed));

struct ahci_hba_mem {
    // Generic Host Control
    uint32_t cap;          // 0x00, Host Capabilities
    uint32_t ghc;          // 0x04, Global Host Control
    uint32_t is;           // 0x08, Interrupt Status
    uint32_t pi;           // 0x0C, Ports Implemented
    uint32_t vs;           // 0x10, Version
    uint32_t ccc_ctl;      // 0x14, Command Completion Coalescing Control
    uint32_t ccc_ports;    // 0x18, Command Completion Coalescing Ports
    uint32_t em_loc;       // 0x1C, Enclosure Management Location
    uint32_t em_ctl;       // 0x20, Enclosure Management Control
    uint32_t cap2;         // 0x24, Host Capabilities Extended
    uint32_t bohc;         // 0x28, BIOS/OS Handoff Control and Status
    
    // Reserved 0x2C - 0x9F
    uint8_t reserved[0xA0 - 0x2C];
    
    // Vendor Specific 0xA0 - 0xFF
    uint8_t vendor[0x100 - 0xA0];
    
    // Port control registers, up to 32 ports
    struct ahci_hba_port ports[32];
} __attribute__((packed));

/* Global variables - extern declarations */
extern struct ahci_hba_mem *hba_mem;  // Pointer to HBA memory mapped registers
extern uint32_t ahci_base;            // Physical base address of AHCI controller

/* Function prototypes */

/**
 * Allocate memory aligned to specified boundary
 * 
 * @param alignment Boundary to align to (should be power of 2)
 * @param size Size of memory to allocate in bytes
 * @return Pointer to aligned memory or NULL on failure
 */
void* ahci_memalign(size_t alignment, size_t size);

/**
 * Find AHCI controller using PCI enumeration
 * 
 * @return Physical address of AHCI controller's registers or 0 if not found
 */
uint32_t find_ahci_controller();

/**
 * Initialize AHCI controller
 * 
 * @param ahci_base_addr Physical address of AHCI controller
 */
void init_ahci(uint32_t ahci_base_addr);

/**
 * Initialize the SATA interface
 * This is the main initialization function to call from kernel
 */
void init_sata_interface();

/**
 * Find SATA drives connected to the AHCI controller
 * 
 * @return Number of SATA drives found
 */
int find_sata_drives();

/**
 * Initialize a SATA port
 * 
 * @param port_num Port number to initialize
 * @return 0 on success, -1 on failure
 */
int init_sata_port(int port_num);

/**
 * Send IDENTIFY DEVICE command to a SATA drive
 * 
 * @param port_num Port number to identify
 * @return 0 on success, -1 on failure
 */
int ahci_identify_device(int port_num);

/**
 * Read sectors from a SATA drive
 * 
 * @param port_num Port number to read from
 * @param start_lba Starting Logical Block Address
 * @param count Number of sectors to read
 * @param buffer Buffer to store read data (must be 512-byte aligned)
 * @return 0 on success, -1 on failure
 */
int ahci_read_sectors(int port_num, uint64_t start_lba, uint16_t count, void *buffer);

/**
 * Write sectors to a SATA drive
 * 
 * @param port_num Port number to write to
 * @param start_lba Starting Logical Block Address
 * @param count Number of sectors to write
 * @param buffer Buffer containing data to write (must be 512-byte aligned)
 * @return 0 on success, -1 on failure
 */
int ahci_write_sectors(int port_num, uint64_t start_lba, uint16_t count, const void *buffer);

/**
 * Reset a SATA port
 * 
 * @param port_num Port number to reset
 * @return 0 on success, -1 on failure
 */
int ahci_reset_port(int port_num);

/**
 * Print status of a SATA port
 * 
 * @param port_num Port number to print status for
 */
void ahci_print_port_status(int port_num);

/**
 * Check and display MBR (Master Boot Record) of a SATA drive
 * 
 * @param port_num Port number to check
 * @return 0 on success, -1 on failure
 */
int ahci_check_mbr(int port_num);

/**
 * Read a file from SATA drive
 * This is a high-level function built on top of ahci_read_sectors
 * 
 * @param port_num Port number to read from
 * @param start_lba Starting Logical Block Address of the file
 * @param size_bytes Size of the file in bytes
 * @param buffer Buffer to store file data
 * @return 0 on success, -1 on failure
 */
int ahci_read_file(int port_num, uint64_t start_lba, uint32_t size_bytes, void *buffer);

/**
 * Write a file to SATA drive
 * This is a high-level function built on top of ahci_write_sectors
 * 
 * @param port_num Port number to write to
 * @param start_lba Starting Logical Block Address for the file
 * @param size_bytes Size of the file in bytes
 * @param buffer Buffer containing file data
 * @return 0 on success, -1 on failure
 */
int ahci_write_file(int port_num, uint64_t start_lba, uint32_t size_bytes, const void *buffer);

/**
 * Stop command processing on a port
 * 
 * @param port Pointer to port structure
 * @return 0 on success, -1 on failure
 */
int ahci_stop_port_cmd(struct ahci_hba_port *port);

/**
 * Start command processing on a port
 * 
 * @param port Pointer to port structure
 * @return 0 on success, -1 on failure
 */
int ahci_start_port_cmd(struct ahci_hba_port *port);

/**
 * Set up command list and FIS structures for a port
 * 
 * @param port_num Port number to rebase
 * @return 0 on success, -1 on failure
 */
int ahci_port_rebase(int port_num);

/**
 * Register SATA commands with the OS shell/command processor
 */
void register_sata_commands();

/**
 * Run a demonstration of SATA functionality
 */
void ahci_demo();

/**
 * List available SATA drives
 */
void cmd_sata_list();

/**
 * Display information about a specific SATA drive
 * 
 * @param port_num Port number of the drive
 */
void cmd_sata_info(int port_num);

/**
 * Display MBR partition table of a specific SATA drive
 * 
 * @param port_num Port number of the drive
 */
void cmd_sata_mbr(int port_num);

#endif /* SATA_INTERFACE_H */