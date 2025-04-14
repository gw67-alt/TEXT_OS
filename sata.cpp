/*
 * SATA Controller Debug Utility
 * For bare metal AMD64 environment
 */
#include "kernel.h"
#include "iostream_wrapper.h"
#include "pci.h"
#include "stdlib_hooks.h"

 // AHCI registers offsets
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

// Simple memory access
inline uint32_t read_mem32(uint64_t addr) {
    return *((volatile uint32_t*)addr);
}

// Function to print hex value with label
void print_hex(const char* label, uint32_t value) {
    cout << label;

    // Convert to hex
    char hex_chars[16] = { '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F' };
    char buffer[10];

    buffer[0] = '0';
    buffer[1] = 'x';

    for (int i = 0; i < 8; i++) {
        uint8_t nibble = (value >> (28 - i * 4)) & 0xF;
        buffer[2 + i] = hex_chars[nibble];
    }
    buffer[10] = '\0';

    cout << buffer << "\n";
}

// Function to decode port status
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

// Function to decode task file data
void decode_task_file(uint32_t tfd) {
    uint8_t status = tfd & 0xFF;
    uint8_t error = (tfd >> 8) & 0xFF;

    cout << "  Status register: ";
    if (status & 0x80) cout << "BSY ";
    if (status & 0x40) cout << "DRDY ";
    if (status & 0x20) cout << "DF ";
    if (status & 0x10) cout << "DSC ";
    if (status & 0x08) cout << "DRQ ";
    if (status & 0x04) cout << "CORR ";
    if (status & 0x02) cout << "IDX ";
    if (status & 0x01) cout << "ERR ";
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

// Function to decode port command register
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

    for (bus = 0; bus < 256 && !ahci_base; bus++) {
        for (dev = 0; dev < 32 && !ahci_base; dev++) {
            for (func = 0; func < 8 && !ahci_base; func++) {
                uint32_t class_reg = pci_read_config_dword(bus, dev, func, 0x08);
                uint8_t class_code = (class_reg >> 24) & 0xFF;
                uint8_t subclass = (class_reg >> 16) & 0xFF;
                uint8_t prog_if = (class_reg >> 8) & 0xFF;

                if (class_code == 0x01 && subclass == 0x06 && prog_if == 0x01) {
                    uint32_t bar5 = pci_read_config_dword(bus, dev, func, 0x24);
                    ahci_base = bar5 & ~0xF;

                    cout << "Found AHCI controller at PCI ";
                    cout << (int)bus << ":" << (int)dev << ":" << (int)func << "\n";

                    // Get vendor and device ID
                    uint32_t vendor_device = pci_read_config_dword(bus, dev, func, 0x00);
                    uint16_t vendor_id = vendor_device & 0xFFFF;
                    uint16_t device_id = (vendor_device >> 16) & 0xFFFF;

                    cout << "Vendor ID: ";
                    char v_buffer[5];
                    for (int i = 0; i < 4; i++) {
                        uint8_t nibble = (vendor_id >> (12 - i * 4)) & 0xF;
                        v_buffer[i] = nibble < 10 ? '0' + nibble : 'A' + (nibble - 10);
                    }
                    v_buffer[4] = '\0';
                    cout << v_buffer << "\n";

                    cout << "Device ID: ";
                    char d_buffer[5];
                    for (int i = 0; i < 4; i++) {
                        uint8_t nibble = (device_id >> (12 - i * 4)) & 0xF;
                        d_buffer[i] = nibble < 10 ? '0' + nibble : 'A' + (nibble - 10);
                    }
                    d_buffer[4] = '\0';
                    cout << d_buffer << "\n";

                    cout << "\nPress enter to continue\n\n";
                    char input[1];
                    cin >> input;
                }
            }
        }

    }

    if (!ahci_base) {
        cout << "No SATA controller found\n";
        return;
    }

    // Print ABAR address
    cout << "AHCI Base Address: ";
    char hex_str[20];
    for (int i = 15; i >= 0; i--) {
        int digit = (ahci_base >> (i * 4)) & 0xF;
        hex_str[15 - i] = digit < 10 ? '0' + digit : 'A' + (digit - 10);
    }
    hex_str[16] = '\0';
    char* p = hex_str;
    while (*p == '0' && *(p + 1) != '\0') p++;
    cout << "0x" << p << "\n\n";

    // Read and print AHCI global registers
    uint32_t cap = read_mem32(ahci_base + AHCI_CAP);
    uint32_t ghc = read_mem32(ahci_base + AHCI_GHC);
    uint32_t is = read_mem32(ahci_base + AHCI_IS);
    uint32_t pi = read_mem32(ahci_base + AHCI_PI);
    uint32_t vs = read_mem32(ahci_base + AHCI_VS);

    print_hex("Capabilities: ", cap);
    print_hex("Global Host Control: ", ghc);
    print_hex("Interrupt Status: ", is);
    print_hex("Ports Implemented: ", pi);
    print_hex("Version: ", vs);

    // Check if AHCI mode is enabled
    cout << "AHCI Mode: " << ((ghc & 0x80000000) ? "Enabled" : "Disabled") << "\n\n";

    // Get number of ports and scan each implemented port
    cout << "Port Status:\n";

    for (int i = 0; i < 32; i++) {
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
            print_hex("  SSTS: ", ssts);
            print_hex("  TFD: ", tfd);
            print_hex("  SIG: ", sig);
            print_hex("  CMD: ", cmd);
            print_hex("  SERR: ", serr);

            // Decode port status
            decode_port_status(ssts);
            decode_task_file(tfd);
            decode_port_cmd(cmd);

            // Check signature to identify device type
            cout << "  Device type: ";
            if (sig == 0x00000101) {
                cout << "SATA drive";
            }
            else if (sig == 0xEB140101) {
                cout << "ATAPI device";
            }
            else if (sig == 0xC33C0101) {
                cout << "Enclosure management bridge";
            }
            else if (sig == 0x96690101) {
                cout << "Port multiplier";
            }
            else {
                cout << "Unknown device";
            }
            cout << "\nPress enter to continue\n\n";
            char input[1];
            cin >> input;
        }
    }

    cout << "Debug complete\n";
}