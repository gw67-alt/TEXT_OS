#ifndef AHCI_DRIVER_H
#define AHCI_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

// ========== AHCI Constants ==========

// Device signatures
#define SATA                    0x00000101  // Plain SATA device
#define SATAPI                  0xEB140101  // SATAPI device
#define EnclosureManagement     0xC33C0101  // Enclosure management bridge
#define PortMultiplier          0x96690101  // SATA port multiplier

// ATA Commands
#define ReadDma48               0x25  // READ DMA EXT command
#define Identify                0xEC  // IDENTIFY DEVICE command
#define IdentifyPacket          0xA1  // IDENTIFY PACKET DEVICE command

// FIS Types
#define RegisterHostToDevice    0x27  // Host to device register FIS
#define RegisterDeviceToHost    0x34  // Device to host register FIS
#define DMAActivate             0x39  // DMA activate FIS
#define DMASetup                0x41  // DMA setup FIS
#define Data                    0x46  // Data FIS
#define BIST                    0x58  // BIST FIS
#define PIOSetup                0x5F  // PIO setup FIS
#define DeviceBits              0xA1  // Set device bits FIS

// ATA Status bits
#define Error                   (1U << 0)  // Error bit
#define Ready                   (1U << 6)  // Ready bit
#define Busy                    (1U << 7)  // Busy bit

// Port Command bits
#define Start                   (1U << 0)   // Start command engine
#define FisReceiveEnable        (1U << 4)   // Enable FIS receive
#define CommandListRunning      (1U << 15)  // Command list running
#define FisReceiveRunning       (1U << 14)  // FIS receive running

// Port Interrupt bits
#define DeviceToHostRegister    (1U << 0)   // Device to host register FIS interrupt
#define PioSetup                (1U << 1)   // PIO setup FIS interrupt
#define DmaSetup                (1U << 2)   // DMA setup FIS interrupt
#define SetDeviceBits           (1U << 3)   // Set device bits FIS interrupt
#define DescriptorProcessed     (1U << 5)   // Descriptor processed interrupt
#define PortConnectionChanged   (1U << 6)   // Port connection changed
#define DeviceMechanicalPresence (1U << 7)  // Device mechanical presence
#define TaskFileError           (1U << 30)  // Task file error

/**
 * Host structure for the Physical Region Descriptor Table Entry
 */
typedef struct {
    uint32_t dataBaseAddress;           // Data base address
    uint32_t dataBaseAddressUpper;      // Data base address upper 32 bits
    uint32_t reserved0;                 // Reserved

    uint32_t byteCount:22;              // Byte count, 4M max
    uint32_t reserved1:9;               // Reserved
    uint32_t interruptOnCompletion:1;   // Interrupt on completion
} HBA_PRDT_ENTRY;

/**
 * Host structure for the Command Table
 */
typedef struct {
    // 0x00
    uint8_t commandFIS[64];             // Command FIS
    // 0x40
    uint8_t atapiCommand[16];           // ATAPI command, 12 or 16 bytes
    // 0x50
    uint8_t reserved[48];               // Reserved
    // 0x80
    HBA_PRDT_ENTRY prdtEntry[1];        // Physical region descriptor table entries, 0 ~ 65535
} HBA_CMD_TABLE;

/**
 * Host structure for the Command Header
 */
typedef struct {
    // DW0
    uint8_t commandFISLength:5;         // Command FIS length in DWORDS, 2 ~ 16
    uint8_t atapi:1;                    // ATAPI
    uint8_t write:1;                    // Write, 1: H2D, 0: D2H
    uint8_t prefetchable:1;             // Prefetchable
    
    uint8_t reset:1;                    // Reset
    uint8_t bist:1;                     // BIST
    uint8_t clearBusy:1;                // Clear busy upon R_OK
    uint8_t reserved0:1;                // Reserved
    uint8_t portMultiplier:4;           // Port multiplier port
    
    uint16_t prdtLength;                // Physical region descriptor table length in entries
    
    // DW1
    volatile
    uint32_t prdByteCount;              // Physical region descriptor byte count transferred
    
    // DW2, 3
    uint32_t commandTableBaseAddr;      // Command table descriptor base address
    uint32_t commandTableBaseAddrUpper; // Command table descriptor base address upper 32 bits
    
    // DW4 - 7
    uint32_t reserved1[4];              // Reserved
} HBA_CMD_HEADER;

/**
 * Host-to-Device Register FIS
 */
typedef struct {
    uint8_t fisType;                    // FIS_TYPE_REG_H2D
    
    uint8_t portMultiplier:4;           // Port multiplier
    uint8_t reserved0:3;                // Reserved
    uint8_t commandControl:1;           // 1: Command, 0: Control
    
    uint8_t command;                    // Command register
    uint8_t featureLow;                 // Feature register, 7:0
    
    uint8_t lba0;                       // LBA low register, 7:0
    uint8_t lba1;                       // LBA mid register, 15:8
    uint8_t lba2;                       // LBA high register, 23:16
    uint8_t device;                     // Device register
    
    uint8_t lba3;                       // LBA register, 31:24
    uint8_t lba4;                       // LBA register, 39:32
    uint8_t lba5;                       // LBA register, 47:40
    uint8_t featureHigh;                // Feature register, 15:8
    
    uint8_t countLow;                   // Count register, 7:0
    uint8_t countHigh;                  // Count register, 15:8
    uint8_t icc;                        // Isochronous command completion
    uint8_t control;                    // Control register
    
    uint8_t reserved1[4];               // Reserved
} FIS_REG_H2D;

/**
 * Device-to-Host Register FIS
 */
typedef struct {
    uint8_t fisType;                    // FIS_TYPE_REG_D2H
    
    uint8_t portMultiplier:4;           // Port multiplier
    uint8_t reserved0:2;                // Reserved
    uint8_t interrupt:1;                // Interrupt bit
    uint8_t reserved1:1;                // Reserved
    
    uint8_t status;                     // Status register
    uint8_t error;                      // Error register
    
    uint8_t lba0;                       // LBA low register, 7:0
    uint8_t lba1;                       // LBA mid register, 15:8
    uint8_t lba2;                       // LBA high register, 23:16
    uint8_t device;                     // Device register
    
    uint8_t lba3;                       // LBA register, 31:24
    uint8_t lba4;                       // LBA register, 39:32
    uint8_t lba5;                       // LBA register, 47:40
    uint8_t reserved2;                  // Reserved
    
    uint8_t countLow;                   // Count register, 7:0
    uint8_t countHigh;                  // Count register, 15:8
    uint8_t reserved3[6];               // Reserved
} FIS_REG_D2H;

/**
 * Port structure for a single AHCI port.
 */
typedef struct {
    uint32_t commandListBase;           // 0x00, Command list base address, 1K-byte aligned
    uint32_t commandListBaseUpper;      // 0x04, Command list base address upper 32 bits
    uint32_t fisBase;                   // 0x08, FIS base address, 256-byte aligned
    uint32_t fisBaseUpper;              // 0x0C, FIS base address upper 32 bits
    uint32_t interruptStatus;           // 0x10, Interrupt status
    uint32_t interruptEnable;           // 0x14, Interrupt enable
    uint32_t command;                   // 0x18, Command and status
    uint32_t reserved0;                 // 0x1C, Reserved
    uint32_t taskFileData;              // 0x20, Task file data
    uint32_t signature;                 // 0x24, Signature
    uint32_t sataStatus;                // 0x28, SATA status (SCR0:SStatus)
    uint32_t sataControl;               // 0x2C, SATA control (SCR2:SControl)
    uint32_t sataError;                 // 0x30, SATA error (SCR1:SError)
    uint32_t sataActive;                // 0x34, SATA active (SCR3:SActive)
    uint32_t commandIssue;              // 0x38, Command issue
    uint32_t sataNotification;          // 0x3C, SATA notification (SCR4:SNotification)
    uint32_t fisSwitchControl;          // 0x40, FIS-based switch control
    uint32_t reserved1[11];             // 0x44 ~ 0x6F, Reserved
    uint32_t vendor[4];                 // 0x70 ~ 0x7F, Vendor specific
} HBA_PORT;

/**
 * Main AHCI Host Bus Adapter memory structure.
 */
typedef struct {
    // 0x00 - 0x2B, Generic Host Control
    uint32_t hostCapability;            // 0x00, Host capability
    uint32_t globalHostControl;         // 0x04, Global host control
    uint32_t interruptStatus;           // 0x08, Interrupt status
    uint32_t portsImplemented;          // 0x0C, Ports implemented
    uint32_t version;                   // 0x10, Version
    uint32_t cccControl;                // 0x14, Command completion coalescing control
    uint32_t cccPorts;                  // 0x18, Command completion coalescing ports
    uint32_t enclosureMgmtLocation;     // 0x1C, Enclosure management location
    uint32_t enclosureMgmtControl;      // 0x20, Enclosure management control
    uint32_t hostCapabilityExt;         // 0x24, Host capabilities extended
    uint32_t biosHandoffCtrlSts;        // 0x28, BIOS/OS handoff control and status

    // 0x2C - 0x9F, Reserved
    uint8_t reserved[0xA0-0x2C];

    // 0xA0 - 0xFF, Vendor specific registers
    uint8_t vendor[0x100-0xA0];

    // 0x100 - 0x10FF, Port control registers
    HBA_PORT ports[32];                 // Port control registers
} HBA_MEM;

// ========== Function Declarations ==========

/**
 * Start command engine on the port
 */
void start_command(HBA_PORT *port);

/**
 * Stop command engine on the port
 */
void stop_command(HBA_PORT *port);

/**
 * Find a free command slot in the port
 */
int find_command_slot(HBA_PORT *port, int max_slots);

/**
 * Check the type of device connected to the port
 */
int check_port_type(HBA_PORT *port);

/**
 * Probe all implemented ports to find connected devices
 */
void probe_ports(HBA_MEM *abar);

/**
 * Initialize the port by setting up command list and FIS structures
 */
void initialize_port(HBA_PORT *port, int portno);

/**
 * Read sectors from the drive through the specified port
 */
bool read_sectors(HBA_PORT *port, uint64_t start, uint32_t count, void *buffer);

/**
 * Sends IDENTIFY DEVICE command to the specified port
 */
bool identify_device(HBA_PORT *port, void *buffer);

/**
 * Process IDENTIFY command data to extract device information
 */
void process_identify_data(uint8_t *buffer);

/**
 * Get the number of command slots supported by the HBA
 */
int get_command_slots_count(HBA_MEM *abar);

/**
 * Utility function to convert a string from the device (byte-swapped)
 */
void convert_string(char *str, int len);

#endif /* AHCI_DRIVER_H */