#include "pcie_driver.h"
#include "iostream_wrapper.h"
#include "terminal_io.h"
#include <stdint.h>

// Forward declarations
uint8_t driver_pcie_read(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset);
uint8_t driver_memory_read(uint32_t address);
// Potentially others like driver_pcie_write, driver_memory_write if not in .h
bool parse_pcie_specification(const char* pcie_start, DriverCommand* cmd); // Also for error 7 & 8

// PCIe configuration mechanism addresses
#define PCIE_CONFIG_ADDRESS 0xCF8
#define PCIE_CONFIG_DATA    0xCFC

// Global PCIe device list (simplified for demonstration)
static PCIeDevice detected_devices[32];
static int device_count = 0;

// Port I/O functions - inline assembly implementations
static inline void outl(uint16_t port, uint32_t value) {
    asm volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline void outw(uint16_t port, uint16_t value) {
    asm volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}


static inline uint32_t inl(uint16_t port) {
    uint32_t value;
    asm volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint16_t inw(uint16_t port) {
    uint16_t value;
    asm volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}


// Custom string functions to avoid standard library dependencies
static const char* my_strstr(const char* haystack, const char* needle) {
    if (!needle || !*needle) return haystack;
    
    for (const char* h = haystack; *h; h++) {
        const char* h_temp = h;
        const char* n_temp = needle;
        
        while (*h_temp && *n_temp && *h_temp == *n_temp) {
            h_temp++;
            n_temp++;
        }
        
        if (!*n_temp) return h;
    }
    return nullptr;
}

static int my_strncmp(const char* s1, const char* s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i]) {
            return (unsigned char)s1[i] - (unsigned char)s2[i];
        }
        if (s1[i] == '\0') break;
    }
    return 0;
}

// Better string copying function
static void my_strncpy(char* dest, const char* src, size_t n) {
    size_t i = 0;
    while (i < n - 1 && src[i] != '\0' && src[i] != ':' && src[i] != ' ' && src[i] != '>') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

void init_pcie_driver() {
    cout << "Initializing PCIe Driver...\n";
    device_count = 0;
    
    // Clear device list
    for (int i = 0; i < 32; i++) {
        detected_devices[i].valid = false;
    }
    
    // Scan for PCIe devices
    pcie_scan_devices();
    cout << "PCIe Driver initialized. Found " << device_count << " devices.\n";
}

void cmd_driver() {
    char input[256];
    DriverCommand cmd;
    
    cout << "Driver Command Format:\n";
    cout << "  Memory Write: driver >> 0x[VALUE] >> 0x[ADDRESS]\n";
    cout << "  Memory Read:  driver >> read >> 0x[ADDRESS]\n";
    cout << "  PCIe Write:   driver >> 0x[VALUE] >> 0x[ADDRESS] >> pcie:[BUS]:[DEV]:[FUNC]:[OFFSET]\n";
    cout << "  PCIe Read:    driver >> read >> 0x[ADDRESS] >> pcie:[BUS]:[DEV]:[FUNC]:[OFFSET]\n";
    cout << "  List:         driver >> list\n";
    cout << "Enter driver command: ";
    
    // Read the full line including spaces
    cin >> input;
    
    // Check for list command
    if (my_strstr(input, "list") != nullptr) {
        cout << "Detected PCIe devices:\n";
        for (int i = 0; i < device_count; i++) {
            print_pcie_device_info(&detected_devices[i]);
        }
        return;
    }
    
    cout << "Debug: Received command: " << input << "\n";
    
    if (parse_driver_command(input, &cmd)) {
        if (cmd.is_read) {
            // Handle read operations
            if (cmd.use_pcie) {
                cout << "Reading from PCIe device " << (int)cmd.bus << ":" << (int)cmd.device 
                     << ":" << (int)cmd.function << " offset " << std::hex << cmd.offset << std::dec << "\n";
                uint8_t read_value = driver_pcie_read(cmd.bus, cmd.device, cmd.function, cmd.offset);
                cout << "Read value: " << std::hex << (int)read_value << std::dec << "\n";
            } else {
                cout << "Reading from memory address " << std::hex << cmd.address << std::dec << "\n";
                uint8_t read_value = driver_memory_read(cmd.address);
                cout << "Read value: " << std::hex << (int)read_value << std::dec << "\n";
            }
        } else {
            // Handle write operations
            if (cmd.use_pcie) {
                cout << "Writing " << std::hex << (int)cmd.value 
                     << " to PCIe device " << (int)cmd.bus << ":" << (int)cmd.device 
                     << ":" << (int)cmd.function << " offset " << cmd.offset << std::dec << "\n";
                driver_pcie_write(cmd.bus, cmd.device, cmd.function, cmd.offset, cmd.value);
            } else {
                cout << "Writing " << std::hex << (int)cmd.value 
                     << " to memory address " << cmd.address << std::dec << "\n";
                driver_memory_write(cmd.address, cmd.value);
            }
        }
        cout << "Driver command executed successfully.\n";
    } else {
        cout << "Invalid driver command format.\n";
        cout << "Examples:\n";
        cout << "  Write: driver >> 0xFF >> 0x1000\n";
        cout << "  Read:  driver >> read >> 0x1000\n";
        cout << "  PCIe:  driver >> 0xFF >> 0x1000 >> pcie:0:1:0:10\n";
    }
}

bool parse_driver_command(const char* input, DriverCommand* cmd) {
    // Initialize command structure
    cmd->use_pcie = false;
    cmd->is_read = false;
    cmd->value = 0;
    cmd->address = 0;
    cmd->bus = 0;
    cmd->device = 0;
    cmd->function = 0;
    cmd->offset = 0;
    
    cout << "Debug: Parsing command: '" << input << "'\n";
    
    // Skip whitespace and find "driver"
    const char* driver_pos = my_strstr(input, "driver");
    if (!driver_pos) {
        cout << "Debug: 'driver' keyword not found\n";
        return false;
    }
    
    // Find first ">>" after "driver"
    const char* first_arrow = my_strstr(driver_pos, ">>");
    if (!first_arrow) {
        cout << "Debug: No first >> found\n";
        return false;
    }
    
    // Check if this is a read command
    const char* first_param_start = first_arrow + 2;
    while (*first_param_start == ' ') first_param_start++; // Skip spaces
    
    if (my_strncmp(first_param_start, "read", 4) == 0) {
        cmd->is_read = true;
        cout << "Debug: Read command detected\n";
        
        // Find second ">>" for address
        const char* second_arrow = my_strstr(first_param_start, ">>");
        if (!second_arrow) {
            cout << "Debug: No second >> found for read command\n";
            return false;
        }
        
        // Extract address (after second >>)
        const char* addr_start = second_arrow + 2;
        while (*addr_start == ' ') addr_start++; // Skip spaces
        
        // Check for PCIe specification (third >>)
        const char* third_arrow = my_strstr(addr_start, ">>");
        const char* addr_end;
        
        if (third_arrow) {
            addr_end = third_arrow;
            cout << "Debug: Found third >>, PCIe read command detected\n";
        } else {
            // Find end of address (space, null, or newline)
            addr_end = addr_start;
            while (*addr_end && *addr_end != ' ' && *addr_end != '\n' && *addr_end != '\r') {
                addr_end++;
            }
            cout << "Debug: No third >>, memory read command\n";
        }
        
        cout << "Debug: Address section: '";
        for (const char* p = addr_start; p < addr_end && *p; p++) {
            cout << *p;
        }
        cout << "'\n";
        
        if (addr_start[0] == '0' && (addr_start[1] == 'x' || addr_start[1] == 'X')) {
            cmd->address = hex_string_to_uint32(addr_start + 2);
            cout << "Debug: Parsed address: " << std::hex << cmd->address << std::dec << "\n";
        } else {
            cout << "Debug: Invalid address format\n";
            return false;
        }
        
        // Handle PCIe specification if present
        if (third_arrow) {
            if (!parse_pcie_specification(third_arrow + 2, cmd)) {
                return false;
            }
        }
        
        cout << "Debug: Read command parsed successfully\n";
        return true;
    }
    
    // Handle write commands (original logic)
    // Extract value (after first >>)
    const char* value_start = first_param_start;
    
    // Find end of value (next >> or end of string)
    const char* value_end = my_strstr(value_start, ">>");
    if (!value_end) {
        // No second >>, treat as simple memory command
        // Find end of value by looking for space, null, or newline
        value_end = value_start;
        while (*value_end && *value_end != ' ' && *value_end != '\n' && *value_end != '\r') {
            value_end++;
        }
        
        // Extract and parse value
        if (value_start[0] == '0' && (value_start[1] == 'x' || value_start[1] == 'X')) {
            cmd->value = hex_string_to_uint8(value_start + 2);
            cout << "Debug: Parsed value: " << std::hex << (int)cmd->value << std::dec << "\n";
            
            // For simple format, value and address are the same
            cmd->address = hex_string_to_uint32(value_start + 2);
            cout << "Debug: Simple format - using value as address: " << std::hex << cmd->address << std::dec << "\n";
            return true;
        } else {
            cout << "Debug: Invalid value format in simple command\n";
            return false;
        }
    }
    
    // Extract value (between first >> and second >>)
    cout << "Debug: Value section: '";
    for (const char* p = value_start; p < value_end && *p; p++) {
        cout << *p;
    }
    cout << "'\n";
    
    if (value_start[0] == '0' && (value_start[1] == 'x' || value_start[1] == 'X')) {
        cmd->value = hex_string_to_uint8(value_start + 2);
        cout << "Debug: Parsed value: " << std::hex << (int)cmd->value << std::dec << "\n";
    } else {
        cout << "Debug: Invalid value format\n";
        return false;
    }
    
    // Extract address (after second >>)
    const char* addr_start = value_end + 2;
    while (*addr_start == ' ') addr_start++; // Skip spaces
    
    // Check for PCIe specification (third >>)
    const char* third_arrow = my_strstr(addr_start, ">>");
    const char* addr_end;
    
    if (third_arrow) {
        addr_end = third_arrow;
        cout << "Debug: Found third >>, PCIe write command detected\n";
    } else {
        // Find end of address (space, null, or newline)
        addr_end = addr_start;
        while (*addr_end && *addr_end != ' ' && *addr_end != '\n' && *addr_end != '\r') {
            addr_end++;
        }
        cout << "Debug: No third >>, memory write command\n";
    }
    
    cout << "Debug: Address section: '";
    for (const char* p = addr_start; p < addr_end && *p; p++) {
        cout << *p;
    }
    cout << "'\n";
    
    if (addr_start[0] == '0' && (addr_start[1] == 'x' || addr_start[1] == 'X')) {
        cmd->address = hex_string_to_uint32(addr_start + 2);
        cout << "Debug: Parsed address: " << std::hex << cmd->address << std::dec << "\n";
    } else {
        cout << "Debug: Invalid address format\n";
        return false;
    }
    
    // Handle PCIe specification if present
    if (third_arrow) {
        if (!parse_pcie_specification(third_arrow + 2, cmd)) {
            return false;
        }
    }
    
    cout << "Debug: Write command parsed successfully\n";
    return true;
}

// Helper function to parse PCIe specification
bool parse_pcie_specification(const char* pcie_start, DriverCommand* cmd) {
    while (*pcie_start == ' ') pcie_start++; // Skip spaces
    
    cout << "Debug: PCIe section: '";
    for (int i = 0; i < 20 && pcie_start[i] && pcie_start[i] != '\n'; i++) {
        cout << pcie_start[i];
    }
    cout << "'\n";
    
    if (my_strncmp(pcie_start, "pcie:", 5) == 0) {
        cmd->use_pcie = true;
        const char* pcie_spec = pcie_start + 5;
        
        // Parse bus:device:function:offset format
        char temp_str[16];
        int field = 0;
        int pos = 0;
        
        for (int i = 0; pcie_spec[i] != '\0' && pcie_spec[i] != '\n' && pcie_spec[i] != '\r' && field <= 3; i++) {
            if (pcie_spec[i] == ':' || (field == 3 && (pcie_spec[i] == ' ' || pcie_spec[i] == '\0'))) {
                temp_str[pos] = '\0';
                
                switch (field) {
                    case 0:
                        cmd->bus = (uint8_t)hex_string_to_uint32(temp_str);
                        cout << "Debug: Parsed bus: " << (int)cmd->bus << "\n";
                        break;
                    case 1:
                        cmd->device = (uint8_t)hex_string_to_uint32(temp_str);
                        cout << "Debug: Parsed device: " << (int)cmd->device << "\n";
                        break;
                    case 2:
                        cmd->function = (uint8_t)hex_string_to_uint32(temp_str);
                        cout << "Debug: Parsed function: " << (int)cmd->function << "\n";
                        break;
                    case 3:
                        cmd->offset = (uint16_t)hex_string_to_uint32(temp_str);
                        cout << "Debug: Parsed offset: " << std::hex << cmd->offset << std::dec << "\n";
                        break;
                }
                field++;
                pos = 0;
                
                if (pcie_spec[i] == ' ' || pcie_spec[i] == '\0') break;
            } else if (pos < 15) {
                temp_str[pos++] = pcie_spec[i];
            }
        }
        
        // Handle last field if we ended without a delimiter
        if (field == 3 && pos > 0) {
            temp_str[pos] = '\0';
            cmd->offset = (uint16_t)hex_string_to_uint32(temp_str);
            cout << "Debug: Parsed offset (final): " << std::hex << cmd->offset << std::dec << "\n";
        }
        
        return true;
    }
    
    cout << "Debug: Invalid PCIe specification format\n";
    return false;
}

uint32_t pcie_config_read32(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset) {
    uint32_t address = (1U << 31) | ((uint32_t)bus << 16) | ((uint32_t)device << 11) | 
                       ((uint32_t)function << 8) | (offset & 0xFC);
    
    outl(PCIE_CONFIG_ADDRESS, address);
    return inl(PCIE_CONFIG_DATA);
}

uint16_t pcie_config_read16(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset) {
    uint32_t address = (1U << 31) | ((uint32_t)bus << 16) | ((uint32_t)device << 11) | 
                       ((uint32_t)function << 8) | (offset & 0xFC);
    
    outl(PCIE_CONFIG_ADDRESS, address);
    return inw(PCIE_CONFIG_DATA + (offset & 2));
}

uint8_t pcie_config_read8(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset) {
    uint32_t address = (1U << 31) | ((uint32_t)bus << 16) | ((uint32_t)device << 11) | 
                       ((uint32_t)function << 8) | (offset & 0xFC);
    
    outl(PCIE_CONFIG_ADDRESS, address);
    return inb(PCIE_CONFIG_DATA + (offset & 3));
}

void pcie_config_write32(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint32_t value) {
    uint32_t address = (1U << 31) | ((uint32_t)bus << 16) | ((uint32_t)device << 11) | 
                       ((uint32_t)function << 8) | (offset & 0xFC);
    
    outl(PCIE_CONFIG_ADDRESS, address);
    outl(PCIE_CONFIG_DATA, value);
}

void pcie_config_write16(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint16_t value) {
    uint32_t address = (1U << 31) | ((uint32_t)bus << 16) | ((uint32_t)device << 11) | 
                       ((uint32_t)function << 8) | (offset & 0xFC);
    
    outl(PCIE_CONFIG_ADDRESS, address);
    outw(PCIE_CONFIG_DATA + (offset & 2), value);
}

void pcie_config_write8(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint8_t value) {
    uint32_t address = (1U << 31) | ((uint32_t)bus << 16) | ((uint32_t)device << 11) | 
                       ((uint32_t)function << 8) | (offset & 0xFC);
    
    outl(PCIE_CONFIG_ADDRESS, address);
    outb(PCIE_CONFIG_DATA + (offset & 3), value);
}

bool pcie_device_exists(uint8_t bus, uint8_t device, uint8_t function) {
    uint16_t vendor_id = pcie_config_read16(bus, device, function, PCIE_CONFIG_VENDOR_ID);
    return (vendor_id != 0xFFFF && vendor_id != 0x0000);
}

void pcie_enable_device(uint8_t bus, uint8_t device, uint8_t function) {
    uint16_t command = pcie_config_read16(bus, device, function, PCIE_CONFIG_COMMAND);
    command |= PCIE_CMD_IO_ENABLE | PCIE_CMD_MEMORY_ENABLE | PCIE_CMD_BUS_MASTER;
    pcie_config_write16(bus, device, function, PCIE_CONFIG_COMMAND, command);
}

void pcie_scan_devices() {
    device_count = 0;
    
    for (uint16_t bus = 0; bus < 256 && device_count < 32; bus++) {
        for (uint8_t device = 0; device < 32 && device_count < 32; device++) {
            for (uint8_t function = 0; function < 8 && device_count < 32; function++) {
                if (pcie_device_exists(bus, device, function)) {
                    PCIeDevice* dev = &detected_devices[device_count];
                    dev->bus = bus;
                    dev->device = device;
                    dev->function = function;
                    dev->vendor_id = pcie_config_read16(bus, device, function, PCIE_CONFIG_VENDOR_ID);
                    dev->device_id = pcie_config_read16(bus, device, function, PCIE_CONFIG_DEVICE_ID);
                    dev->class_code = pcie_config_read32(bus, device, function, PCIE_CONFIG_CLASS_CODE);
                    
                    // Read BARs
                    for (int i = 0; i < 6; i++) {
                        dev->bar[i] = pcie_config_read32(bus, device, function, PCIE_CONFIG_BAR0 + (i * 4));
                    }
                    
                    dev->valid = true;
                    device_count++;
                }
            }
        }
    }
}

uint8_t driver_memory_read(uint32_t address) {
    return *((volatile uint8_t*)address);
}

void driver_memory_write(uint32_t address, uint8_t value) {
    *((volatile uint8_t*)address) = value;
}

uint8_t driver_pcie_read(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset) {
    if (pcie_device_exists(bus, device, function)) {
        return pcie_config_read8(bus, device, function, offset);
    } else {
        cout << "PCIe device " << (int)bus << ":" << (int)device << ":" << (int)function << " not found!\n";
        return 0xFF;
    }
}

void driver_pcie_write(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint8_t value) {
    if (pcie_device_exists(bus, device, function)) {
        pcie_config_write8(bus, device, function, offset, value);
    } else {
        cout << "PCIe device " << (int)bus << ":" << (int)device << ":" << (int)function << " not found!\n";
    }
}

void print_pcie_device_info(const PCIeDevice* dev) {
    cout << "  Bus " << (int)dev->bus << ", Device " << (int)dev->device 
         << ", Function " << (int)dev->function;
    cout << " - Vendor: " << std::hex << dev->vendor_id 
         << ", Device: " << dev->device_id;
    cout << ", Class: " << (dev->class_code >> 8) << std::dec << "\n";
}

// Utility functions
uint8_t hex_char_to_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

uint32_t hex_string_to_uint32(const char* hex_str) {
    uint32_t result = 0;
    for (int i = 0; hex_str[i] != '\0' && hex_str[i] != ' ' && hex_str[i] != '>' && hex_str[i] != ':'; i++) {
        result = (result << 4) | hex_char_to_value(hex_str[i]);
    }
    return result;
}

uint8_t hex_string_to_uint8(const char* hex_str) {
    uint8_t result = 0;
    for (int i = 0; hex_str[i] != '\0' && hex_str[i] != ' ' && hex_str[i] != '>' && i < 2; i++) {
        result = (result << 4) | hex_char_to_value(hex_str[i]);
    }
    return result;
}
