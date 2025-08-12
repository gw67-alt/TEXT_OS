
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
#include "dma_memory.h"


// Global variables declarations (but not initialized here)
char buffer[4096]; // Define a buffer to read/write data
size_t buffer_size = sizeof(buffer);
uint64_t ahci_base; // Will be initialized in kernel_main

DMAManager dma_manager;


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

    cout << "  program1     - Run test program 1\n";
    cout << "  program2     - Run test program 2\n";
    cout << "  read         - Read test file\n";
    cout << "  write        - Write to test file\n";
    cout << "  dma          - DMA menu\n";

    cout << "  dmadump      - DMA dump\n";

}
// Add to your kernel.cpp command implementations section
// Helper function to parse hex input
uint64_t parse_hex_input() {
    char hex_str[20];
    cin >> hex_str;
    
    uint64_t result = 0;
    for (int i = 0; hex_str[i] != '\0'; i++) {
        char c = hex_str[i];
        result = result << 4;
        
        if (c >= '0' && c <= '9') {
            result |= (c - '0');
        } else if (c >= 'a' && c <= 'f') {
            result |= (c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            result |= (c - 'A' + 10);
        }
    }
    return result;
}

// Helper function to parse decimal input
size_t parse_decimal_input() {
    char dec_str[20];
    cin >> dec_str;
    
    size_t result = 0;
    for (int i = 0; dec_str[i] != '\0'; i++) {
        if (dec_str[i] >= '0' && dec_str[i] <= '9') {
            result = result * 10 + (dec_str[i] - '0');
        }
    }
    return result;
}

// Complete DMA test function
void cmd_dma_test() {
    cout << "=== DMA Memory Editor ===\n";
    cout << "1. Read Memory Block\n";
    cout << "2. Write Memory Block\n";
    cout << "3. Memory Dump\n";
    cout << "4. Pattern Fill\n";
    cout << "5. Memory Copy\n";
    cout << "6. DMA Channel Status\n";
    cout << "7. Performance Test\n";
    cout << "Enter choice: ";
    
    char choice[10];
    cin >> choice;
    
    switch(choice[0]) {
        case '1': {
            cout << "=== DMA Read Memory Block ===\n";
            cout << "Enter source address (hex): 0x";
            uint64_t addr = parse_hex_input();
            
            cout << "Enter size (bytes): ";
            size_t size = parse_decimal_input();
            
            if (size > 4096) {
                cout << "Size too large (max 4096 bytes)\n";
                break;
            }
            
            void* buffer = dma_manager.allocate_dma_buffer(size);
            if (!buffer) {
                cout << "Failed to allocate DMA buffer\n";
                break;
            }
            
            cout << "Starting DMA read from 0x";
            // Print hex address manually
            char hex_output[17];
            uint64_t temp_addr = addr;
            int pos = 15;
            hex_output[16] = '\0';
            
            do {
                int digit = temp_addr & 0xF;
                hex_output[pos--] = (digit < 10) ? ('0' + digit) : ('A' + digit - 10);
                temp_addr >>= 4;
            } while (temp_addr > 0 && pos >= 0);
            
            while (pos >= 0) {
                hex_output[pos--] = '0';
            }
            cout << hex_output << "...\n";
            
            if (dma_manager.read_memory_dma(addr, buffer, size)) {
                cout << "DMA read successful!\n";
                cout << "Data contents (first 64 bytes):\n";
                
                uint8_t* data = (uint8_t*)buffer;
                size_t display_size = (size > 64) ? 64 : size;
                
                for (size_t i = 0; i < display_size; i += 16) {
                    cout << "  ";
                    for (size_t j = 0; j < 16 && (i + j) < display_size; j++) {
                        uint8_t byte = data[i + j];
                        char hex_byte[3];
                        hex_byte[0] = (byte >> 4) < 10 ? ('0' + (byte >> 4)) : ('A' + (byte >> 4) - 10);
                        hex_byte[1] = (byte & 0xF) < 10 ? ('0' + (byte & 0xF)) : ('A' + (byte & 0xF) - 10);
                        hex_byte[2] = '\0';
                        cout << hex_byte << " ";
                    }
                    cout << "\n";
                }
            } else {
                cout << "DMA read failed\n";
            }
            
            dma_manager.free_dma_buffer(buffer);
            break;
        }
        
        case '2': {
            cout << "=== DMA Write Memory Block ===\n";
            cout << "Enter destination address (hex): 0x";
            uint64_t addr = parse_hex_input();
            
            cout << "Enter data pattern (hex byte): 0x";
            uint64_t pattern = parse_hex_input();
            uint8_t byte_pattern = (uint8_t)(pattern & 0xFF);
            
            cout << "Enter size (bytes): ";
            size_t size = parse_decimal_input();
            
            if (size > 4096) {
                cout << "Size too large (max 4096 bytes)\n";
                break;
            }
            
            void* buffer = dma_manager.allocate_dma_buffer(size);
            if (!buffer) {
                cout << "Failed to allocate DMA buffer\n";
                break;
            }
            
            // Fill buffer with pattern
            uint8_t* data = (uint8_t*)buffer;
            for (size_t i = 0; i < size; i++) {
                data[i] = byte_pattern;
            }
            
            cout << "Writing pattern 0x";
            char hex_byte[3];
            hex_byte[0] = (byte_pattern >> 4) < 10 ? ('0' + (byte_pattern >> 4)) : ('A' + (byte_pattern >> 4) - 10);
            hex_byte[1] = (byte_pattern & 0xF) < 10 ? ('0' + (byte_pattern & 0xF)) : ('A' + (byte_pattern & 0xF) - 10);
            hex_byte[2] = '\0';
            cout << hex_byte << " to memory...\n";
            
            if (dma_manager.write_memory_dma(addr, buffer, size)) {
                cout << "DMA write successful!\n";
            } else {
                cout << "DMA write failed\n";
            }
            
            dma_manager.free_dma_buffer(buffer);
            break;
        }
        
        case '3': {
            cout << "=== DMA Memory Dump ===\n";
            cout << "Enter start address (hex): 0x";
            uint64_t addr = parse_hex_input();
            
            cout << "Enter dump size (bytes): ";
            size_t size = parse_decimal_input();
            
            if (size > 2048) {
                cout << "Size too large for display (max 2048 bytes)\n";
                break;
            }
            
            dma_manager.dump_memory_region(addr, size);
            break;
        }
        
        case '4': {
            cout << "=== DMA Pattern Fill ===\n";
            cout << "Enter destination address (hex): 0x";
            uint64_t addr = parse_hex_input();
            
            cout << "Enter pattern (hex byte): 0x";
            uint64_t pattern = parse_hex_input();
            uint8_t byte_pattern = (uint8_t)(pattern & 0xFF);
            
            cout << "Enter size (bytes): ";
            size_t size = parse_decimal_input();
            
            if (dma_manager.pattern_fill(addr, byte_pattern, size)) {
                cout << "Pattern fill completed successfully\n";
            } else {
                cout << "Pattern fill failed\n";
            }
            break;
        }
        
        case '5': {
            cout << "=== DMA Memory Copy ===\n";
            cout << "Enter source address (hex): 0x";
            uint64_t src_addr = parse_hex_input();
            
            cout << "Enter destination address (hex): 0x";
            uint64_t dst_addr = parse_hex_input();
            
            cout << "Enter size (bytes): ";
            size_t size = parse_decimal_input();
            
            cout << "Copying " << (int)size << " bytes via DMA...\n";
            
            if (dma_manager.memory_copy(src_addr, dst_addr, size)) {
                cout << "Memory copy completed successfully\n";
            } else {
                cout << "Memory copy failed\n";
            }
            break;
        }
        
        case '6': {
            cout << "=== DMA Channel Status ===\n";
            dma_manager.show_channel_status();
            break;
        }
        
        case '7': {
            cout << "=== DMA Performance Test ===\n";
            cout << "Running DMA performance benchmark...\n";
            
            const size_t test_size = 1024;
            uint64_t test_addr = 0x100000;  // 1MB mark
            
            void* src_buffer = dma_manager.allocate_dma_buffer(test_size);
            void* dst_buffer = dma_manager.allocate_dma_buffer(test_size);
            
            if (src_buffer && dst_buffer) {
                // Fill source with test data
                uint8_t* src_data = (uint8_t*)src_buffer;
                for (size_t i = 0; i < test_size; i++) {
                    src_data[i] = (uint8_t)(i & 0xFF);
                }
                
                cout << "Testing DMA read performance...\n";
                if (dma_manager.read_memory_dma(test_addr, dst_buffer, test_size)) {
                    cout << "Read test completed\n";
                }
                
                cout << "Testing DMA write performance...\n";
                if (dma_manager.write_memory_dma(test_addr, src_buffer, test_size)) {
                    cout << "Write test completed\n";
                }
                
                cout << "Testing memory-to-memory copy...\n";
                if (dma_manager.memory_copy(test_addr, test_addr + test_size, test_size)) {
                    cout << "Copy test completed\n";
                }
                
                cout << "Performance test completed successfully\n";
            } else {
                cout << "Failed to allocate test buffers\n";
            }
            
            if (src_buffer) dma_manager.free_dma_buffer(src_buffer);
            if (dst_buffer) dma_manager.free_dma_buffer(dst_buffer);
            break;
        }
        
        default:
            cout << "Invalid choice\n";
            break;
    }
}

// Enhanced quick DMA dump with better input parsing
void cmd_dma_dump_quick() {
    cout << "Quick DMA Memory Dump\n";
    cout << "Enter address: 0x";
    uint64_t addr = parse_hex_input();
    
    cout << "Enter size (default 256): ";
    char size_input[20];
    cin >> size_input;
    
    size_t size = 256;  // default
    if (size_input[0] != '\0') {
        size = 0;
        for (int i = 0; size_input[i] != '\0'; i++) {
            if (size_input[i] >= '0' && size_input[i] <= '9') {
                size = size * 10 + (size_input[i] - '0');
            }
        }
    }
    
    if (size > 2048) {
        cout << "Size limited to 2048 bytes for display\n";
        size = 2048;
    }
    
    dma_manager.dump_memory_region(addr, size);
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
        } else if (cmd == "dma") {
            cmd_dma_test();
        } else if (cmd == "dmadump") {
            // Quick DMA memory dump command
            cout << "Enter address: 0x";
            char addr_str[20];
            cin >> addr_str;
            uint64_t addr = 0; // Parse hex string to addr
            dma_manager.dump_memory_region(addr, 256); // Dump 256 bytes
        }
        else {
            cout << "Unknown command: " << input << "\n";
            cout << "Type 'help' for a list of commands.\n";
        }
    }
}




// Update kernel_main() to initialize new systems:
extern "C" void kernel_main() {
    terminal_initialize();
    init_terminal_io();
    init_keyboard();
    
    cout << "Hello, kernel World!" << '\n';
    
    // Initialize DMA system
    uint64_t dma_base = 0xFED00000; // Example DMA controller base address
    if (dma_manager.initialize(dma_base)) {
        cout << "DMA Manager initialized successfully\n";
    }
    
    ahci_base = disk_init();
    
    cout << "Available commands: help, clear, dma, fs, dmadump, ...\n";
    cout << "Initialization complete. Start typing commands...\n";
    
    command_prompt();
}

