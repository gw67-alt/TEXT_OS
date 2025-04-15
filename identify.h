/*
 * SATA Controller IDENTIFY Command Implementation
 * For bare metal AMD64 environment
 */
#ifndef IDENTIFY_H
#define IDENTIFY_H

#include "kernel.h"           // Assumed to provide basic types like uintXX_t
#include "iostream_wrapper.h" // Assumed to provide a 'cout' like object

 // Port registers offsets - duplicate from main file to avoid dependency issues
 // DEBUG: Ensure these offsets match the AHCI specification (Section 3.3.x)
#define PORT_CLB         0x00  // Command List Base Address (Lower 32 bits)
#define PORT_CLBU        0x04  // Command List Base Address Upper 32 bits
#define PORT_FB          0x08  // FIS Base Address (Lower 32 bits)
#define PORT_FBU         0x0C  // FIS Base Address Upper 32 bits
#define PORT_IS          0x10  // Interrupt Status
#define PORT_IE          0x14  // Interrupt Enable
#define PORT_CMD         0x18  // Command and Status
#define PORT_TFD         0x20  // Task File Data
#define PORT_SIG         0x24  // Signature
#define PORT_SSTS        0x28  // SATA Status (SCR0: SStatus)
#define PORT_SCTL        0x2C  // SATA Control (SCR2: SControl)
#define PORT_SERR        0x30  // SATA Error (SCR1: SError)
#define PORT_SACT        0x34  // SATA Active (SCR3: SActive)
#define PORT_CI          0x38  // Command Issue

// Simple memory access - duplicate from main file
// DEBUG: Ensure 'volatile' is used correctly for MMIO. This looks okay.
// DEBUG: Ensure the addresses used are correct physical addresses mapped appropriately.
inline uint32_t read_mem32(uint64_t addr) {
    return *((volatile uint32_t*)addr);
}

inline void write_mem32(uint64_t addr, uint32_t value) {
    *((volatile uint32_t*)addr) = value;
}
// Simple atoi implementation
int str_int(const char* str) {
    int result = 0;
    int sign = 1;

    // Skip leading whitespace
    while (*str == ' ' || *str == '\t' || *str == '\n') {
        str++;
    }

    // Handle sign
    if (*str == '-') {
        sign = -1;
        str++;
    }
    else if (*str == '+') {
        str++;
    }

    // Convert digits
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }

    return result * sign;
}

// Useful constants for AHCI Port Command and Status Register (PORT_CMD)
// DEBUG: Verify these bit definitions against AHCI spec (Section 3.3.8)
#define HBA_PORT_CMD_ST     0x0001 // Start (bit 0) - Controller processes command list
#define HBA_PORT_CMD_FRE    0x0010 // FIS Receive Enable (bit 4) - HBA can receive FISes
#define HBA_PORT_CMD_FR     0x4000 // FIS Receive Running (bit 14) - Status
#define HBA_PORT_CMD_CR     0x8000 // Command List Running (bit 15) - Status

// FIS types
// DEBUG: Verify these type codes against AHCI spec (Section 4.1)
#define FIS_TYPE_REG_H2D    0x27   // Register FIS - host to device
#define FIS_TYPE_REG_D2H    0x34   // Register FIS - device to host
#define FIS_TYPE_DMA_ACT    0x39   // DMA activate FIS - device to host
#define FIS_TYPE_DMA_SETUP  0x41   // DMA setup FIS - bidirectional
#define FIS_TYPE_DATA       0x46   // Data FIS - bidirectional
#define FIS_TYPE_BIST       0x58   // BIST activate FIS - bidirectional
#define FIS_TYPE_PIO_SETUP  0x5F   // PIO setup FIS - device to host
#define FIS_TYPE_DEV_BITS   0xA1   // Set device bits FIS - device to host

// ATA commands
// DEBUG: Verify command code against ATA/ATAPI Command Set (ACS) spec
#define ATA_CMD_IDENTIFY    0xEC   // IDENTIFY DEVICE

// AHCI command list structure (Command Header)
// DEBUG: Verify structure layout and size against AHCI spec (Section 5.5)
typedef struct {
    // DW0
    uint8_t  cfl : 5;       // Command FIS Length (in DWORDS, 2-16)
    uint8_t  a : 1;         // ATAPI
    uint8_t  w : 1;         // Write (1 = Dev->Host/Read, 0 = Host->Dev/Write)
    uint8_t  p : 1;         // Prefetchable
    uint8_t  r : 1;         // Reset
    uint8_t  b : 1;         // BIST
    uint8_t  c : 1;         // Clear Busy upon R_OK
    uint8_t  reserved0 : 1; // Reserved
    uint16_t prdtl;         // Physical Region Descriptor Table Length (Entries)

    // DW1
    volatile uint32_t prdbc; // Physical Region Descriptor Byte Count (bytes transferred)

    // DW2, DW3
    uint64_t ctba;          // Command Table Base Address (must be 128-byte aligned)

    // DW4-DW7
    uint32_t reserved1[4]; // Reserved
} __attribute__((packed)) hba_cmd_header_t; // Total size: 32 bytes

// FIS structure for H2D Register
// DEBUG: Verify structure layout and size against AHCI spec (Section 4.2.1)
typedef struct {
    // DW0
    uint8_t  fis_type;    // FIS_TYPE_REG_H2D (0x27)
    uint8_t  pmport : 4;    // Port multiplier port
    uint8_t  reserved0 : 3; // Reserved
    uint8_t  c : 1;         // 1: Command, 0: Control
    uint8_t  command;     // Command register (e.g., ATA_CMD_IDENTIFY)
    uint8_t  featurel;    // Feature register, 7:0

    // DW1
    uint8_t  lba0;        // LBA low register, 7:0
    uint8_t  lba1;        // LBA mid register, 15:8
    uint8_t  lba2;        // LBA high register, 23:16
    uint8_t  device;      // Device register

    // DW2
    uint8_t  lba3;        // LBA register, 31:24
    uint8_t  lba4;        // LBA register, 39:32
    uint8_t  lba5;        // LBA register, 47:40
    uint8_t  featureh;    // Feature register, 15:8

    // DW3
    uint8_t  countl;      // Count register, 7:0
    uint8_t  counth;      // Count register, 15:8
    uint8_t  icc;         // Isochronous command completion
    uint8_t  control;     // Control register

    // DW4
    uint8_t  reserved1[4]; // Reserved
} __attribute__((packed)) fis_reg_h2d_t; // Total size: 20 bytes

// Physical Region Descriptor Table Entry
// DEBUG: Verify structure layout and size against AHCI spec (Section 5.3)
typedef struct {
    // DW0, DW1
    uint64_t dba;           // Data Base Address (must be 2-byte aligned)

    // DW2
    uint32_t reserved0;     // Reserved

    // DW3
    uint32_t dbc : 22;      // Byte Count (0-based, max 4MB-1)
    uint32_t reserved1 : 9; // Reserved
    uint32_t i : 1;         // Interrupt on Completion
} __attribute__((packed)) hba_prdt_entry_t; // Total size: 16 bytes

// Command Table Structure
// DEBUG: Verify structure layout and size against AHCI spec (Section 5.4)
// DEBUG: Base address must be 128-byte aligned.
typedef struct {
    uint8_t  cfis[64];      // Command FIS (should match hba_cmd_header_t.cfl)
    uint8_t  acmd[16];      // ATAPI command (if hba_cmd_header_t.a == 1)
    uint8_t  reserved[48];  // Reserved
    // Variable length PRDT (Physical Region Descriptor Table)
    // Using [1] allows flexible use, but the buffer holding this MUST be large enough.
    hba_prdt_entry_t prdt[1]; // PRDT entries (1 to 65535)
} __attribute__((packed)) hba_cmd_tbl_t;


// --- Memory Allocation / Buffers ---
// DEBUG: Ensure these buffers are in memory physically accessible by the AHCI controller (DMA).
// DEBUG: Ensure alignment requirements are met.
// WARNING: These static buffers are shared between IDENTIFY and R/W operations.
//          This is NOT thread-safe and only suitable for simple, sequential tests.
#define IDENTIFY_DATA_SIZE 512

// Command list (array of Command Headers) must be 1KB aligned. Max 32 slots.
// Size: 32 slots * 32 bytes/slot = 1024 bytes.
// Making it static here means it's defined if this header is included.
static hba_cmd_header_t cmd_list_buffer[32] __attribute__((aligned(1024)));

// Received FIS buffer must be 256-byte aligned.
static uint8_t fis_buffer[256] __attribute__((aligned(256)));

// Command Table must be 128-byte aligned.
// DEBUG: Calculate size more precisely: 64 (CFIS) + 16 (ACMD) + 48 (Resvd) + 1 * sizeof(hba_prdt_entry_t) = 128 + 16 = 144 bytes.
// Using a larger buffer is safer. Make it static.
// Needs to be large enough for the maximum PRDT entries expected in one command.
static uint8_t cmd_table_buffer[256] __attribute__((aligned(128)));

// Data buffer for IDENTIFY must be 2-byte aligned (word aligned). Static.
static uint8_t identify_data_buffer[IDENTIFY_DATA_SIZE] __attribute__((aligned(2)));


// Wait for a bit to clear in the specified register
// DEBUG: This is a busy-wait loop. In a real system, use a proper timer or sleep mechanism.
// DEBUG: The inner loop count (100000) is arbitrary and CPU-speed dependent.
// DEBUG: Timeout value might need adjustment based on hardware.
// Making it inline allows it to be defined in the header without linker errors.
inline int wait_for_clear(uint64_t reg_addr, uint32_t mask, int timeout_ms) {
    // DEBUG: Consider adding a check for timeout_ms <= 0
    // Increase multiplier slightly? Depends on target CPU speed.
    for (int i = 0; i < timeout_ms * 10; i++) { // Arbitrary multiplier for delay loop
        if ((read_mem32(reg_addr) & mask) == 0) {
            return 0; // Success
        }
        // Simple delay - replace with platform-specific delay/yield if possible
        for (volatile int j = 0; j < 100000; j++);
    }
    return -1; // Timeout
}

// Function to display IDENTIFY data in a readable format
// DEBUG: Assumes iostream_wrapper can handle basic types and C-style strings.
// DEBUG: Assumes identify_data_buffer contains valid data after successful command.
// Making it inline allows definition in header.
inline void display_identify_data(uint16_t* data) {
    // DEBUG: Check for null pointer?
    if (!data) {
        cout << "ERROR: display_identify_data called with null pointer.\n";
        return;
    }

    char model_number[41];  // Model number is 40 bytes (20 words, offset 27)
    char serial_number[21]; // Serial number is 20 bytes (10 words, offset 10)

    // Extract Model Number (Words 27-46) - Characters are swapped in each word
    for (int i = 0; i < 20; i++) {
        uint16_t word = data[27 + i];
        model_number[i * 2] = (char)(word >> 8);   // High byte first
        model_number[i * 2 + 1] = (char)(word & 0xFF); // Low byte second
    }
    model_number[40] = '\0'; // Null terminate

    // Extract Serial Number (Words 10-19) - Characters are swapped
    for (int i = 0; i < 10; i++) {
        uint16_t word = data[10 + i];
        serial_number[i * 2] = (char)(word >> 8);
        serial_number[i * 2 + 1] = (char)(word & 0xFF);
    }
    serial_number[20] = '\0'; // Null terminate

    // Trim trailing spaces (common in IDENTIFY data)
    for (int i = 39; i >= 0 && model_number[i] == ' '; i--) model_number[i] = '\0';
    for (int i = 19; i >= 0 && serial_number[i] == ' '; i--) serial_number[i] = '\0';

    cout << "IDENTIFY Device Information:\n";
    cout << "--------------------------\n";
    cout << "Model Number:  [" << model_number << "]\n"; // Brackets help see spaces
    cout << "Serial Number: [" << serial_number << "]\n";

    // Capabilities Word (Word 49)
    cout << "Supports LBA:  " << ((data[49] & (1 << 9)) ? "Yes" : "No") << "\n";

    // Command Set/Feature Support Words (Word 82, 83, 84, etc.)
    bool lba48_supported = (data[83] & (1 << 10));
    cout << "Supports LBA48:" << (lba48_supported ? "Yes" : "No") << "\n";

    if (lba48_supported) {
        // LBA48 Max Sectors (Words 100-103)
        uint64_t max_lba48 =
            ((uint64_t)data[100]) |
            ((uint64_t)data[101] << 16) |
            ((uint64_t)data[102] << 32) |
            ((uint64_t)data[103] << 48);

        // DEBUG: The iostream_wrapper limitation workaround.
        //        This might still overflow 'unsigned int' if max_lba48 >= 2^64.
        //        A proper 64-bit print function is better if available.
        unsigned int lba_high = (unsigned int)(max_lba48 >> 32);
        unsigned int lba_low = (unsigned int)(max_lba48 & 0xFFFFFFFF);

        cout << "LBA48 Max Sectors: ";
        if (lba_high > 0) {
            // NOTE: This assumes iostream_wrapper can print unsigned int.
            cout << lba_high << " * 2^32 + ";
        }
        // Attempt to print 64-bit value directly if cout supports it, otherwise use workaround
        // cout << max_lba48 << "\n"; // Preferred if available
        cout << lba_low << " (approx total: " << (unsigned int)(max_lba48 >> 32) << " high, " << (unsigned int)max_lba48 << " low)\n"; // Fallback print

        // Calculate capacity (assuming 512 bytes/sector)
        // DEBUG: Potential overflow if max_lba48 * 512 exceeds uint64_t max. Unlikely with current tech.
        // DEBUG: Integer division truncates. Result is in GiB (1024^3).
        uint64_t total_bytes = max_lba48 * 512;
        unsigned int gib_capacity = (unsigned int)(total_bytes / (1024ULL * 1024 * 1024));
        cout << "Capacity (approx): " << gib_capacity << " GiB\n";

    }
    else if (data[49] & (1 << 9)) { // Check LBA support again
        // LBA28 Max Sectors (Words 60-61)
        unsigned int max_lba28 =
            ((unsigned int)data[60]) |
            ((unsigned int)data[61] << 16);
        cout << "LBA28 Max Sectors: " << max_lba28 << "\n";

        // Calculate capacity
        // DEBUG: Use unsigned long long (uint64_t) for intermediate calculation to avoid overflow.
        uint64_t total_bytes = (uint64_t)max_lba28 * 512;
        unsigned int gib_capacity = (unsigned int)(total_bytes / (1024ULL * 1024 * 1024));
        // Calculate remaining MiB more carefully
        unsigned int mib_remainder = (unsigned int)((total_bytes % (1024ULL * 1024 * 1024)) / (1024 * 1024));
        cout << "Capacity (approx): " << gib_capacity << "." << (mib_remainder * 100 / 1024) << " GiB\n"; // Rough decimal
    }
    else {
        cout << "CHS addressing only (not supported by this display logic).\n";
    }

    // SATA Capabilities (Word 76)
    if (data[76] != 0 && data[76] != 0xFFFF) { // Check if word is valid
        cout << "SATA Gen Supported: ";
        if (data[76] & (1 << 3)) cout << "3 (6.0 Gbps) ";
        if (data[76] & (1 << 2)) cout << "2 (3.0 Gbps) ";
        if (data[76] & (1 << 1)) cout << "1 (1.5 Gbps) ";
        cout << "\n";
    }

    // Features (Words 82-87)
    cout << "Features:\n";
    if (data[82] & (1 << 0))  cout << "  - SMART supported\n";
    if (data[82] & (1 << 1))  cout << "  - Security Mode supported\n";
    if (data[82] & (1 << 3))  cout << "  - Power Management supported\n";
    if (data[82] & (1 << 5))  cout << "  - Write Cache supported\n";
    if (data[82] & (1 << 6))  cout << "  - Look-ahead supported\n";
    // Word 83
    if (data[83] & (1 << 10)) cout << "  - LBA48 supported\n"; // Already checked, but good to list
    if (data[83] & (1 << 12)) cout << "  - AAM supported\n";   // Automatic Acoustic Management
    if (data[83] & (1 << 13)) cout << "  - SET MAX security extension supported\n";
    // Word 84
    if (data[84] & (1 << 0))  cout << "  - Device Configuration Overlay (DCO) supported\n";
    if (data[84] & (1 << 5))  cout << "  - NCQ supported\n"; // Native Command Queuing
    // Word 85
    if (data[85] & (1 << 0))  cout << "  - General Purpose Logging (GPL) supported\n";
    if (data[85] & (1 << 4))  cout << "  - Write DMA Queued supported\n"; // If NCQ
    if (data[85] & (1 << 5))  cout << "  - Read DMA Queued supported\n";  // If NCQ
    // Word 86
    if (data[86] & (1 << 13)) cout << "  - IDLE IMMEDIATE with UNLOAD supported\n";
    // Word 87
    if (data[87] & (1 << 14)) cout << "  - World Wide Name (WWN) supported\n";
    // Word 78 for SATA features
    if (data[78] & (1 << 2))  cout << "  - NCQ Management supported\n";
    if (data[78] & (1 << 8))  cout << "  - Software Settings Preservation supported\n";
    if (data[78] & (1 << 9))  cout << "  - Hardware Feature Control supported\n";
    if (data[78] & (1 << 10)) cout << "  - Device Initiated Power Management (DIPM) supported\n";
}


// Main function to send IDENTIFY DEVICE command
// Leave non-static/non-inline as it's a primary function, but beware of multiple definitions if header included > once.
int send_identify_command(uint64_t ahci_base, int port) {
    // DEBUG: Validate port number against HBA capabilities (e.g., read HBA_CAP register)
    uint64_t port_addr = ahci_base + 0x100 + (port * 0x80);

    // --- Pre-Command Setup & Checks ---

    // 1. Command Slot Check (Using Slot 0 for simplicity)
    int slot = 0; // Use slot 0
    // DEBUG: Should properly check HBA_CAP.NCS for max slots
    const int MAX_CMD_SLOTS = 32; // Assume 32
    if (slot >= MAX_CMD_SLOTS) {
        cout << "ERROR: Invalid command slot selected.\n";
        return -99; // Internal error
    }

    uint32_t slots_in_use = read_mem32(port_addr + PORT_CI) | read_mem32(port_addr + PORT_SACT);
    if ((slots_in_use >> slot) & 1) {
        cout << "ERROR: Command slot " << slot << " is already active (CI=0x" << read_mem32(port_addr + PORT_CI)
            << ", SACT=0x" << read_mem32(port_addr + PORT_SACT) << "). Cannot issue command.\n";
        // DEBUG: In a real driver, find the first free slot.
        return -5;
    }


    // 2. Ensure FIS Receive is Enabled (PORT_CMD.FRE, bit 4)
    uint32_t port_cmd = read_mem32(port_addr + PORT_CMD);
    if (!(port_cmd & HBA_PORT_CMD_FRE)) {
        cout << "WARNING: Port " << port << " FIS Receive (FRE) is not enabled. Attempting to enable.\n";
        write_mem32(port_addr + PORT_CMD, port_cmd | HBA_PORT_CMD_FRE);
        // Small delay might be needed after enabling FRE
        for (volatile int j = 0; j < 10000; j++); // Short delay - use platform delay if available
        port_cmd = read_mem32(port_addr + PORT_CMD); // Re-read
        if (!(port_cmd & HBA_PORT_CMD_FRE)) {
            cout << "ERROR: Failed to enable FIS Receive (FRE) on port " << port << "\n";
            return -2; // Use distinct error code
        }
    }

    // 3. Ensure Port is Started (PORT_CMD.ST, bit 0)
    if (!(port_cmd & HBA_PORT_CMD_ST)) {
        cout << "WARNING: Port " << port << " Start (ST) is not set. Attempting to start.\n";
        write_mem32(port_addr + PORT_CMD, port_cmd | HBA_PORT_CMD_ST);
        // Small delay might be needed after enabling ST
        for (volatile int j = 0; j < 10000; j++); // Short delay
        port_cmd = read_mem32(port_addr + PORT_CMD); // Re-read
        if (!(port_cmd & HBA_PORT_CMD_ST)) {
            cout << "ERROR: Failed to start port " << port << " (ST bit)\n";
            return -3; // Use distinct error code
        }
    }

    // 4. Check device presence and PHY communication (PORT_SSTS)
    //    DET (bits 3:0) should be 3 (Device presence and Phy communication)
    //    IPM (bits 11:8) should be 1 (Active state)
    uint32_t ssts = read_mem32(port_addr + PORT_SSTS);
    uint8_t det = ssts & 0x0F;
    uint8_t ipm = (ssts >> 8) & 0x0F;
    if (det != 3) {
        cout << "ERROR: No device detected or communication not established on port " << port << ". DET = " << (int)det << "\n";
        return -4;
    }
    if (ipm != 1) {
        cout << "WARNING: Port " << port << " is not in active state (IPM = " << (int)ipm << "). May not respond.\n";
        // Attempting to wake? Requires SCTL writes. For now, just warn.
    }


    // --- Setup Command Structures ---
    // DEBUG: Ensure physical addresses are used for DMA.
    // Assuming VIRT_TO_PHYS handles translation if needed, or identity mapping.
    // Define VIRT_TO_PHYS if it's not already defined (e.g., in kernel.h)
#ifndef VIRT_TO_PHYS
// Placeholder: Assumes identity mapping. REPLACE WITH ACTUAL IMPLEMENTATION if MMU active.
#define VIRT_TO_PHYS(addr) ((uint64_t)(addr))
#endif

    uint64_t cmd_list_phys = VIRT_TO_PHYS(cmd_list_buffer);
    uint64_t fis_buffer_phys = VIRT_TO_PHYS(fis_buffer);
    // Using the single shared command table buffer for this slot
    uint64_t cmd_table_phys = VIRT_TO_PHYS(cmd_table_buffer);
    uint64_t identify_data_phys = VIRT_TO_PHYS(identify_data_buffer);


    // Get pointers to the (virtual) buffers for the chosen slot
    hba_cmd_header_t* cmd_header = &cmd_list_buffer[slot];
    // Point to the single shared command table buffer
    hba_cmd_tbl_t* cmd_table = (hba_cmd_tbl_t*)cmd_table_buffer;
    fis_reg_h2d_t* cmdfis = (fis_reg_h2d_t*)cmd_table->cfis;

    // Clear buffers (important!)
    // Clear command header for the specific slot
    volatile uint8_t* hdr_ptr = (uint8_t*)cmd_header;
    for (int i = 0; i < sizeof(hba_cmd_header_t); i++) hdr_ptr[i] = 0;
    // Clear the shared command table buffer
    volatile uint8_t* tbl_ptr = (uint8_t*)cmd_table;
    for (int i = 0; i < sizeof(cmd_table_buffer); i++) tbl_ptr[i] = 0;
    // Clear the data buffer before the read
    volatile uint8_t* data_ptr = (uint8_t*)identify_data_buffer;
    for (int i = 0; i < IDENTIFY_DATA_SIZE; i++) data_ptr[i] = 0;


    // Configure the command header (for slot 'slot')
    cmd_header->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t); // 5 DWORDs
    cmd_header->a = 0;   // Not ATAPI
    cmd_header->w = 0;   // Write = 0 for H2D FIS. Data direction for IDENTIFY is D2H (Read), but the FIS itself is H2D. The 'W' bit in cmd header likely refers to data xfer dir: 1=D2H, 0=H2D. So should be 1? Check Spec 5.5: W=1 "when the command will write data TO MEMORY". So W=1 for IDENTIFY. Let's try W=1.
    // Correction based on common interpretation & prev R/W code: W=1 means D2H data (READ).
    cmd_header->w = 1;
    cmd_header->p = 1;   // Prefetchable = 1 is okay if PRDT is used.
    cmd_header->prdtl = 1; // We are using 1 PRDT entry.
    // cmd_header->prdbc is volatile, cleared by HBA, updated on completion.
    cmd_header->ctba = cmd_table_phys; // Physical address of the command table

    // Configure the command table
    // Setup PRDT entry 0
    cmd_table->prdt[0].dba = identify_data_phys;       // Physical address of data buffer
    cmd_table->prdt[0].dbc = IDENTIFY_DATA_SIZE - 1; // Byte count (0-based). 511 for 512 bytes.
    cmd_table->prdt[0].i = 1; // Interrupt on completion (useful even if polling CI)

    // Setup the Command FIS (H2D Register FIS) within the command table
    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1; // This is a command FIS
    cmdfis->command = ATA_CMD_IDENTIFY;
    cmdfis->device = 0x40; // LBA mode (bit 6=1), DEV bit (4) = 0. Trying 0x40 instead of 0xE0 or 0.
    // LBA and Feature registers are not used for IDENTIFY DEVICE
    cmdfis->lba0 = 0; cmdfis->lba1 = 0; cmdfis->lba2 = 0;
    cmdfis->lba3 = 0; cmdfis->lba4 = 0; cmdfis->lba5 = 0;
    cmdfis->featurel = 0; cmdfis->featureh = 0;
    // Count should be 0 for IDENTIFY (implies 1 sector in some contexts, ignored here)
    cmdfis->countl = 0;
    cmdfis->counth = 0;
    cmdfis->control = 0; // Control register (usually 0)

    // --- Program HBA Registers ---

    // 6. Set the command list base and FIS base addresses
    //    *** THIS IS CRITICAL - DO NOT SKIP ***
    //    Only needs to be done once if buffers don't change, but doing it
    //    here ensures correctness for this command.
    write_mem32(port_addr + PORT_CLB, (uint32_t)cmd_list_phys);
    write_mem32(port_addr + PORT_CLBU, (uint32_t)(cmd_list_phys >> 32));

    write_mem32(port_addr + PORT_FB, (uint32_t)fis_buffer_phys);
    write_mem32(port_addr + PORT_FBU, (uint32_t)(fis_buffer_phys >> 32));

    // 7. Clear any pending interrupt status bits for the port
    write_mem32(port_addr + PORT_IS, 0xFFFFFFFF); // Write 1s to clear bits

    // 8. Clear any SATA error bits
    uint32_t serr = read_mem32(port_addr + PORT_SERR);
    if (serr != 0) {
        write_mem32(port_addr + PORT_SERR, serr); // Write 1s to clear bits
    }

    // 9. Wait for the port to be idle (not busy - TFD.BSY=0, TFD.DRQ=0)
    //    BSY (bit 7), DRQ (bit 3)
    if (wait_for_clear(port_addr + PORT_TFD, (1 << 7) | (1 << 3), 1000) < 0) { // Timeout 1 second
        cout << "ERROR: Port is busy before command issue (TFD=0x" << read_mem32(port_addr + PORT_TFD) << "). Cannot send command.\n";
        // DEBUG: Might need a port reset here.
        return -6;
    }


    // --- Issue Command and Wait ---

    // 10. Issue the command by setting the corresponding bit in PORT_CI
    write_mem32(port_addr + PORT_CI, (1 << slot)); // Issue command on slot 'slot'

    cout << "Command issued on slot " << slot << ", waiting for completion...\n";

    // 11. Wait for command completion by polling PORT_CI bit for the slot to clear
    //     Timeout needs to be generous for IDENTIFY command (can take a few seconds).
    if (wait_for_clear(port_addr + PORT_CI, (1 << slot), 5000) < 0) { // Timeout 5 seconds
        cout << "ERROR: Command timed out waiting for CI bit " << slot << " to clear. CI=0x" << read_mem32(port_addr + PORT_CI) << "\n";
        // DEBUG: Consider attempting a port reset or controller reset on timeout.
        return -7;
    }


    // --- Check Results ---

    // 12. Check for errors in the Task File Data register (PORT_TFD)
    //     ERR (bit 0) or DF (bit 5) indicate an error.
    uint32_t tfd = read_mem32(port_addr + PORT_TFD);
    if (tfd & ((1 << 0) | (1 << 5))) { // Check ERR or DF bits
        cout << "ERROR: IDENTIFY command failed. TFD status: ";
        // Print status bits (based on ATA spec)
        if (tfd & 0x80) cout << "BSY ";  // Busy
        if (tfd & 0x40) cout << "DRDY "; // Device Ready
        if (tfd & 0x20) cout << "DF ";   // Device Fault
        if (tfd & 0x10) cout << "DSC ";  // Device Seek Complete (Obsolete)
        if (tfd & 0x08) cout << "DRQ ";  // Data Request
        if (tfd & 0x04) cout << "CORR "; // Corrected Data (Obsolete)
        if (tfd & 0x02) cout << "IDX ";  // Index (Obsolete)
        if (tfd & 0x01) cout << "ERR ";  // Error
        cout << "(Raw TFD: 0x" << tfd << ")\n";

        // Check SError register for more details if ERR bit is set
        if (tfd & 0x01) {
            uint32_t serr_val = read_mem32(port_addr + PORT_SERR);
            cout << "       SERR: 0x" << serr_val << "\n";
            // Decode SERR bits here based on AHCI spec 3.3.11 if needed
            if (serr_val != 0) write_mem32(port_addr + PORT_SERR, serr_val); // Clear SError
        }
        return -8;
    }

    // 13. Check how many bytes were actually transferred (optional but good practice)
    uint32_t bytes_transferred = cmd_header->prdbc;
    if (bytes_transferred != IDENTIFY_DATA_SIZE) {
        cout << "WARNING: Expected " << IDENTIFY_DATA_SIZE << " bytes, but received " << bytes_transferred << " bytes.\n";
        // Data might still be usable, but it's unexpected. Treat as success for now.
    }

    // 14. Process the IDENTIFY data if successful
    cout << "\nIDENTIFY command completed successfully.\n";
    display_identify_data((uint16_t*)identify_data_buffer); // Cast the byte buffer to word pointer

    return 0; // Success
}


// ========================================================================== //
// == START OF APPENDED READ/WRITE DEBUG MODULE                            == //
// ========================================================================== //
// WARNING: Implementation code below this line. See warning at top of file //
//          about including implementation in headers.                      //
// ========================================================================== //

// --- Configuration ---
// TODO: Define AHCI_BASE_ADDRESS appropriately where this header is included,
//       or pass it into the test functions. Example placeholder:
// #define AHCI_BASE_ADDRESS 0xF0000000 // Example address - FIND THE CORRECT ONE!

// Maximum number of sectors per single READ/WRITE DMA EXT command
// Limited by PRDT entry size (4MB) & count field (16-bit).
#define MAX_SECTORS_PER_RW_COMMAND 128 // Keep reasonable for static buffer

// --- ATA Commands (ACS Spec) ---
#define ATA_CMD_READ_DMA_EXT  0x25 // LBA48 Read DMA
#define ATA_CMD_WRITE_DMA_EXT 0x35 // LBA48 Write DMA

// --- Static Buffers (Shared between Identify, Read, Write for simplicity) ---
// Data buffer for Read/Write testing (needs correct alignment for PRDT dba - word aligned is fine)
// Size based on max sectors * 512 bytes/sector. Static allocation.
static uint8_t rw_data_buffer[MAX_SECTORS_PER_RW_COMMAND * 512] __attribute__((aligned(2)));


// --- Synchronization Placeholder ---
// TODO: Implement proper locking if used in a multitasking environment
inline void acquire_lock(int port) { /* Placeholder */ }
inline void release_lock(int port) { /* Placeholder */ }

// --- Delay Placeholder ---
// TODO: Implement a more reliable delay mechanism than busy-waiting loops if possible
// Making inline to allow definition in header.
inline void delay_ms(int ms) {
    for (int i = 0; i < ms; ++i) {
        // Crude busy loop - adjust multiplier based on CPU speed / testing
        for (volatile int j = 0; j < 100000; ++j);
    }
}


// --- Core AHCI Command Function ---

/**
 * @brief Finds the first available command slot on the port.
 * @param port_addr Base MMIO address of the AHCI port.
 * @param max_slots Maximum number of command slots supported (usually from HBA_CAP.NCS).
 * @return Command slot number (0 to max_slots-1) if found, -1 otherwise.
 */
 // Make static to limit scope to this compilation unit (if header included multiple times)
static int find_free_command_slot(uint64_t port_addr, int max_slots) {
    uint32_t slots_in_use = read_mem32(port_addr + PORT_CI) | read_mem32(port_addr + PORT_SACT);
    for (int i = 0; i < max_slots; i++) {
        if (!((slots_in_use >> i) & 1)) {
            return i; // Found a free slot
        }
    }
    return -1; // No free slots
}


/**
 * @brief Sends a generic ATA DMA command (like READ/WRITE DMA EXT) via AHCI.
 *
 * @param ahci_base Base address of the AHCI controller.
 * @param port Port number.
 * @param ata_command The ATA command code (e.g., 0x25, 0x35).
 * @param lba Starting Logical Block Address (64-bit).
 * @param sector_count Number of sectors to transfer (1 to 65535).
 * @param data_buffer_virt Virtual address of the data buffer. MUST be physically contiguous.
 * @param is_write True if this is a write command (Host to Device), False for read (Device to Host).
 * @return 0 on success, negative error code on failure.
 */
 // Make static to limit scope if header included multiple times.
static int send_ata_dma_command(uint64_t ahci_base, int port, uint8_t ata_command,
    uint64_t lba, uint16_t sector_count,
    void* data_buffer_virt, bool is_write)
{
    // --- Input Validation ---
    if (sector_count == 0 || sector_count > MAX_SECTORS_PER_RW_COMMAND) {
        cout << "ERROR: Invalid sector count (" << (int)sector_count
            << ") for port " << port << ". Max allowed: " << MAX_SECTORS_PER_RW_COMMAND << "\n";
        return -10; // Choose a distinct error code
    }
    if (!data_buffer_virt) {
        cout << "ERROR: Data buffer is null for port " << port << "\n";
        return -11;
    }
    if (sector_count * 512 > sizeof(rw_data_buffer) && data_buffer_virt == rw_data_buffer) {
        cout << "ERROR: Requested R/W size exceeds static rw_data_buffer size.\n";
        return -12;
    }


    uint64_t port_addr = ahci_base + 0x100 + (port * 0x80);
    // Assuming HBA supports 32 command slots (check HBA_CAP.NCS if necessary)
    const int MAX_CMD_SLOTS = 32;

    acquire_lock(port); // Lock the port if multi-tasking

    // --- Find a free command slot ---
    int slot = find_free_command_slot(port_addr, MAX_CMD_SLOTS);
    if (slot < 0) {
        cout << "ERROR: No free command slots available on port " << port << "\n";
        release_lock(port);
        return -5; // Reusing error code from identify.h
    }

    // --- Get Physical Addresses (CRITICAL for DMA) ---
    // Assume VIRT_TO_PHYS is defined (e.g. via macro above or in kernel.h)
    uint64_t cmd_list_phys = VIRT_TO_PHYS(cmd_list_buffer);
    uint64_t fis_buffer_phys = VIRT_TO_PHYS(fis_buffer);
    // Using the single shared command table buffer.
    uint64_t cmd_table_phys = VIRT_TO_PHYS(cmd_table_buffer);
    uint64_t data_buffer_phys = VIRT_TO_PHYS(data_buffer_virt); // Translate the specific buffer passed in
    uint32_t data_buffer_size = (uint32_t)sector_count * 512;


    // --- Get pointers to the command structures for the chosen slot ---
    hba_cmd_header_t* cmd_header = &cmd_list_buffer[slot];
    hba_cmd_tbl_t* cmd_table = (hba_cmd_tbl_t*)cmd_table_buffer; // Using shared table
    fis_reg_h2d_t* cmdfis = (fis_reg_h2d_t*)cmd_table->cfis;


    // --- Pre-Command Checks (similar to IDENTIFY) ---
    uint32_t port_cmd = read_mem32(port_addr + PORT_CMD);
    if (!(port_cmd & HBA_PORT_CMD_FRE)) { // FIS Receive Enable
        cout << "ERROR: Port " << port << " FRE not enabled.\n"; release_lock(port); return -2;
    }
    if (!(port_cmd & HBA_PORT_CMD_ST)) { // Start
        cout << "ERROR: Port " << port << " ST not set.\n"; release_lock(port); return -3;
    }
    uint32_t ssts = read_mem32(port_addr + PORT_SSTS);
    if ((ssts & 0x0F) != 3) { // Device Detection
        cout << "ERROR: No device detected (DET!=3) on port " << port << ".\n"; release_lock(port); return -4;
    }
    if (((ssts >> 8) & 0x0F) != 1) { // Interface Power Management (IPM)
        cout << "WARNING: Port " << port << " not in active state (IPM!=1).\n";
    }


    // --- Setup Command Structures ---

    // Clear command header for the specific slot
    volatile uint8_t* hdr_ptr = (uint8_t*)cmd_header;
    for (int i = 0; i < sizeof(hba_cmd_header_t); i++) hdr_ptr[i] = 0;

    // Clear the shared command table buffer (important!)
    volatile uint8_t* tbl_ptr = (uint8_t*)cmd_table;
    for (int i = 0; i < sizeof(cmd_table_buffer); i++) tbl_ptr[i] = 0;


    // Configure the command header (slot 'slot')
    cmd_header->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t); // 5 DWORDs for Register H2D FIS
    cmd_header->a = 0; // ATA command
    // W (Write bit): 1 = Device-to-Host (Read), 0 = Host-to-Device (Write)
    cmd_header->w = is_write ? 0 : 1;
    cmd_header->p = 1; // Prefetchable is okay for DMA with PRDT
    cmd_header->prdtl = 1; // Using 1 PRDT entry for this transfer
    // cmd_header->prdbc is volatile, cleared/written by HBA
    cmd_header->ctba = cmd_table_phys; // Physical address of the command table


    // Configure the command table (pointed to by cmd_header->ctba)
    // Setup PRDT entry 0
    cmd_table->prdt[0].dba = data_buffer_phys;       // Physical address of data buffer
    cmd_table->prdt[0].dbc = data_buffer_size - 1;   // Byte count (0-based)
    cmd_table->prdt[0].i = 1;                       // Interrupt on completion

    // Setup the Command FIS (H2D Register FIS) within the command table
    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1; // This is a command FIS
    cmdfis->command = ata_command; // The specific ATA command (e.g., READ/WRITE DMA EXT)

    // LBA48 Addressing
    cmdfis->lba0 = (uint8_t)(lba & 0xFF);
    cmdfis->lba1 = (uint8_t)((lba >> 8) & 0xFF);
    cmdfis->lba2 = (uint8_t)((lba >> 16) & 0xFF);
    cmdfis->device = 1 << 6; // LBA mode (bit 6 = 1), master device (bit 4 = 0) -> 0x40.
    // cmdfis->device = 0x40; // Set LBA mode bit

    cmdfis->lba3 = (uint8_t)((lba >> 24) & 0xFF);
    cmdfis->lba4 = (uint8_t)((lba >> 32) & 0xFF);
    cmdfis->lba5 = (uint8_t)((lba >> 40) & 0xFF);

    // Sector Count
    cmdfis->countl = (uint8_t)(sector_count & 0xFF);
    cmdfis->counth = (uint8_t)((sector_count >> 8) & 0xFF);

    // Features and Control are typically 0 for basic DMA R/W
    cmdfis->featurel = 0;
    cmdfis->featureh = 0;
    cmdfis->control = 0;


    // --- Program HBA Registers ---
    // Re-setting CLB/FB here ensures they point to our static buffers for this command.
    write_mem32(port_addr + PORT_CLB, (uint32_t)cmd_list_phys);
    write_mem32(port_addr + PORT_CLBU, (uint32_t)(cmd_list_phys >> 32));
    write_mem32(port_addr + PORT_FB, (uint32_t)fis_buffer_phys);
    write_mem32(port_addr + PORT_FBU, (uint32_t)(fis_buffer_phys >> 32));

    // Clear status/error registers before issuing command
    write_mem32(port_addr + PORT_IS, 0xFFFFFFFF);  // Clear interrupt status bits
    uint32_t serr = read_mem32(port_addr + PORT_SERR);
    if (serr != 0) {
        write_mem32(port_addr + PORT_SERR, serr); // Clear SATA error bits
    }

    // Wait for the port to be idle (not busy/DRQ) before issuing
    if (wait_for_clear(port_addr + PORT_TFD, (1 << 7) | (1 << 3), 1000) < 0) { // Timeout 1 sec
        cout << "ERROR: Port " << port << " is busy before command issue (TFD=0x"
            << read_mem32(port_addr + PORT_TFD) << ").\n";
        release_lock(port);
        return -6; // Reusing error code
    }

    // --- Issue Command ---
    write_mem32(port_addr + PORT_CI, (1 << slot)); // Issue command on our free slot


    // --- Wait for Completion ---
    // Timeout needs to be appropriate for disk R/W (can be longer than IDENTIFY)
    if (wait_for_clear(port_addr + PORT_CI, (1 << slot), 10000) < 0) { // Timeout 10 seconds
        cout << "ERROR: Command (Slot " << slot << ") timed out on port " << port << ". CI=0x" << read_mem32(port_addr + PORT_CI) << "\n";
        // DEBUG: Should attempt a port reset here in a real driver.
        release_lock(port);
        return -7; // Reusing error code
    }


    // --- Check Results ---
    // Check Task File Data for errors (ERR bit 0, DF bit 5)
    uint32_t tfd = read_mem32(port_addr + PORT_TFD);
    if (tfd & ((1 << 0) | (1 << 5))) {
        cout << "ERROR: Disk R/W command failed on port " << port << ". TFD status: 0x" << tfd << "\n";
        // Optionally print detailed TFD bits like in send_identify_command
        uint32_t serr_val = read_mem32(port_addr + PORT_SERR); // Check SError too
        if (serr_val != 0) {
            cout << "       SERR: 0x" << serr_val << "\n";
            write_mem32(port_addr + PORT_SERR, serr_val); // Clear SError
        }
        release_lock(port);
        return -8; // Reusing error code
    }

    // Check bytes transferred (optional but good sanity check)
    // Must read this *after* checking for errors and *before* potentially issuing another command
    // as cmd_header->prdbc might get cleared by HBA for next command.
    uint32_t bytes_transferred = cmd_header->prdbc; // Read the volatile field
    if (bytes_transferred != data_buffer_size) {
        cout << "WARNING: Port " << port << " - Expected " << data_buffer_size
            << " bytes, but transferred " << bytes_transferred << " bytes.\n";
        // This might indicate partial success or an issue. Treat as error? For now, just warn.
        // Might still return success if no TFD error occurred.
    }

    release_lock(port); // Release lock before returning
    return 0; // Success
}


// --- Public Read/Write Functions ---

/**
 * @brief Reads sectors from an AHCI device using DMA.
 * @param ahci_base Base address of the AHCI controller.
 * @param port Port number.
 * @param lba Starting LBA address.
 * @param sector_count Number of sectors to read (max MAX_SECTORS_PER_RW_COMMAND).
 * @param buffer Virtual address of the buffer to read into (Must be >= sector_count * 512 bytes).
 * @return 0 on success, negative error code on failure.
 */
 // Leave non-static/non-inline as primary API, but beware multiple definition errors if header included > once.
int read_sectors_ahci(uint64_t ahci_base, int port, uint64_t lba, uint16_t sector_count, void* buffer) {
    if (sector_count > MAX_SECTORS_PER_RW_COMMAND) {
        cout << "ERROR: Read request exceeds MAX_SECTORS_PER_RW_COMMAND.\n";
        return -10;
    }
    // Ensure LBA48 is supported (could check identify_data_buffer if available)
    // Add check here if needed
    return send_ata_dma_command(ahci_base, port, ATA_CMD_READ_DMA_EXT, lba, sector_count, buffer, false);
}

/**
 * @brief Writes sectors to an AHCI device using DMA.
 * @param ahci_base Base address of the AHCI controller.
 * @param port Port number.
 * @param lba Starting LBA address.
 * @param sector_count Number of sectors to write (max MAX_SECTORS_PER_RW_COMMAND).
 * @param buffer Virtual address of the buffer containing data to write (Must be >= sector_count * 512 bytes).
 * @return 0 on success, negative error code on failure.
 */
 // Leave non-static/non-inline as primary API, but beware multiple definition errors if header included > once.
int write_sectors_ahci(uint64_t ahci_base, int port, uint64_t lba, uint16_t sector_count, void* buffer) {
    if (sector_count > MAX_SECTORS_PER_RW_COMMAND) {
        cout << "ERROR: Write request exceeds MAX_SECTORS_PER_RW_COMMAND.\n";
        return -10;
    }
    // Ensure LBA48 is supported (could check identify_data_buffer if available)
    // Add check here if needed
    return send_ata_dma_command(ahci_base, port, ATA_CMD_WRITE_DMA_EXT, lba, sector_count, buffer, true);
}


// --- Test Function ---

/**
 * @brief Performs IDENTIFY, Read, Write, Read, Verify sequence on a port.
 * Uses the static rw_data_buffer for transfers.
 * @param ahci_base Base address of the AHCI controller.
 * @param port Port number to test.
 */
 // Leave non-static/non-inline as primary API, but beware multiple definition errors if header included > once.
void test_ahci_rw(uint64_t ahci_base, int port) {
    cout << "\n--- Testing AHCI Port " << port << " R/W Operations ---\n";

    // 1. Send IDENTIFY Command (using the function already defined)
    int identify_status = send_identify_command(ahci_base, port);
    if (identify_status != 0) {
        cout << "ERROR: IDENTIFY command failed for port " << port << " with status " << identify_status << ". Aborting R/W test.\n";
        return;
    }

    // Check if LBA and LBA48 are supported before proceeding
    // Assumes identify_data_buffer holds the latest IDENTIFY result. This is fragile!
    // A better approach would be to store device capabilities per port.
    uint16_t* identify_data_ptr = (uint16_t*)identify_data_buffer; // Use the static buffer
    if (!(identify_data_ptr[49] & (1 << 9))) {
        cout << "INFO: Device on port " << port << " does not support LBA. Skipping R/W test.\n";
        return;
    }
    bool lba48_supported = (identify_data_ptr[83] & (1 << 10));
    if (!lba48_supported) {
        cout << "INFO: Device on port " << port << " does not support LBA48. READ/WRITE DMA EXT may fail. Skipping LBA48 test.\n";
        // TODO: Could implement READ/WRITE DMA (28-bit LBA) commands as fallback
        return;
    }

    // --- Read/Write Test Parameters ---
    uint64_t test_lba = 0; // Test on the first sector (LBA 0) - EXTREMELY DANGEROUS!
    uint16_t test_sector_count = 1; // Test with a single sector

    cout << "Proceeding.\n";


    // --- Stage 2: Prepare and Write test data ---
    cout << "\nStage 2: Writing to LBA " << (unsigned int)test_lba << "...\n";

    cout << "";

    // Option 1: Fill with a single string at the beginning
    char LBA_TEST[512];
    

    // Add option to send identify commands
    cout << "\nDo you want to (r)ead or (w)rite?: ";
    char response[2];
    cin >> response;

    if (response[0] == 'r') {
        cout << "Enter LBA number: ";

        cin >> LBA_TEST;
        test_lba = str_int(LBA_TEST);

        // --- Stage 3: Read back data ---
        cout << "\nStage 3: Reading back data from LBA " << (unsigned int)test_lba << "...\n";
        // Clear buffer with different pattern before reading back
        for (int i = 0; i < test_sector_count * 512; ++i) rw_data_buffer[i] = 0xFF;

        int read_status2 = read_sectors_ahci(ahci_base, port, test_lba, test_sector_count, rw_data_buffer);
        if (read_status2 != 0) {
            cout << "ERROR: Stage 3 read back failed with status " << read_status2 << ". Cannot verify write.\n";
            return;
        }
        cout << "Stage 3 read back succeeded. 512 bytes: ";
        for (int k = 0; k < 512; ++k) {
            char c = rw_data_buffer[k];
            // Check if the character is printable
            if (c >= 32 && c <= 126) {
                cout << c;
            }
            else {
                // For non-printable characters, print a placeholder
                cout << "· ";
            }
        }
    }
        cout << "...\n";
        if (response[0] == 'w') {
            cout << "Enter LBA number: ";

            cin >> LBA_TEST;
            test_lba = str_int(LBA_TEST);


            char str[512];

            cout << "Enter data: ";
            cin >> str;
            int str_len = strlen(str);

            // Copy the string to the beginning of the buffer
            memcpy(rw_data_buffer, str, str_len);

            // Fill the rest with zeros or another pattern
            for (int i = str_len; i < test_sector_count * 512; ++i) {
                rw_data_buffer[i] = ' '; // or any other value
            }
            int write_status = write_sectors_ahci(ahci_base, port, test_lba, test_sector_count, rw_data_buffer);
            if (write_status != 0) {
                cout << "ERROR: Stage 2 write failed with status " << write_status << ". Aborting test. SECTOR MAY BE CORRUPTED.\n";
                // Attempt to restore original data? Risky if write failed partially.
                return;
            }
            cout << "Stage 2 write command succeeded.\n";
        }



    cout << "\n--- Test Finished for Port " << port << " ---\n";
}

#endif // IDENTIFY_H