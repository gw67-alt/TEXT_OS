#include "terminal_hooks.h"
#include "terminal_io.h"
#include "iostream_wrapper.h"
#include "interrupts.h"
#include "hardware_specs.h"
#include "stdlib_hooks.h"
#include "pci.h"
#include "sata.h"
#include "test.h"
#include "test2.h"
#include "disk.h"
#include "pcie_driver.h"  // Add the new PCIe driver header
#include <stdio.h>        // For snprintf

// Global variables declarations (but not initialized here)
char buffer[4096]; // Define a buffer to read/write data
size_t buffer_size = sizeof(buffer);
uint64_t ahci_base; // Will be initialized in kernel_main

// Forward declarations for command functions
void cmd_help();
void cmd_read_memory();
void cmd_write_memory();
void cmd_sata_test();
void command_prompt();

// --- Command Implementations ---

void cmd_help() {
    cout << "Available commands:\n";
    cout << "  help         - Show this help message\n";
    cout << "  clear        - Clear the screen\n";
    cout << "  pciescan     - Scan PCI devices\n";
    cout << "  cpu          - Display CPU information\n";
    cout << "  memory       - Display memory configuration\n";
    cout << "  cache        - Display cache information\n";
    cout << "  topology     - Display CPU topology\n";
    cout << "  features     - Display CPU features\n";
    cout << "  pstates      - Display P-States information\n";
    cout << "  full         - Display all hardware information\n";
    cout << "  fs           - Run filesystem debug\n";
    cout << "  program1     - Run test program 1\n";
    cout << "  program2     - Run test program 2\n";
    cout << "  read         - Read test file\n";
    cout << "  write        - Write to test file\n";
    cout << "  readmem      - Read memory\n";
    cout << "  writemem     - Write to memory\n";
    cout << "  driver       - Driver command (memory/PCIe write)\n";
    cout << "  dcfgtest     - OS driver test\n";
    cout << "  satatest     - Run a basic SATA port 0 test\n";
}

void cmd_read_memory() {
    uint32_t address = 0;
    size_t size = 0;
    char address_str[20];
    char size_str[20];

    cout << "Enter memory address to read (in hexadecimal): 0x";
    cin >> address_str;

    // Convert hex string to uint32_t manually
    for (int i = 0; address_str[i] != '\0'; i++) {
        char c = address_str[i];
        address = address << 4; // Shift left by 4 bits (multiply by 16)
        
        if (c >= '0' && c <= '9') {
            address |= (c - '0');
        } else if (c >= 'a' && c <= 'f') {
            address |= (c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            address |= (c - 'A' + 10);
        }
    }
    
    cout << "Enter number of bytes to read: ";
    cin >> size_str;
    
    // Convert decimal string to size_t
    for (int i = 0; size_str[i] != '\0'; i++) {
        if (size_str[i] >= '0' && size_str[i] <= '9') {
            size = size * 10 + (size_str[i] - '0');
        }
    }

    cout << "Memory at " << std::hex << address << ":\n";
    for (size_t i = 0; i < size; ++i) {
        cout << std::hex << (int)*((uint8_t*)(address + i)) << " ";
    }
    cout << std::dec << "\n";
}

void cmd_write_memory() {
    uint32_t address = 0;
    uint8_t value = 0;
    char address_str[20];
    char value_str[5];

    cout << "Enter memory address to write to (in hexadecimal): 0x";
    cin >> address_str;
    
    // Convert hex string to uint32_t manually
    for (int i = 0; address_str[i] != '\0'; i++) {
        char c = address_str[i];
        address = address << 4; // Shift left by 4 bits (multiply by 16)
        
        if (c >= '0' && c <= '9') {
            address |= (c - '0');
        } else if (c >= 'a' && c <= 'f') {
            address |= (c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            address |= (c - 'A' + 10);
        }
    }
    
    cout << "Enter byte value to write (in hexadecimal): 0x";
    cin >> value_str;
    
    // Convert hex string to uint8_t
    for (int i = 0; value_str[i] != '\0'; i++) {
        char c = value_str[i];
        value = value << 4; // Shift left by 4 bits (multiply by 16)
        
        if (c >= '0' && c <= '9') {
            value |= (c - '0');
        } else if (c >= 'a' && c <= 'f') {
            value |= (c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            value |= (c - 'A' + 10);
        }
    }

    *((uint8_t*)address) = value;

    cout << "Value " << std::hex << (int)value << " written to address " << address << std::dec << "\n";
}

// Helper to convert addresses to strings for driver_cfg
void to_hex_str(uint64_t n, char* out) {
    const char* hex_chars = "0123456789ABCDEF";
    out[0] = '0';
    out[1] = 'x';
    // Handle the case of 0 separately
    if (n == 0) {
        out[2] = '0';
        out[3] = '\0';
        return;
    }
    // Buffer to hold hex chars in reverse
    char rev_hex[17];
    int i = 0;
    while (n > 0) {
        rev_hex[i++] = hex_chars[n % 16];
        n /= 16;
    }
    // Now write them to the output string in correct order
    int j = 2;
    while (i > 0) {
        out[j++] = rev_hex[--i];
    }
    out[j] = '\0';
}


void cmd_sata_test() {
    if (ahci_base == 0) {
        cout << "AHCI base not initialized. Cannot run test.\n";
        return;
    }

    cout << "--- Starting SATA Test using driver_cfg ---\n";

    // In a real driver, you'd get B:D:F from pcie_scan
    const char* ahci_device_pci = "pcie:0:31:2:0"; // Common for Intel ICH/PCH

    // Addresses for our structures in memory
    const uint32_t cmd_list_addr = 0x8000;
    const uint32_t cmd_table_addr = 0x9000;
    const uint32_t fis_addr = 0xA000;

    // --- 1. Setup Memory Structures using direct memory writes ---
    // We need to write the Command Table address into the Command List.

    // Command Header 0 in Command List points to our Command Table
    // Offset 0: Command FIS Length (5 dwords), C bit, PRDTL length
    *((volatile uint32_t*)(cmd_list_addr + 0)) = (1 << 16) | 5;
    // Offset 4: Physical Region Descriptor Table Byte Count (0 for no data)
    *((volatile uint32_t*)(cmd_list_addr + 4)) = 0;
    // Offset 8: Command Table Base Address
    *((volatile uint32_t*)(cmd_list_addr + 8)) = cmd_table_addr;
    *((volatile uint32_t*)(cmd_list_addr + 12)) = 0; // Upper 32 bits

    // Setup the Command Table with an IDENTIFY DEVICE command
    // This is the Command FIS part of the table
    volatile uint8_t* cfis = (volatile uint8_t*)cmd_table_addr;
    cfis[0] = 0x27;       // FIS_TYPE_REG_H2D
    cfis[1] = 0x80;       // C=1, command
    cfis[2] = 0xEC;       // COMMAND_IDENTIFY_DEVICE
    cfis[3] = 0;          // Features
    cfis[4] = 0;          // Device

    cout << "Memory structures for IDENTIFY DEVICE prepared.\n";

    // --- 2. Use driver_cfg to configure the AHCI Port 0 ---
    bool success;
    uint8_t dummy_reads[16];
    int num_reads;
    char cmd_str[256];
    char addr_str[20];
    char val_str[20];

    // Build the command strings dynamically
    // P0CLB (Port 0 Command List Base Address) @ AHCI_BASE + 0x100
    to_hex_str(ahci_base + 0x100, addr_str);
    to_hex_str(cmd_list_addr, val_str);
    snprintf(cmd_str, sizeof(cmd_str), "driver >> %s >> %s >> %s", val_str, addr_str, ahci_device_pci);
    cout << "Executing: " << cmd_str << "\n";
    driver_cfg(cmd_str, &success, dummy_reads, 16, &num_reads);

    // P0FB (Port 0 FIS Base Address) @ AHCI_BASE + 0x108
    to_hex_str(ahci_base + 0x108, addr_str);
    to_hex_str(fis_addr, val_str);
    snprintf(cmd_str, sizeof(cmd_str), "driver >> %s >> %s >> %s", val_str, addr_str, ahci_device_pci);
    cout << "Executing: " << cmd_str << "\n";
    driver_cfg(cmd_str, &success, dummy_reads, 16, &num_reads);
    
    // P0CMD (Port 0 Command and Status) @ AHCI_BASE + 0x118
    // Enable FIS Receive (FRE, bit 4), and Start (ST, bit 0)
    to_hex_str(ahci_base + 0x118, addr_str);
    snprintf(cmd_str, sizeof(cmd_str), "driver >> 0x11 >> %s >> %s", addr_str, ahci_device_pci);
    cout << "Executing: " << cmd_str << "\n";
    driver_cfg(cmd_str, &success, dummy_reads, 16, &num_reads);

    cout << "Port configured. Issuing command...\n";

    // --- 3. Issue the command by writing to Port 0 Command Issue register ---
    // P0CI (Port 0 Command Issue) @ AHCI_BASE + 0x138
    to_hex_str(ahci_base + 0x138, addr_str);
    snprintf(cmd_str, sizeof(cmd_str), "driver >> 0x1 >> %s >> %s", addr_str, ahci_device_pci);
    cout << "Executing: " << cmd_str << "\n";
    driver_cfg(cmd_str, &success, dummy_reads, 16, &num_reads);

    if (success) {
        cout << "SUCCESS: IDENTIFY DEVICE command issued to Port 0.\n";
        cout << "In a full driver, you would now poll P0CI until the bit is cleared,\n";
        cout << "then read the response from the FIS at 0x" << std::hex << fis_addr << std::dec << "\n";
    } else {
        cout << "FAILURE: Could not issue command via driver_cfg.\n";
    }

    // --- 4. Cleanup (Stop the port by clearing ST bit in P0CMD) ---
    to_hex_str(ahci_base + 0x118, addr_str);
    snprintf(cmd_str, sizeof(cmd_str), "driver >> 0x10 >> %s >> %s", addr_str, ahci_device_pci); // Clear ST (bit 0), leave FRE (bit 4)
    cout << "Executing cleanup: " << cmd_str << "\n";
    driver_cfg(cmd_str, &success, dummy_reads, 16, &num_reads);

    cout << "--- SATA Test Finished ---\n";
}


// --- Main Command Processing Function ---

void command_prompt() {
    char input[MAX_COMMAND_LENGTH + 1]; // Add 1 for null terminator
    
    /* Display initial prompt */
    while (true) {
        cout << "> ";

        // Safely read input and null-terminate
        cin >> input;
        input[MAX_COMMAND_LENGTH] = '\0'; // Ensure null termination

        StringRef cmd(input); // Create a StringRef from the input

        if (cmd == "help") {
            cmd_help();
        } else if (cmd == "clear") {
            clear_screen();
        } else if (cmd == "cpu") {
            cmd_cpu();
        } else if (cmd == "memory") {
            cmd_memory();
        } else if (cmd == "cache") {
            cmd_cache();
        } else if (cmd == "topology") {
            cmd_topology();
        } else if (cmd == "features") {
            cmd_features();
        } else if (cmd == "pstates") {
            cmd_pstates();
        } else if (cmd == "full") {
            cmd_full();
        } else if (cmd == "program1") {
            print_prog();
        } else if (cmd == "program2") {
            print_prog2();
        } else if (cmd == "pciescan") {
            scan_pci();
        } else if (cmd == "readmem") {
            cmd_read_memory();
        } else if (cmd == "writemem") {
            cmd_write_memory();
        } else if (cmd == "dcfgtest") {
            bool success;
            uint8_t dummy_reads[16];
            int num_reads;
            cout << "Formats: driver >> [list | read | 0xVALUE] >> 0xADDRESS [>> pcie:B:D:F:O]\n";
            driver_cfg("driver >> 0xFF >> 0xFF >> pcie:0:0:0:0;driver >> read >> 0x55 >> pcie:0:1:0:0;driver >> 0xFF >> 0xFF >> pcie:0:0:0:0", &success, dummy_reads, 16, &num_reads);
        } else if (cmd == "satatest") {
            cmd_sata_test();
        }
        else {
            cout << "Unknown command: " << input << "\n";
            cout << "Type 'help' for a list of commands.\n";
        }
    }
}

// --- Main kernel entry point ---

extern "C" void kernel_main() {
    /* Initialize terminal interface */
    terminal_initialize();
    /* Initialize terminal I/O */
    init_terminal_io();
    /* Initialize keyboard and timer interrupts */
    init_keyboard();

    cout << "Hello, kernel World!" << '\n';
    
    // This function must find the AHCI controller and return its base address
    ahci_base = disk_init(); 
    
    // Initialize the PCIe driver
    init_pcie_driver();

    cout << "Initialization complete. Start typing commands...\n";

    command_prompt();
}