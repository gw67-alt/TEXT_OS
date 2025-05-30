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

// Global variables declarations (but not initialized here)
char buffer[4096]; // Define a buffer to read/write data
size_t buffer_size = sizeof(buffer);
uint64_t ahci_base; // Will be initialized in kernel_main

/* Command implementations */
void cmd_help() {
    cout << "Available commands:\n";
    cout << "  help         - Show this help message\n";
    cout << "  clear        - Clear the screen\n";
    cout << "  pciscan      - Scan PCI devices\n";
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

/* Command processing function */
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
        } else if (cmd == "pciscan") {
            scan_pci();
        } else if (cmd == "readmem") {
            cmd_read_memory();
        } else if (cmd == "writemem") {
            cmd_write_memory();
        } else if (cmd == "driver") {
            cmd_driver();  // Add the new driver command
        }
        else {
            cout << "Unknown command: " << input << "\n";
            cout << "Type 'help' for a list of commands.\n";
        }
    }
}

/* Main kernel entry point */
extern "C" void kernel_main() {
    /* Initialize terminal interface */
    terminal_initialize();
    /* Initialize terminal I/O */
    init_terminal_io();
    /* Initialize keyboard and timer interrupts */
    init_keyboard();

    cout << "Hello, kernel World!" << '\n';
    
    ahci_base = disk_init();
    
    // Initialize the PCIe driver
    init_pcie_driver();

    cout << "Initialization complete. Start typing commands...\n";

    command_prompt();
}