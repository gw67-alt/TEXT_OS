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
}

/* File operation commands */
void cmd_read_file() {
    cout << "Reading file TEST.TXT...\n";
    
}

void cmd_write_file() {
    cout << "Enter text to write to TEST.TXT (max 4096 chars): ";
    cin >> buffer;
    
    size_t write_size = strlen(buffer);
    if (write_size > buffer_size) {
        write_size = buffer_size;
    }
    
    
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
        } else if (cmd == "read") {
            cmd_read_file();
        } else if (cmd == "write") {
            cmd_write_file();
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

    cout << "Initialization complete. Start typing commands...\n";

    command_prompt();
}