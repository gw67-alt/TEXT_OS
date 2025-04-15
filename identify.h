/*

 * SATA Controller IDENTIFY Command Implementation

 * For bare metal AMD64 environment

 */

#ifndef IDENTIFY_H

#define IDENTIFY_H



#include "kernel.h" // Assumed to provide basic types like uintXX_t

#include "iostream_wrapper.h" // Assumed to provide a 'cout' like object



 // Port registers offsets - duplicate from main file to avoid dependency issues

 // DEBUG: Ensure these offsets match the AHCI specification (Section 3.3.x)

#define PORT_CLB        0x00  // Command List Base Address (Lower 32 bits)

#define PORT_CLBU       0x04  // Command List Base Address Upper 32 bits

#define PORT_FB         0x08  // FIS Base Address (Lower 32 bits)

#define PORT_FBU        0x0C  // FIS Base Address Upper 32 bits

#define PORT_IS         0x10  // Interrupt Status

#define PORT_IE         0x14  // Interrupt Enable

#define PORT_CMD        0x18  // Command and Status

#define PORT_TFD        0x20  // Task File Data

#define PORT_SIG        0x24  // Signature

#define PORT_SSTS       0x28  // SATA Status (SCR0: SStatus)

#define PORT_SCTL       0x2C  // SATA Control (SCR2: SControl)

#define PORT_SERR       0x30  // SATA Error (SCR1: SError)

#define PORT_SACT       0x34  // SATA Active (SCR3: SActive)

#define PORT_CI         0x38  // Command Issue



// Simple memory access - duplicate from main file

// DEBUG: Ensure 'volatile' is used correctly for MMIO. This looks okay.

// DEBUG: Ensure the addresses used are correct physical addresses mapped appropriately.

inline uint32_t read_mem32(uint64_t addr) {

    return *((volatile uint32_t*)addr);

}



inline void write_mem32(uint64_t addr, uint32_t value) {

    *((volatile uint32_t*)addr) = value;

}





// Useful constants for AHCI Port Command and Status Register (PORT_CMD)

// DEBUG: Verify these bit definitions against AHCI spec (Section 3.3.8)

#define HBA_PORT_CMD_ST     0x0001 // Start (bit 0) - Controller processes command list

#define HBA_PORT_CMD_FRE    0x0010 // FIS Receive Enable (bit 4) - HBA can receive FISes

#define HBA_PORT_CMD_FR     0x4000 // FIS Receive Running (bit 14) - Status

#define HBA_PORT_CMD_CR     0x8000 // Command List Running (bit 15) - Status



// FIS types

// DEBUG: Verify these type codes against AHCI spec (Section 4.1)

#define FIS_TYPE_REG_H2D    0x27    // Register FIS - host to device

#define FIS_TYPE_REG_D2H    0x34    // Register FIS - device to host

#define FIS_TYPE_DMA_ACT    0x39    // DMA activate FIS - device to host

#define FIS_TYPE_DMA_SETUP  0x41    // DMA setup FIS - bidirectional

#define FIS_TYPE_DATA       0x46    // Data FIS - bidirectional

#define FIS_TYPE_BIST       0x58    // BIST activate FIS - bidirectional

#define FIS_TYPE_PIO_SETUP  0x5F    // PIO setup FIS - device to host

#define FIS_TYPE_DEV_BITS   0xA1    // Set device bits FIS - device to host



// ATA commands

// DEBUG: Verify command code against ATA/ATAPI Command Set (ACS) spec

#define ATA_CMD_IDENTIFY    0xEC    // IDENTIFY DEVICE



// AHCI command list structure (Command Header)

// DEBUG: Verify structure layout and size against AHCI spec (Section 5.5)

typedef struct {

    // DW0

    uint8_t  cfl : 5;       // Command FIS Length (in DWORDS, 2-16)

    uint8_t  a : 1;         // ATAPI

    uint8_t  w : 1;         // Write (0: H2D, 1: D2H)

    uint8_t  p : 1;         // Prefetchable

    uint8_t  r : 1;         // Reset

    uint8_t  b : 1;         // BIST

    uint8_t  c : 1;         // Clear Busy upon R_OK

    uint8_t  reserved0 : 1; // Reserved

    uint16_t prdtl;       // Physical Region Descriptor Table Length (Entries)



    // DW1

    volatile uint32_t prdbc; // Physical Region Descriptor Byte Count (bytes transferred)



    // DW2, DW3

    uint64_t ctba;        // Command Table Base Address (must be 128-byte aligned)



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

    uint64_t dba;         // Data Base Address (must be 2-byte aligned)



    // DW2

    uint32_t reserved0;   // Reserved



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

    hba_prdt_entry_t prdt[1]; // PRDT entries (1 to 65535)

} __attribute__((packed)) hba_cmd_tbl_t;



// Simple memory allocation for demonstration

// DEBUG: Ensure these buffers are in memory physically accessible by the AHCI controller (DMA).

// DEBUG: Ensure alignment requirements are met.

#define IDENTIFY_DATA_SIZE 512

// Command list (array of Command Headers) must be 1KB aligned.

static uint8_t cmd_list_buffer[1024] __attribute__((aligned(1024)));

// Received FIS buffer must be 256-byte aligned.

static uint8_t fis_buffer[256] __attribute__((aligned(256)));

// Command Table must be 128-byte aligned.

// DEBUG: Calculate size more precisely: 64 (CFIS) + 16 (ACMD) + 48 (Resvd) + 1 * sizeof(hba_prdt_entry_t) = 128 + 16 = 144 bytes.

//        Size needs to accommodate the number of PRDT entries specified in cmd_header->prdtl.

//        Using a larger buffer is safer for now.

static uint8_t cmd_table_buffer[256] __attribute__((aligned(128)));

// Data buffer must be 2-byte aligned (word aligned).

static uint8_t identify_data_buffer[IDENTIFY_DATA_SIZE] __attribute__((aligned(2)));





// Wait for a bit to clear in the specified register

// DEBUG: This is a busy-wait loop. In a real system, use a proper timer or sleep mechanism.

//        The inner loop count (100000) is arbitrary and CPU-speed dependent.

//        Timeout value might need adjustment based on hardware.

int wait_for_clear(uint64_t reg_addr, uint32_t mask, int timeout_ms) {

    // DEBUG: Consider adding a check for timeout_ms <= 0

    for (int i = 0; i < timeout_ms * 10; i++) { // Arbitrary multiplier for delay loop

        if ((read_mem32(reg_addr) & mask) == 0) {

            return 0;  // Success

        }

        // Simple delay - replace with platform-specific delay/yield if possible

        for (volatile int j = 0; j < 100000; j++);

    }

    return -1;  // Timeout

}



// Function to display IDENTIFY data in a readable format

// DEBUG: Assumes iostream_wrapper can handle basic types and C-style strings.

// DEBUG: Assumes identify_data_buffer contains valid data after successful command.

void display_identify_data(uint16_t* data) {

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

        model_number[i * 2] = (char)(word >> 8); // High byte first

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

        cout << lba_low << " (" << int(max_lba48) << " total)\n"; // Try printing uint64_t if possible



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

int send_identify_command(uint64_t ahci_base, int port) {

    // DEBUG: Validate port number against HBA capabilities (e.g., read HBA_CAP register)

    uint64_t port_addr = ahci_base + 0x100 + (port * 0x80);





    // --- Pre-Command Setup & Checks ---



    // 1. Stop the port command processing IF it's running?

    //    This might be necessary to safely modify CLB/FB.

    //    However, stopping/starting can be complex. Let's assume it's stopped or safe.

    //    Alternative: Find an unused command slot if the port is running.

    //    For simplicity, let's try without stopping first, assuming slot 0 is free.



    // 2. Ensure FIS Receive is Enabled (PORT_CMD.FRE, bit 4)

    //    The HBA needs this to receive the response FIS from the device.

    //    This should typically be set during port initialization.

    uint32_t port_cmd = read_mem32(port_addr + PORT_CMD);

    if (!(port_cmd & HBA_PORT_CMD_FRE)) {

        cout << "WARNING: Port " << port << " FIS Receive (FRE) is not enabled. Attempting to enable.\n";

        write_mem32(port_addr + PORT_CMD, port_cmd | HBA_PORT_CMD_FRE);

        // Small delay might be needed after enabling FRE

        for (volatile int j = 0; j < 10000; j++); // Short delay

        port_cmd = read_mem32(port_addr + PORT_CMD); // Re-read

        if (!(port_cmd & HBA_PORT_CMD_FRE)) {

            cout << "ERROR: Failed to enable FIS Receive (FRE) on port " << port << "\n";

            return -2; // Use distinct error code

        }

    }



    // 3. Ensure Port is Started (PORT_CMD.ST, bit 0)

    //    The controller needs to be processing commands.

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



    // 5. Find an empty command slot (we'll use slot 0 for simplicity)

    //    Check PORT_CI and PORT_SACT to ensure slot 0 is free.

    uint32_t ci_val = read_mem32(port_addr + PORT_CI);

    uint32_t sact_val = read_mem32(port_addr + PORT_SACT);

    int slot = 0; // Using slot 0

    if ((ci_val | sact_val) & (1 << slot)) {

        cout << "ERROR: Command slot " << slot << " is already active (CI=0x" << ci_val << ", SACT=0x" << sact_val << "). Cannot issue command.\n";

        // DEBUG: In a real driver, find the first free slot (lowest bit clear in CI | SACT).

        return -5;

    }



    // --- Setup Command Structures ---

    // DEBUG: Ensure physical addresses are used for DMA. Cast virtual addresses to physical if needed.

    //        This code assumes identity mapping or that buffers are already in physical memory.

    uint64_t cmd_list_phys = (uint64_t)cmd_list_buffer; // DEBUG: Replace with actual physical address if different

    uint64_t fis_buffer_phys = (uint64_t)fis_buffer;   // DEBUG: Replace with actual physical address if different

    uint64_t cmd_table_phys = (uint64_t)cmd_table_buffer; // DEBUG: Replace with actual physical address if different

    uint64_t identify_data_phys = (uint64_t)identify_data_buffer; // DEBUG: Replace with actual physical address if different



    // Get pointers to the (virtual) buffers

    hba_cmd_header_t* cmd_header = (hba_cmd_header_t*)(cmd_list_buffer + (slot * sizeof(hba_cmd_header_t)));

    hba_cmd_tbl_t* cmd_table = (hba_cmd_tbl_t*)cmd_table_buffer; // Assuming one command table for slot 0

    fis_reg_h2d_t* cmdfis = (fis_reg_h2d_t*)cmd_table->cfis;



    // Clear buffers (important!)

    // DEBUG: Use memset if available for efficiency.

    for (int i = 0; i < sizeof(hba_cmd_header_t); i++) ((uint8_t*)cmd_header)[i] = 0;

    // DEBUG: Only clear the relevant part of the command table buffer

    for (int i = 0; i < sizeof(cmd_table_buffer); i++) cmd_table_buffer[i] = 0;

    // DEBUG: Clear the data buffer before the read

    for (int i = 0; i < IDENTIFY_DATA_SIZE; i++) identify_data_buffer[i] = 0;

    // NOTE: fis_buffer is for received FISes, doesn't need clearing here.



    // Configure the command header (for slot 0)

    cmd_header->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t); // 20 bytes / 4 = 5 DWORDs

    cmd_header->a = 0;    // Not ATAPI

    cmd_header->w = 0;    // Write = 0 indicates Host-to-Device data transfer direction *for the FIS*, not the command itself. IDENTIFY reads data.

    cmd_header->p = 1;    // Prefetchable = 1 is okay if PRDT is used.

    cmd_header->prdtl = 1; // We are using 1 PRDT entry.

    // cmd_header->prdbc is volatile, cleared by HBA before command execution, updated on completion.

    cmd_header->ctba = cmd_table_phys; // Physical address of the command table



    // Configure the command table

    // Setup PRDT entry 0

    cmd_table->prdt[0].dba = identify_data_phys;      // Physical address of data buffer

    cmd_table->prdt[0].dbc = IDENTIFY_DATA_SIZE - 1; // Byte count (0-based). 511 for 512 bytes.

    cmd_table->prdt[0].i = 1; // Interrupt on completion (useful even if polling CI)



    // Setup the Command FIS (H2D Register FIS) within the command table

    cmdfis->fis_type = FIS_TYPE_REG_H2D;

    cmdfis->c = 1; // This is a command FIS

    cmdfis->command = ATA_CMD_IDENTIFY;

    cmdfis->device = 0; // Master device, LBA mode (bit 6=1). 0 might work, but 0xE0 (11100000) is standard for LBA master. Let's try 0 first.

    // DEBUG: Try cmdfis->device = 0xE0; if 0 fails.

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

    write_mem32(port_addr + PORT_CI, (1 << slot)); // Issue command on slot 0



    cout << "Command issued, waiting for completion...\n";



    // 11. Wait for command completion by polling PORT_CI bit for the slot to clear

    //     Timeout needs to be generous for IDENTIFY command (can take a few seconds).

    if (wait_for_clear(port_addr + PORT_CI, (1 << slot), 5000) < 0) { // Timeout 5 seconds

        cout << "ERROR: Command timed out waiting for CI bit " << slot << " to clear.\n";

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

        if (tfd & 0x80) cout << "BSY "; // Busy

        if (tfd & 0x40) cout << "DRDY "; // Device Ready

        if (tfd & 0x20) cout << "DF ";  // Device Fault

        if (tfd & 0x10) cout << "DSC "; // Device Seek Complete (Obsolete)

        if (tfd & 0x08) cout << "DRQ "; // Data Request

        if (tfd & 0x04) cout << "CORR "; // Corrected Data (Obsolete)

        if (tfd & 0x02) cout << "IDX "; // Index (Obsolete)

        if (tfd & 0x01) cout << "ERR "; // Error

        cout << "(Raw TFD: 0x" << tfd << ")\n";



        // Check SError register for more details if ERR bit is set

        if (tfd & 0x01) {

            uint32_t serr_val = read_mem32(port_addr + PORT_SERR);

            // Decode SERR bits here based on AHCI spec 3.3.11 if needed

        }

        return -8;

    }



    // 13. Check how many bytes were actually transferred (optional but good practice)

    uint32_t bytes_transferred = cmd_header->prdbc;

    if (bytes_transferred != IDENTIFY_DATA_SIZE) {

        cout << "WARNING: Expected " << IDENTIFY_DATA_SIZE << " bytes, but received " << bytes_transferred << " bytes.\n";

        // Data might still be usable, but it's unexpected.

    }



    // 14. Process the IDENTIFY data if successful

    cout << "\nIDENTIFY command completed successfully.\n";

    display_identify_data((uint16_t*)identify_data_buffer); // Cast the byte buffer to word pointer



    return 0; // Success

}



#endif // IDENTIFY_H

