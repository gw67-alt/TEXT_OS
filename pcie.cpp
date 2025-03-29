#include <iostream>
#include <cstdint>
#include <iomanip>

// Define some common PCIe configuration register offsets
constexpr uint16_t PCI_VENDOR_ID        = 0x00;
constexpr uint16_t PCI_DEVICE_ID        = 0x02;
constexpr uint16_t PCI_COMMAND          = 0x04;
constexpr uint16_t PCI_STATUS           = 0x06;
constexpr uint16_t PCI_REVISION_ID      = 0x08;
constexpr uint16_t PCI_CLASS_CODE       = 0x09;
constexpr uint16_t PCI_HEADER_TYPE      = 0x0E;
constexpr uint16_t PCI_BAR0             = 0x10;
constexpr uint16_t PCI_SECONDARY_BUS    = 0x19;

// x86-specific: I/O port addresses for PCI configuration access
constexpr uint16_t PCI_CONFIG_ADDRESS   = 0xCF8;
constexpr uint16_t PCI_CONFIG_DATA      = 0xCFC;

// x86-specific: I/O port access functions using inline assembly
inline uint32_t inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

inline void outl(uint16_t port, uint32_t value) {
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

inline uint16_t inw(uint16_t port) {
    uint16_t value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

// Construct the x86 PCI configuration address for Type 1 access
uint32_t pci_config_address(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset) {
    return (0x80000000 | 
            (static_cast<uint32_t>(bus) << 16) | 
            (static_cast<uint32_t>(device) << 11) | 
            (static_cast<uint32_t>(function) << 8) | 
            (offset & 0xFC));
}

// Function to read a byte from PCI configuration space
uint8_t pci_read_byte(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset) {
    outl(PCI_CONFIG_ADDRESS, pci_config_address(bus, device, function, offset));
    return inb(PCI_CONFIG_DATA + (offset & 0x3));
}

// Function to read a word (16 bits) from PCI configuration space
uint16_t pci_read_word(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset) {
    outl(PCI_CONFIG_ADDRESS, pci_config_address(bus, device, function, offset));
    return inw(PCI_CONFIG_DATA + (offset & 0x2));
}

// Function to read a double word (32 bits) from PCI configuration space
uint32_t pci_read_dword(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset) {
    outl(PCI_CONFIG_ADDRESS, pci_config_address(bus, device, function, offset));
    return inl(PCI_CONFIG_DATA);
}

// Function to write a byte to PCI configuration space
void pci_write_byte(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint8_t value) {
    outl(PCI_CONFIG_ADDRESS, pci_config_address(bus, device, function, offset));
    outb(PCI_CONFIG_DATA + (offset & 0x3), value);
}

// Function to write a word (16 bits) to PCI configuration space
void pci_write_word(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint16_t value) {
    outl(PCI_CONFIG_ADDRESS, pci_config_address(bus, device, function, offset));
    outw(PCI_CONFIG_DATA + (offset & 0x2), value);
}

// Function to write a double word (32 bits) to PCI configuration space
void pci_write_dword(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint32_t value) {
    outl(PCI_CONFIG_ADDRESS, pci_config_address(bus, device, function, offset));
    outl(PCI_CONFIG_DATA, value);
}

// x86-specific: Map physical memory for MMIO access
// Note: This requires OS-level support or direct manipulation of page tables
// This implementation is a placeholder that would need to be adapted based on your environment
volatile uint32_t* map_physical_memory(uintptr_t physical_address, size_t size) {
    // In a true bare-metal x86 environment, you would:
    // 1. Create or modify appropriate page table entries
    // 2. Set the appropriate cache attributes (usually WC - Write Combining)
    // 3. Return a pointer to the virtual address
    
    #ifdef __linux__
    // Example for Linux if you're using it with proper permissions:
    // You would need: #include <sys/mman.h> and #include <fcntl.h>
    // int fd = open("/dev/mem", O_RDWR | O_SYNC);
    // if (fd == -1) return nullptr;
    // void* mapped = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, physical_address);
    // return static_cast<volatile uint32_t*>(mapped);
    #endif
    
    return nullptr;
}

// Function to enumerate a single device/function
void enumerate_function(uint8_t bus, uint8_t device, uint8_t function) {
    uint16_t vendor_id = pci_read_word(bus, device, function, PCI_VENDOR_ID);
    if (vendor_id == 0xFFFF) {
        return; // No device present
    }

    uint16_t device_id = pci_read_word(bus, device, function, PCI_DEVICE_ID);
    
    // Read class code (base class, sub class, and interface)
    uint8_t class_code_base = pci_read_byte(bus, device, function, PCI_CLASS_CODE + 2);
    uint8_t class_code_sub = pci_read_byte(bus, device, function, PCI_CLASS_CODE + 1);
    uint8_t class_code_interface = pci_read_byte(bus, device, function, PCI_CLASS_CODE);
    
    uint8_t header_type = pci_read_byte(bus, device, function, PCI_HEADER_TYPE);
    uint8_t revision_id = pci_read_byte(bus, device, function, PCI_REVISION_ID);

  
    // Display human-readable class information
    const char* class_name = "Unknown";
    switch (class_code_base) {
        case 0x00: class_name = "Unclassified"; break;
        case 0x01: class_name = "Mass Storage Controller"; break;
        case 0x02: class_name = "Network Controller"; break;
        case 0x03: class_name = "Display Controller"; break;
        case 0x04: class_name = "Multimedia Controller"; break;
        case 0x05: class_name = "Memory Controller"; break;
        case 0x06: class_name = "Bridge Device"; break;
        case 0x07: class_name = "Simple Communication Controller"; break;
        case 0x08: class_name = "Base System Peripheral"; break;
        case 0x09: class_name = "Input Device Controller"; break;
        case 0x0A: class_name = "Docking Station"; break;
        case 0x0B: class_name = "Processor"; break;
        case 0x0C: class_name = "Serial Bus Controller"; break;
        case 0x0D: class_name = "Wireless Controller"; break;
        case 0x0E: class_name = "Intelligent Controller"; break;
        case 0x0F: class_name = "Satellite Communication Controller"; break;
        case 0x10: class_name = "Encryption Controller"; break;
        case 0x11: class_name = "Signal Processing Controller"; break;
        case 0x12: class_name = "Processing Accelerator"; break;
        case 0x13: class_name = "Non-Essential Instrumentation"; break;
        case 0x40: class_name = "Co-Processor"; break;
        case 0xFF: class_name = "Unassigned Class"; break;
    }

    // If it's a PCI-to-PCI bridge, recursively scan the secondary bus
    if (class_code_base == 0x06 && class_code_sub == 0x04) {
        uint8_t secondary_bus = pci_read_byte(bus, device, function, PCI_SECONDARY_BUS);
        enumerate_bus(secondary_bus);
    }
    
    // For each device, print out the base address registers (BARs)
    for (int i = 0; i < 6; i++) {
        uint32_t bar = pci_read_dword(bus, device, function, PCI_BAR0 + i*4);
        if (bar != 0) {
            bool is_io = (bar & 0x1);
            if (is_io) {
                // I/O space BAR
                uint16_t io_addr = bar & 0xFFFFFFFC;
            } else {
                // Memory space BAR
                uint32_t mem_addr = bar & 0xFFFFFFF0;
                uint8_t type = (bar & 0x6) >> 1;
                const char* type_str;
                switch (type) {
                    case 0: type_str = "32-bit"; break;
                    case 1: type_str = "Reserved"; break;
                    case 2: type_str = "64-bit"; break;
                    default: type_str = "Reserved"; break;
                }
                
                bool prefetchable = (bar & 0x8);
                
                // For 64-bit BARs, we need to read the next BAR as well
                if (type == 2) {
                    i++;
                    if (i < 6) {
                        uint32_t bar_high = pci_read_dword(bus, device, function, PCI_BAR0 + i*4);
                        uint64_t full_addr = (static_cast<uint64_t>(bar_high) << 32) | mem_addr;
                    }
                }
            }
        }
    }
    
    std::cout << std::endl;
}

// Forward declaration
void enumerate_bus(uint8_t bus);

// Function to enumerate all devices on a given bus
void enumerate_bus(uint8_t bus) {
    for (uint8_t device = 0; device < 32; ++device) {
        uint16_t vendor_id = pci_read_word(bus, device, 0, PCI_VENDOR_ID);
        if (vendor_id == 0xFFFF) {
            continue; // No device present
        }

        uint8_t header_type = pci_read_byte(bus, device, 0, PCI_HEADER_TYPE);
        uint8_t max_functions = ((header_type & 0x80) != 0) ? 8 : 1;
        
        for (uint8_t function = 0; function < max_functions; ++function) {
            if (function > 0) {
                // Check if the function exists
                vendor_id = pci_read_word(bus, device, function, PCI_VENDOR_ID);
                if (vendor_id == 0xFFFF) {
                    continue;
                }
            }
            enumerate_function(bus, device, function);
        }
    }
}

// Example function to enable memory and I/O access for a device
void enable_device(uint8_t bus, uint8_t device, uint8_t function) {
    uint16_t command = pci_read_word(bus, device, function, PCI_COMMAND);
    command |= 0x3; // Enable memory and I/O space access
    pci_write_word(bus, device, function, PCI_COMMAND, command);
          
}

// Example function to enable bus mastering for a device
void enable_bus_mastering(uint8_t bus, uint8_t device, uint8_t function) {
    uint16_t command = pci_read_word(bus, device, function, PCI_COMMAND);
    command |= 0x4; // Enable bus master
    pci_write_word(bus, device, function, PCI_COMMAND, command);
              << static_cast<int>(bus) << ":" 
              << static_cast<int>(device) << ":" 
              << static_cast<int>(function) << std::endl;
}

int main() {
    
    // Start enumeration from bus 0
    enumerate_bus(0);
    
    
    // Example of how to enable a specific device if found
    // Note: This is just an example - you'd need to identify the specific device you want to configure
    uint16_t vendor_id = pci_read_word(0, 0, 0, PCI_VENDOR_ID);
    if (vendor_id != 0xFFFF) {
        enable_device(0, 0, 0);
        enable_bus_mastering(0, 0, 0);
        
        // Read back command register to verify
        uint16_t command = pci_read_word(0, 0, 0, PCI_COMMAND);
    } else {
    }
    
    return 0;
}