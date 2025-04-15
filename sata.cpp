/*
 * SATA Controller Debug Utility
 * For bare metal AMD64 environment
 */
#include "kernel.h"
#include "iostream_wrapper.h"
#include "pci.h"
#include "stdlib_hooks.h" // Assumed to provide basic utilities if needed
#include "identify.h"       // Includes the string R/W functions now

 // AHCI registers offsets (Keep these definitions)
#define AHCI_CAP        0x00  // Host Capabilities
#define AHCI_GHC        0x04  // Global Host Control
#define AHCI_IS         0x08  // Interrupt Status
#define AHCI_PI         0x0C  // Ports Implemented
#define AHCI_VS         0x10  // Version
#define AHCI_PORT_BASE  0x100 // Port registers base
#define AHCI_PORT_SIZE  0x80  // Size of port register space

// Port registers offsets (add to PORT_BASE + port_num * PORT_SIZE)
#define PORT_CLB        0x00  // Command List Base Address
#define PORT_CLBU       0x04  // Command List Base Address Upper 32 bits
#define PORT_FB         0x08  // FIS Base Address
#define PORT_FBU        0x0C  // FIS Base Address Upper 32 bits
#define PORT_IS         0x10  // Interrupt Status
#define PORT_IE         0x14  // Interrupt Enable
#define PORT_CMD        0x18  // Command and Status
#define PORT_TFD        0x20  // Task File Data
#define PORT_SIG        0x24  // Signature
#define PORT_SSTS       0x28  // SATA Status
#define PORT_SCTL       0x2C  // SATA Control
#define PORT_SERR       0x30  // SATA Error
#define PORT_SACT       0x34  // SATA Active
#define PORT_CI         0x38  // Command Issue


// Function to print hex value with label (Keep this function)
void print_hex(const char* label, uint32_t value) {
    cout << label;

    // Convert to hex
    char hex_chars[16] = { '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F' };
    char buffer[11]; // Increased size for 0x + 8 digits + null

    buffer[0] = '0';
    buffer[1] = 'x';

    // Fill from right to left for potentially shorter numbers if desired, but fixed 8 is fine too.
    for (int i = 0; i < 8; i++) {
        uint8_t nibble = (value >> (28 - i * 4)) & 0xF;
        buffer[2 + i] = hex_chars[nibble];
    }
    buffer[10] = '\0';

    cout << buffer << "\n";
}

// Helper to print 64-bit hex
void print_hex64(const char* label, uint64_t value) {
    cout << label;
    char hex_chars[16] = { '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F' };
    char buffer[19]; // 0x + 16 digits + null

    buffer[0] = '0';
    buffer[1] = 'x';
    for (int i = 0; i < 16; i++) {
        uint8_t nibble = (value >> (60 - i * 4)) & 0xF;
        buffer[2 + i] = hex_chars[nibble];
    }
    buffer[18] = '\0';
    cout << buffer << "\n";
}


// Simple string to unsigned int conversion (basic, no error checking)
// Assumes valid decimal number input.
unsigned int simple_stou(const char* str) {
    unsigned int res = 0;
    while (*str >= '0' && *str <= '9') {
        res = res * 10 + (*str - '0');
        str++;
    }
    return res;
}

// Simple string to unsigned long long conversion (basic, no error checking)
// Assumes valid decimal number input.
uint64_t simple_stoull(const char* str) {
    uint64_t res = 0;
    while (*str >= '0' && *str <= '9') {
        res = res * 10 + (*str - '0');
        str++;
    }
    return res;
}


// Function to decode port status (Keep this function)
void decode_port_status(uint32_t ssts) {
    uint8_t det = ssts & 0xF;
    uint8_t ipm = (ssts >> 8) & 0xF;

    cout << "  Device Detection: ";
    switch (det) {
    case 0: cout << "No device detected, PHY offline"; break;
    case 1: cout << "Device present but no communication"; break;
    case 3: cout << "Device present and communication established"; break;
    case 4: cout << "PHY offline, in BIST or loopback mode"; break;
    default: cout << "Unknown state"; break;
    }
    cout << "\n";

    cout << "  Interface Power Management: ";
    switch (ipm) {
    case 0: cout << "Not present, disabled"; break;
    case 1: cout << "Active state"; break;
    case 2: cout << "Partial power management state"; break;
    case 6: cout << "Slumber power management state"; break;
    case 8: cout << "DevSleep power management state"; break;
    default: cout << "Unknown state"; break;
    }
    cout << "\n";
}

// Function to decode task file data (Keep this function)
void decode_task_file(uint32_t tfd) {
    uint8_t status = tfd & 0xFF;
    uint8_t error = (tfd >> 8) & 0xFF;

    cout << "  Status register: ";
    if (status == 0 && error == 0) {
        cout << "(No error reported)"; // Avoid printing nothing if status is 0
    }
    else {
        if (status & 0x80) cout << "BSY ";
        if (status & 0x40) cout << "DRDY ";
        if (status & 0x20) cout << "DF ";
        if (status & 0x10) cout << "DSC ";
        if (status & 0x08) cout << "DRQ ";
        if (status & 0x04) cout << "CORR ";
        if (status & 0x02) cout << "IDX ";
        if (status & 0x01) cout << "ERR ";
    }
    cout << "\n";

    if (status & 0x01) {
        cout << "  Error register: ";
        if (error & 0x80) cout << "ICRC ";
        if (error & 0x40) cout << "UNC ";
        if (error & 0x20) cout << "MC ";
        if (error & 0x10) cout << "IDNF ";
        if (error & 0x08) cout << "MCR ";
        if (error & 0x04) cout << "ABRT ";
        if (error & 0x02) cout << "TK0NF ";
        if (error & 0x01) cout << "AMNF ";
        cout << "\n";
    }
}

// Function to decode port command register (Keep this function)
void decode_port_cmd(uint32_t cmd) {
    cout << "  Command register: ";
    if (cmd & 0x0001) cout << "ST ";
    if (cmd & 0x0002) cout << "SUD ";
    if (cmd & 0x0004) cout << "POD ";
    if (cmd & 0x0008) cout << "CLO ";
    if (cmd & 0x0010) cout << "FRE ";
    if (cmd & 0x0020) cout << "MPSS ";
    if (cmd & 0x4000) cout << "FR ";
    if (cmd & 0x8000) cout << "CR ";
    cout << "\n";
}

// Debug function to fully examine SATA controller state
void debug_sata_controller() {
    cout << "SATA Controller Debug\n";
    cout << "--------------------\n";

    // Find AHCI controller via PCI
    uint64_t ahci_base = 0;
    uint16_t bus, dev, func;
    uint16_t ahci_bus = 0, ahci_dev = 0, ahci_func = 0; // Store location

    for (bus = 0; bus < 256 && !ahci_base; bus++) {
        for (dev = 0; dev < 32 && !ahci_base; dev++) {
            for (func = 0; func < 8 && !ahci_base; func++) {
                // Check if device exists first (Vendor ID != 0xFFFF)
                uint32_t vendor_device_check = pci_read_config_dword(bus, dev, func, 0x00);
                if ((vendor_device_check & 0xFFFF) == 0xFFFF) {
                    continue; // No device here
                }

                uint32_t class_reg = pci_read_config_dword(bus, dev, func, 0x08);
                uint8_t class_code = (class_reg >> 24) & 0xFF;
                uint8_t subclass = (class_reg >> 16) & 0xFF;
                uint8_t prog_if = (class_reg >> 8) & 0xFF;

                if (class_code == 0x01 && subclass == 0x06 && prog_if == 0x01) {
                    uint32_t bar5 = pci_read_config_dword(bus, dev, func, 0x24);
                    // Check if BAR5 is memory mapped and non-zero
                    if ((bar5 & 0x1) == 0 && (bar5 & ~0xF) != 0) {
                        ahci_base = bar5 & ~0xF;
                        ahci_bus = bus;
                        ahci_dev = dev;
                        ahci_func = func;

                        cout << "Found AHCI controller at PCI ";
                        cout << (int)bus << ":" << (int)dev << "." << (int)func << "\n"; // Use dot separator common practice

                        // Get vendor and device ID
                        uint32_t vendor_device = pci_read_config_dword(bus, dev, func, 0x00);
                        uint16_t vendor_id = vendor_device & 0xFFFF;
                        uint16_t device_id = (vendor_device >> 16) & 0xFFFF;

                        // Use print_hex for consistency (need to adapt for 16-bit)
                        print_hex(" Vendor ID: ", vendor_id); // Assuming print_hex handles width ok
                        print_hex(" Device ID: ", device_id);

                        cout << "\nPress enter to continue...\n\n";
                        char input[2]; // Allow for potential newline char
                        cin >> input; // Read a line to consume potential newline
                    }
                }
            }
        }
    }

    if (!ahci_base) {
        cout << "No AHCI controller found or BAR5 not valid.\n";
        return;
    }

    // Print ABAR address using helper
    print_hex64("AHCI Base Address (ABAR): ", ahci_base);
    cout << "\n";

    // Read and print AHCI global registers
    uint32_t cap = read_mem32(ahci_base + AHCI_CAP);
    uint32_t ghc = read_mem32(ahci_base + AHCI_GHC);
    uint32_t is = read_mem32(ahci_base + AHCI_IS);
    uint32_t pi = read_mem32(ahci_base + AHCI_PI);
    uint32_t vs = read_mem32(ahci_base + AHCI_VS);

    print_hex("Capabilities (CAP):       ", cap);
    print_hex("Global Host Control (GHC):", ghc);
    print_hex("Interrupt Status (IS):    ", is);
    print_hex("Ports Implemented (PI):   ", pi);
    print_hex("Version (VS):             ", vs);

    // Check if AHCI mode is enabled
    cout << "AHCI Mode (GHC.AE):       " << ((ghc & 0x80000000) ? "Enabled" : "Disabled") << "\n\n";

    // Get number of ports and scan each implemented port
    cout << "Port Status:\n";
    cout << "------------\n";

    bool any_device_found = false;
    for (int i = 0; i < 8; i++) {
        if (pi & (1 << i)) {
            uint64_t port_addr = ahci_base + AHCI_PORT_BASE + (i * AHCI_PORT_SIZE);

            cout << "Port " << i << ":\n";

            // Read port registers
            uint32_t ssts = read_mem32(port_addr + PORT_SSTS);
            uint32_t tfd = read_mem32(port_addr + PORT_TFD);
            uint32_t sig = read_mem32(port_addr + PORT_SIG);
            uint32_t cmd = read_mem32(port_addr + PORT_CMD);
            uint32_t serr = read_mem32(port_addr + PORT_SERR);

            // Print raw values
            print_hex("  SSTS (SATA Status):   ", ssts);
            print_hex("  TFD (Task File Data): ", tfd);
            print_hex("  SIG (Signature):      ", sig);
            print_hex("  CMD (Command/Status): ", cmd);
            print_hex("  SERR (SATA Error):    ", serr);

            // Decode port status
            decode_port_status(ssts);
            decode_task_file(tfd);
            decode_port_cmd(cmd);

            // Check signature to identify device type
            cout << "  Device type: ";
            bool is_sata_drive = (sig == 0x00000101);
            if (is_sata_drive) {
                cout << "SATA Drive (Non-ATAPI)";
                any_device_found = true;
            }
            else if (sig == 0xEB140101) {
                cout << "SATAPI Drive (ATAPI)";
                any_device_found = true; // Can still potentially read/write using ATAPI commands
            }
            else if (sig == 0xC33C0101) {
                cout << "Enclosure Management Bridge (SEMB)";
            }
            else if (sig == 0x96690101) {
                cout << "Port Multiplier";
            }
            else {
                // Check detection status again
                uint8_t det = ssts & 0xF;
                if (det == 0 || det == 4) {
                    cout << "No Device Detected";
                }
                else {
                    print_hex("Unknown Device Signature: ", sig);
                }
            }
            cout << "\n";

            // Print Command List Base Address and FIS Base Address if FRE or ST is set
            if (cmd & (HBA_PORT_CMD_FRE | HBA_PORT_CMD_ST)) {
                uint64_t clb = ((uint64_t)read_mem32(port_addr + PORT_CLBU) << 32) | read_mem32(port_addr + PORT_CLB);
                uint64_t fb = ((uint64_t)read_mem32(port_addr + PORT_FBU) << 32) | read_mem32(port_addr + PORT_FB);
                print_hex64("  Command List Base (CLB):", clb);
                print_hex64("  FIS Base (FB):          ", fb);
            }


            cout << "\nPress enter to continue...\n\n";
            char input[2];
            cin >> input;
        }
    }

    // --- IDENTIFY Command Section ---
    if (any_device_found) { // Only offer if we found something plausible
        cout << "\nSend IDENTIFY commands to detected SATA/SATAPI devices? (y/n): ";
        char response[4]; // Increase size for safety
        cin >> response;

        if (response[0] == 'y' || response[0] == 'Y') {
            for (int i = 0; i < 8; i++) {
                if (pi & (1 << i)) {
                    uint64_t port_addr = ahci_base + AHCI_PORT_BASE + (i * AHCI_PORT_SIZE);
                    uint32_t sig = read_mem32(port_addr + PORT_SIG);
                    // Only try IDENTIFY on plausible devices (SATA/SATAPI) and if communication seems established
                    uint32_t ssts = read_mem32(port_addr + PORT_SSTS);
                    uint8_t det = ssts & 0xF;

                    if ((sig == 0x00000101 || sig == 0xEB140101) && det == 3) {
                        cout << "Send IDENTIFY to port " << i << "? (y/n): ";
                        char port_response[4];
                        cin >> port_response;

                        if (port_response[0] == 'y' || port_response[0] == 'Y') {
                            int result = send_identify_command(ahci_base, i);
                            if (result == 0) {
                                cout << "IDENTIFY command for port " << i << " succeeded.\n";
                            }
                            else {
                                cout << "IDENTIFY command for port " << i << " failed (Error code: " << result << ").\n";
                            }
                            cout << "\nPress enter to continue...\n\n";
                            char input[2];
                            cin >> input;
                        }
                    }
                }
            }
        }
    }
    else {
        cout << "\nNo active SATA/SATAPI devices found, skipping IDENTIFY command option.\n";
    }


    // --- Sector String Read/Write Section ---
    if (any_device_found) {
        cout << "\nPerform sector string read/write operations? (y/n): ";
        char rw_response[4];
        cin >> rw_response;

        if (rw_response[0] == 'y' || rw_response[0] == 'Y') {
            while (true) {
                cout << "\nSector R/W Menu:\n";
                cout << "Enter port number (0-31) or 'q' to quit: ";
                char port_input[8];
                cin >> port_input;
                if (port_input[0] == 'q' || port_input[0] == 'Q') break;

                int port_num = simple_stou(port_input);
                if (port_num > 31 || !(pi & (1 << port_num))) {
                    cout << "Invalid or unimplemented port number.\n";
                    continue;
                }

                // Check if port has a device suitable for R/W
                uint64_t port_addr = ahci_base + AHCI_PORT_BASE + (port_num * AHCI_PORT_SIZE);
                uint32_t sig = read_mem32(port_addr + PORT_SIG);
                uint32_t ssts = read_mem32(port_addr + PORT_SSTS);
                uint8_t det = ssts & 0xF;
                if (!((sig == 0x00000101 || sig == 0xEB140101) && det == 3)) {
                    cout << "Port " << port_num << " does not have an active SATA/SATAPI device suitable for R/W.\n";
                    continue;
                }


                cout << "Enter LBA (sector number, decimal): ";
                char lba_input[24]; // Enough for 64-bit decimal
                cin >> lba_input;
                uint64_t lba = simple_stoull(lba_input);

                cout << "Action: 'r' to Read string, 'w' to Write string: ";
                char action_input[4];
                cin >> action_input;

                if (action_input[0] == 'w' || action_input[0] == 'W') {
                    //cout << "\n *** WARNING: Writing to LBA " << (unsigned long long)lba << " on port " << port_num << " is potentially DANGEROUS! ***\n";
                    //cout << " *** Especially LBA 0 often contains critical boot/partition data. ***\n";
                    cout << "Enter string to write (max " << SECTOR_SIZE - 1 << " chars, spaces allowed): ";
                    char write_buffer[SECTOR_SIZE]; // Buffer to hold user input
                    cin >>write_buffer;

                    int result = write_string_to_sector(ahci_base, port_num, lba, write_buffer);
                    if (result == 0) {
                        cout << "Write successful.\n";
                    }

                }
                else if (action_input[0] == 'r' || action_input[0] == 'R') {
                    char read_buffer[SECTOR_SIZE + 1]; // +1 for null terminator safety
                    int result = read_string_from_sector(ahci_base, port_num, lba, read_buffer, sizeof(read_buffer));

                    if (result >= 0) { // 0 for success, 1 for truncated
                        //cout << "Read successful. Data from LBA " << (unsigned long long)lba << ":\n";
                        cout << "--------------------------------------------------\n";
                        cout << read_buffer << "\n";
                        cout << "--------------------------------------------------\n";
                        if (result == 1) {
                            cout << "(Note: String may have been truncated if it exceeded buffer size or lacked null terminator within sector)\n";
                        }
                    }
                    else {
                        cout << "Read failed (Error code: " << result << ").\n";
                    }
                }
                else {
                    cout << "Invalid action.\n";
                }
                cout << "\nPress enter to continue...\n\n";
                char input[2];
                cin >> input;
            } // end while loop
        }
    }


    cout << "\nSATA Controller Debug complete.\n";
}