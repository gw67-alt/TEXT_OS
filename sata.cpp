/*
 * SATA Controller Debug Utility & Hash Transformation
 * For bare metal AMD64 environment
 */
#include <cstdint>        // For uint32_t, uint8_t (standard integer types)
#include "kernel.h"       // Assumed to provide basic types/macros if needed
#include "stdlib_hooks.h" // Assumed to provide memcpy, strlen, memset, strncmp etc.
#include "iostream_wrapper.h" // Assumed to provide cout, cin
#include "pci.h"
#include "identify.h"     // Includes sector read/write functions (MUST handle raw binary)

 // Common Constants
#define SECTOR_SIZE     512

// --- AHCI Definitions ---
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

// HBA Port CMD bits (used in debug_sata_controller, ensure defined if not in identify.h)
#ifndef HBA_PORT_CMD_ST
#define HBA_PORT_CMD_ST   0x0001 // Start
#endif
#ifndef HBA_PORT_CMD_FRE
#define HBA_PORT_CMD_FRE  0x0010 // FIS Receive Enable
#endif

// --- Utility Functions ---

// Function to print hex value with label
void print_hex(const char* label, uint32_t value) {
    cout << label;
    char hex_chars[16] = { '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F' };
    char buffer[11]; // 0x + 8 digits + null
    buffer[0] = '0';
    buffer[1] = 'x';
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

// Simple string to unsigned int conversion
unsigned int simple_stou(const char* str) {
    unsigned int res = 0;
    while (*str >= '0' && *str <= '9') {
        res = res * 10 + (*str - '0');
        str++;
    }
    return res;
}

// Simple string to unsigned long long conversion
uint64_t simple_stoull(const char* str) {
    uint64_t res = 0;
    while (*str >= '0' && *str <= '9') {
        res = res * 10 + (*str - '0');
        str++;
    }
    return res;
}

// --- FNV-1a 32-bit Hash Functions (Bare-Metal Friendly) ---
// Note: This is a non-cryptographic hash function.

// FNV-1a constants (32-bit)
#define FNV_PRIME_32 (0x01000193)
#define FNV_OFFSET_BASIS_32 (0x811C9DC5)

// Processes a block of data, updating the hash
uint32_t fnv1a_32_process(uint32_t current_hash, const unsigned char* data, size_t len) {
    uint32_t hash = current_hash;
    for (size_t i = 0; i < len; ++i) {
        hash ^= static_cast<uint32_t>(data[i]);
        hash *= FNV_PRIME_32;
    }
    return hash;
}

// Starts the FNV hash calculation
uint32_t fnv1a_32_start() {
    return FNV_OFFSET_BASIS_32;
}

// --- Hash-Based Data Transformation Functions (Bare-Metal) ---
// WARNING: Provides NO real encryption, only simple obfuscation. DO NOT use for security.

/**
 * @brief Transforms data by adding hash bytes cyclically (Modulo 256).
 * Writes the result to an output buffer. Hash depends only on password for reversibility.
 */
int transform_data_with_password_hash(
    const unsigned char* data, size_t data_len,
    const char* password, // Assumed null-terminated C string
    unsigned char* output_buffer, size_t output_buffer_size)
{
    // 1. Basic validation
    size_t password_len = strlen(password); // Requires strlen
    if (password_len == 0) {
        cout << "Error: Password cannot be empty.\n";
        return -1;
    }
    if (output_buffer_size < data_len) {
        cout << "Error: Output buffer too small.\n";
        return -2;
    }

    // 2. Calculate hash based ONLY on the password for reversibility
    uint32_t hash = fnv1a_32_start();
    hash = fnv1a_32_process(hash, reinterpret_cast<const unsigned char*>(password), password_len);

    // 3. Extract bytes from the hash
    unsigned char hash_bytes[4];
    hash_bytes[0] = (hash >> 24) & 0xFF;
    hash_bytes[1] = (hash >> 16) & 0xFF;
    hash_bytes[2] = (hash >> 8) & 0xFF;
    hash_bytes[3] = hash & 0xFF;

    // 4. Transform data: Add hash bytes cyclically (modulo 256)
    for (size_t i = 0; i < data_len; ++i) {
        output_buffer[i] = data[i] + hash_bytes[i % 4]; // Relies on unsigned char wrap-around
    }

    return 0; // Success
}

/**
 * @brief Reverses the transformation done by transform_data_with_password_hash.
 */
int untransform_data_with_password_hash(
    const unsigned char* transformed_data, size_t data_len,
    const char* password, // Assumed null-terminated C string
    unsigned char* output_buffer, size_t output_buffer_size)
{
    // 1. Basic validation
    size_t password_len = strlen(password); // Requires strlen
    if (password_len == 0) {
        cout << "Error: Password cannot be empty.\n";
        return -1;
    }
    if (output_buffer_size < data_len) {
        cout << "Error: Output buffer too small.\n";
        return -2;
    }

    // 2. Calculate hash based ONLY on the password (must match transform function)
    uint32_t hash = fnv1a_32_start();
    hash = fnv1a_32_process(hash, reinterpret_cast<const unsigned char*>(password), password_len);

    // 3. Extract bytes from the hash
    unsigned char hash_bytes[4];
    hash_bytes[0] = (hash >> 24) & 0xFF;
    hash_bytes[1] = (hash >> 16) & 0xFF;
    hash_bytes[2] = (hash >> 8) & 0xFF;
    hash_bytes[3] = hash & 0xFF;

    // 4. Untransform data: Subtract hash bytes cyclically (modulo 256)
    for (size_t i = 0; i < data_len; ++i) {
        output_buffer[i] = transformed_data[i] - hash_bytes[i % 4]; // Relies on unsigned char wrap-around
    }

    return 0; // Success
}


// --- SATA Debug Functions ---

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
    if (status == 0 && error == 0) {
        cout << "(No error reported)";
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
// (Modified to accept ahci_base as parameter)
void debug_sata_controller(uint64_t ahci_base) {
    cout << "\nSATA Controller Debug\n";
    cout << "--------------------\n";

    if (!ahci_base) {
        cout << "Invalid AHCI base address provided.\n";
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
    int max_ports = 8; // AHCI supports up to 32 ports

    for (int i = 0; i < max_ports; i++) {
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
            cin >> input; // Consume potential newline
             // Ignore rest of the line including newline
        }
    }

    // --- IDENTIFY Command Section ---
    if (any_device_found) {
        cout << "\nSend IDENTIFY commands to detected SATA/SATAPI devices? (y/n): ";
        char response[4];
        cin >> response;
         // Consume newline

        if (response[0] == 'y' || response[0] == 'Y') {
            for (int i = 0; i < max_ports; i++) {
                if (pi & (1 << i)) {
                    uint64_t port_addr = ahci_base + AHCI_PORT_BASE + (i * AHCI_PORT_SIZE);
                    uint32_t sig = read_mem32(port_addr + PORT_SIG);
                    uint32_t ssts = read_mem32(port_addr + PORT_SSTS);
                    uint8_t det = ssts & 0xF;

                    if ((sig == 0x00000101 || sig == 0xEB140101) && det == 3) {
                        cout << "Send IDENTIFY to port " << i << "? (y/n): ";
                        char port_response[4];
                        cin >> port_response;
                         // Consume newline

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
                            cin >> input; // Consume potential newline
                            
                        }
                    }
                }
            }
        }
    }
    else {
        cout << "\nNo active SATA/SATAPI devices found, skipping IDENTIFY command option.\n";
    }

    // --- Encrypted Sector Read/Write Section ---
    // WARNING: This uses insecure obfuscation, not real encryption!
    if (any_device_found) {
        cout << "\n(WARNING: Uses custom hash-based transformation, experimental encryption!)\n";

        cout << "\nPerform OBFUSCATED sector read/write operations? (y/n): ";
        char enc_response[4];
        cin >> enc_response;
         // Consume newline

        if (enc_response[0] == 'y' || enc_response[0] == 'Y') {
            while (true) {
                cout << "\nObfuscated Sector R/W Menu:\n";
                cout << "Enter port number (0-" << max_ports - 1 << ") or 'q' to quit: ";
                char port_input[8];
                cin >> port_input;
                 // Consume newline
                if (port_input[0] == 'q' || port_input[0] == 'Q') break;

                int port_num_enc = simple_stou(port_input);
                if (port_num_enc < 0 || port_num_enc >= max_ports || !(pi & (1 << port_num_enc))) {
                    cout << "Invalid or unimplemented port number.\n";
                    continue;
                }

                // Check device presence
                uint64_t port_addr = ahci_base + AHCI_PORT_BASE + (port_num_enc * AHCI_PORT_SIZE);
                uint32_t sig = read_mem32(port_addr + PORT_SIG);
                uint32_t ssts = read_mem32(port_addr + PORT_SSTS);
                uint8_t det = ssts & 0xF;
                if (!((sig == 0x00000101 || sig == 0xEB140101) && det == 3)) {
                    cout << "Port " << port_num_enc << " does not have an active SATA/SATAPI device suitable for R/W.\n";
                    continue;
                }

                cout << "Enter LBA (sector number, decimal): ";
                char lba_input[24];
                cin >> lba_input;
                
                uint64_t lba = simple_stoull(lba_input);

                cout << "Enter password for transformation: ";
                char password[128]; // Fixed-size buffer for password
                cin >> password; // Reads only up to first whitespace
                

                cout << "Action: 'e' to Encrypt/Write, 'd' to Read/Decrypt: ";
                char action_input[4];
                cin >> action_input;
                

                unsigned char data_buffer[SECTOR_SIZE];
                unsigned char transformed_buffer[SECTOR_SIZE];

                if (action_input[0] == 'e' || action_input[0] == 'E') {
                    cout << "Enter data to write (max " << SECTOR_SIZE - 1 << " chars, reads up to first space): ";
                    char input_data[SECTOR_SIZE];
                    cin >> input_data; // Reads only up to first whitespace
                    

                    size_t input_len = strlen(input_data);
                    memset(data_buffer, 0, SECTOR_SIZE); // Zero buffer first
                    memcpy(data_buffer, input_data, input_len); // Copy input data

                    // Transform the data
                    int transform_result = transform_data_with_password_hash(
                        data_buffer, SECTOR_SIZE, // Transform the whole sector buffer
                        password,
                        transformed_buffer, SECTOR_SIZE
                    );

                    if (transform_result != 0) {
                        cout << "Error during data transformation: " << transform_result << "\n";
                        continue;
                    }

                    cout << "\n *** WARNING: Writing obfuscated data to LBA " << (unsigned int)lba << " on port " << port_num_enc << " ***\n";
                    // ** CRITICAL ASSUMPTION: write_string_to_sector handles raw binary **
                    int write_result = write_string_to_sector(ahci_base, port_num_enc, lba, (char*)transformed_buffer); // Cast needed if original takes char*
                    if (write_result == 0) {
                        cout << "Obfuscated write successful.\n";
                    }
                    else {
                        cout << "Obfuscated write failed (Error code: " << write_result << ").\n";
                    }

                }
                else if (action_input[0] == 'd' || action_input[0] == 'D') {
                    // Read the (presumably) transformed data from the sector
                    // ** CRITICAL ASSUMPTION: read_string_from_sector handles raw binary **
                    int read_result = read_string_from_sector(ahci_base, port_num_enc, lba, (char*)transformed_buffer, SECTOR_SIZE); // Cast needed

                    if (read_result < 0) {
                        cout << "Read failed (Error code: " << read_result << "). Cannot decrypt.\n";
                        continue;
                    }

                    // Untransform the data
                    int untransform_result = untransform_data_with_password_hash(
                        transformed_buffer, SECTOR_SIZE, // Untransform the whole sector
                        password,
                        data_buffer, SECTOR_SIZE
                    );

                    if (untransform_result != 0) {
                        cout << "Error during data untransformation: " << untransform_result << "\n";
                        continue;
                    }

                    cout << "Read/Decryption successful. Original data from LBA " << (unsigned int)lba << ":\n"; // Changed cast for LBA
                    cout << "--------------------------------------------------\n";

                    // --- MODIFIED CODE START ---
                    // Print the decrypted data, replacing non-printable chars with '.'
                    for (size_t i = 0; i < SECTOR_SIZE; ++i) {
                        unsigned char current_byte = data_buffer[i];
                        if (current_byte >= 32 && current_byte <= 126) {
                            // Printable ASCII character
                            cout << (char)current_byte;
                        }
                        else {
                            // Non-printable character (including null, control chars, etc.)
                            cout << '.';
                        }
                        // Optional: Add line breaks for readability
                        if ((i + 1) % 64 == 0) { // Newline every 64 characters
                            cout << "\n";
                        }
                    }
                    // Ensure a final newline if the loop didn't end exactly on a multiple of 64
                    if (SECTOR_SIZE % 64 != 0) {
                        cout << "\n";
                    }
                    // --- MODIFIED CODE END ---

                    cout << "--------------------------------------------------\n";

                }
                else {
                    cout << "Invalid action.\n";
                }

                cout << "\nPress enter to continue...\n\n";
                char input[2];
                cin >> input; // Consume potential newline
                
            } // end while loop
        }
    }


    cout << "\nSATA Controller Debug complete.\n";
}


// --- Main Entry Point ---
int crypt() {
    uint64_t ahci_base = 0;
    uint16_t bus, dev, func;

    cout << "System Booting...\n";

    // Find AHCI controller via PCI (Simplified)
    for (bus = 0; bus < 256 && !ahci_base; bus++) {
        for (dev = 0; dev < 32 && !ahci_base; dev++) {
            for (func = 0; func < 8 && !ahci_base; func++) {
                uint32_t vendor_device_check = pci_read_config_dword(bus, dev, func, 0x00);
                if ((vendor_device_check & 0xFFFF) == 0xFFFF) continue;

                uint32_t class_reg = pci_read_config_dword(bus, dev, func, 0x08);
                uint8_t class_code = (class_reg >> 24) & 0xFF;
                uint8_t subclass = (class_reg >> 16) & 0xFF;
                uint8_t prog_if = (class_reg >> 8) & 0xFF;

                if (class_code == 0x01 && subclass == 0x06 && prog_if == 0x01) {
                    uint32_t bar5 = pci_read_config_dword(bus, dev, func, 0x24);
                    if ((bar5 & 0x1) == 0 && (bar5 & ~0xF) != 0) {
                        ahci_base = bar5 & ~0xF;
                        cout << "Found AHCI controller at PCI " << (int)bus << ":" << (int)dev << "." << (int)func << "\n";
                        print_hex64(" AHCI Base Address (ABAR): ", ahci_base);
                    }
                }
            }
        }
    }

    if (!ahci_base) {
        cout << "No AHCI controller found. Halting.\n";
        while (1);
    }

    // Directly call the debug utility
    debug_sata_controller(ahci_base);


    cout << "System Halted.\n";
    while (1); // Halt
    return 0;
}
