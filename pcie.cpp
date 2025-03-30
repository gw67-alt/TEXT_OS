#include "kernel.h"
#include "terminal_hooks.h"
#include "terminal_io.h"
#include "iostream_wrapper.h"
#include "interrupts.h"

/* PCI Configuration Space Access */
#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC

/* PCI Configuration Space Register Offsets */
#define PCI_VENDOR_ID 0x00
#define PCI_DEVICE_ID 0x02
#define PCI_CLASS_CODE 0x08
#define PCI_HEADER_TYPE 0x0E
#define PCI_BAR0 0x10
#define PCI_SECONDARY_BUS 0x19

/* PCI Class Codes */
#define PCI_CLASS_BRIDGE 0x06
#define PCI_SUBCLASS_PCI_BRIDGE 0x04

/* Maximum PCI buses, devices, and functions */
#define PCI_MAX_BUSES 256
#define PCI_MAX_DEVICES 32
#define PCI_MAX_FUNCTIONS 8


/* Forward declarations */
void scan_pci_bus(uint8_t bus);
void scan_pci_function(uint8_t bus, uint8_t device, uint8_t function);


/* PCI device information structure */
struct PCIDeviceInfo {
    uint16_t vendorID;
    uint16_t deviceID;
    uint8_t classCode;
    uint8_t subclassCode;
    uint8_t progIF;
    uint8_t revisionID;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
};

/* Array to store discovered devices */
PCIDeviceInfo discoveredDevices[256];
size_t deviceCount = 0;
/* Read a 32-bit value from PCI configuration space */
uint32_t pci_config_read_dword(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (device << 11) | (function << 8) | 
                      (offset & 0xFC) | (uint32_t)0x80000000);
    
    // Use inline assembly directly instead of outl/inl functions
    uint32_t ret;
    asm volatile ("outl %0, %1" : : "a"(address), "Nd"(PCI_CONFIG_ADDRESS));
    asm volatile ("inl %1, %0" : "=a"(ret) : "Nd"(PCI_CONFIG_DATA));
    return ret;
}

/* Read a 16-bit value from PCI configuration space */
uint16_t pci_config_read_word(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t data = pci_config_read_dword(bus, device, function, offset);
    return (uint16_t)((data >> ((offset & 2) * 8)) & 0xFFFF);
}

/* Read an 8-bit value from PCI configuration space */
uint8_t pci_config_read_byte(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t data = pci_config_read_dword(bus, device, function, offset);
    return (uint8_t)((data >> ((offset & 3) * 8)) & 0xFF);
}

/* Check if a PCI device exists */
bool pci_device_exists(uint8_t bus, uint8_t device, uint8_t function) {
    uint16_t vendorID = pci_config_read_word(bus, device, function, PCI_VENDOR_ID);
    return vendorID != 0xFFFF;
}

/* Check if a device has multiple functions */
bool pci_is_multifunction(uint8_t bus, uint8_t device, uint8_t function) {
    uint8_t headerType = pci_config_read_byte(bus, device, function, PCI_HEADER_TYPE);
    return (headerType & 0x80) != 0;
}

/* Get device type name based on class and subclass */
const char* get_device_type(uint8_t classCode, uint8_t subclassCode) {
    if (classCode == 0x00) {
        return "Unclassified Device";
    } else if (classCode == 0x01) {
        if (subclassCode == 0x00) return "SCSI Storage Controller";
        if (subclassCode == 0x01) return "IDE Controller";
        if (subclassCode == 0x06) return "SATA Controller";
        if (subclassCode == 0x08) return "NVMe Controller";
        return "Mass Storage Controller";
    } else if (classCode == 0x02) {
        if (subclassCode == 0x00) return "Ethernet Controller";
        return "Network Controller";
    } else if (classCode == 0x03) {
        if (subclassCode == 0x00) return "VGA Controller";
        return "Display Controller";
    } else if (classCode == 0x04) {
        return "Multimedia Controller";
    } else if (classCode == 0x06) {
        if (subclassCode == 0x00) return "Host Bridge";
        if (subclassCode == 0x01) return "ISA Bridge";
        if (subclassCode == 0x04) return "PCI-to-PCI Bridge";
        return "Bridge Device";
    } else if (classCode == 0x0C) {
        if (subclassCode == 0x03) return "USB Controller";
        return "Serial Bus Controller";
    }
    return "Unknown Device";
}

/* Scan a specific PCI function */
void scan_pci_function(uint8_t bus, uint8_t device, uint8_t function) {
    uint16_t vendorID = pci_config_read_word(bus, device, function, PCI_VENDOR_ID);
    if (vendorID == 0xFFFF) return;
    
    uint16_t deviceID = pci_config_read_word(bus, device, function, PCI_DEVICE_ID);
    uint32_t classReg = pci_config_read_dword(bus, device, function, PCI_CLASS_CODE);
    
    uint8_t classCode = (classReg >> 24) & 0xFF;
    uint8_t subclassCode = (classReg >> 16) & 0xFF;
    uint8_t progIF = (classReg >> 8) & 0xFF;
    uint8_t revisionID = classReg & 0xFF;
    
    // Store device info
    if (deviceCount < 256) {
        discoveredDevices[deviceCount].vendorID = vendorID;
        discoveredDevices[deviceCount].deviceID = deviceID;
        discoveredDevices[deviceCount].classCode = classCode;
        discoveredDevices[deviceCount].subclassCode = subclassCode;
        discoveredDevices[deviceCount].progIF = progIF;
        discoveredDevices[deviceCount].revisionID = revisionID;
        discoveredDevices[deviceCount].bus = bus;
        discoveredDevices[deviceCount].device = device;
        discoveredDevices[deviceCount].function = function;
        deviceCount++;
    }
    
    // If this is a PCI-to-PCI bridge, scan the secondary bus
    if (classCode == PCI_CLASS_BRIDGE && subclassCode == PCI_SUBCLASS_PCI_BRIDGE) {
        uint8_t secondaryBus = pci_config_read_byte(bus, device, function, PCI_SECONDARY_BUS);
        scan_pci_bus(secondaryBus);
    }
}

/* Scan a specific PCI device */
void scan_pci_device(uint8_t bus, uint8_t device) {
    if (!pci_device_exists(bus, device, 0)) return;
    
    scan_pci_function(bus, device, 0);
    
    if (pci_is_multifunction(bus, device, 0)) {
        for (uint8_t function = 1; function < PCI_MAX_FUNCTIONS; function++) {
            if (pci_device_exists(bus, device, function)) {
                scan_pci_function(bus, device, function);
            }
        }
    }
}

/* Scan a specific PCI bus */
void scan_pci_bus(uint8_t bus) {
    for (uint8_t device = 0; device < PCI_MAX_DEVICES; device++) {
        scan_pci_device(bus, device);
    }
}

/* Display information about a PCI device */
void print_device_info(const PCIDeviceInfo& device) {
    cout.hex();
    cout << "Bus " << (int)device.bus << ", Device " << (int)device.device 
         << ", Function " << (int)device.function << "\n";
    cout << "  Vendor ID: "  << device.vendorID << ", Device ID: " << device.deviceID << "\n";
    cout << "  Type: " << get_device_type(device.classCode, device.subclassCode) << "\n";
    cout << "  Class: " << (int)device.classCode 
         << ", Subclass: " << (int)device.subclassCode 
         << ", ProgIF: " << (int)device.progIF << "\n\n";
}




#include "kernel.h"
#include "terminal_hooks.h"
#include "terminal_io.h"
#include "iostream_wrapper.h"
#include "interrupts.h"

/* PCI Configuration Space Access */
#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC

/* PCI Configuration Space Register Offsets */
#define PCI_VENDOR_ID 0x00
#define PCI_DEVICE_ID 0x02
#define PCI_COMMAND 0x04
#define PCI_STATUS 0x06
#define PCI_REVISION_ID 0x08
#define PCI_PROG_IF 0x09
#define PCI_SUBCLASS 0x0A
#define PCI_CLASS 0x0B
#define PCI_CACHE_LINE_SIZE 0x0C
#define PCI_LATENCY_TIMER 0x0D
#define PCI_HEADER_TYPE 0x0E
#define PCI_BIST 0x0F
#define PCI_BAR0 0x10
#define PCI_BAR1 0x14
#define PCI_BAR2 0x18
#define PCI_BAR3 0x1C
#define PCI_BAR4 0x20
#define PCI_BAR5 0x24
#define PCI_CARDBUS_CIS 0x28
#define PCI_SUBSYSTEM_VENDOR_ID 0x2C
#define PCI_SUBSYSTEM_ID 0x2E
#define PCI_EXPANSION_ROM_BASE 0x30
#define PCI_CAPABILITIES_PTR 0x34
#define PCI_INTERRUPT_LINE 0x3C
#define PCI_INTERRUPT_PIN 0x3D
#define PCI_MIN_GRANT 0x3E
#define PCI_MAX_LATENCY 0x3F
#define PCI_SECONDARY_BUS 0x19

/* PCI Command Register Bits */
#define PCI_CMD_IO_SPACE 0x0001        /* Enable I/O space */
#define PCI_CMD_MEMORY_SPACE 0x0002    /* Enable memory space */
#define PCI_CMD_BUS_MASTER 0x0004      /* Enable bus mastering */
#define PCI_CMD_SPECIAL_CYCLES 0x0008  /* Enable special cycles */
#define PCI_CMD_MWI_ENABLE 0x0010      /* Enable memory write & invalidate */
#define PCI_CMD_VGA_PALETTE 0x0020     /* Enable VGA palette snoop */
#define PCI_CMD_PARITY_ERROR 0x0040    /* Enable parity error response */
#define PCI_CMD_SERR_ENABLE 0x0100     /* Enable SERR# */
#define PCI_CMD_FAST_BACK 0x0200       /* Enable fast back-to-back */
#define PCI_CMD_INTX_DISABLE 0x0400    /* Disable INTx interrupts */

/* PCI Status Register Bits */
#define PCI_STATUS_INTERRUPT 0x0008     /* Interrupt status */
#define PCI_STATUS_CAP_LIST 0x0010      /* Capability list */
#define PCI_STATUS_66MHZ 0x0020         /* 66 MHz capable */
#define PCI_STATUS_FAST_BACK 0x0080     /* Fast back-to-back capable */
#define PCI_STATUS_PARITY 0x0100        /* Parity error detected */
#define PCI_STATUS_DEVSEL_MASK 0x0600   /* DEVSEL timing mask */
#define PCI_STATUS_SIG_TARGET_ABORT 0x0800  /* Signaled target abort */
#define PCI_STATUS_REC_TARGET_ABORT 0x1000  /* Received target abort */
#define PCI_STATUS_REC_MASTER_ABORT 0x2000  /* Received master abort */
#define PCI_STATUS_SIG_SYSTEM_ERROR 0x4000  /* Signaled system error */
#define PCI_STATUS_DETECTED_PARITY 0x8000   /* Detected parity error */

/* BAR related flags */
#define PCI_BAR_IO_SPACE 0x01           /* BAR is I/O space, not memory */
#define PCI_BAR_MEM_TYPE_MASK 0x06      /* Memory BAR type mask */
#define PCI_BAR_MEM_TYPE_32 0x00        /* 32-bit BAR */
#define PCI_BAR_MEM_TYPE_1M 0x02        /* Below 1M */
#define PCI_BAR_MEM_TYPE_64 0x04        /* 64-bit BAR */
#define PCI_BAR_MEM_PREFETCH 0x08       /* Prefetchable memory */

/* Function to write a 32-bit value to PCI configuration space */
void pci_config_write_dword(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value) {
    uint32_t address = (uint32_t)((bus << 16) | (device << 11) | (function << 8) | 
                      (offset & 0xFC) | (uint32_t)0x80000000);
    
    asm volatile ("outl %0, %1" : : "a"(address), "Nd"(PCI_CONFIG_ADDRESS));
    asm volatile ("outl %0, %1" : : "a"(value), "Nd"(PCI_CONFIG_DATA));
}

/* Function to write a 16-bit value to PCI configuration space */
void pci_config_write_word(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint16_t value) {
    uint32_t data = pci_config_read_dword(bus, device, function, offset & ~3);
    uint32_t shift = (offset & 2) * 8;
    
    data &= ~(0xFFFF << shift);
    data |= ((uint32_t)value) << shift;
    
    pci_config_write_dword(bus, device, function, offset & ~3, data);
}

/* Function to write an 8-bit value to PCI configuration space */
void pci_config_write_byte(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint8_t value) {
    uint32_t data = pci_config_read_dword(bus, device, function, offset & ~3);
    uint32_t shift = (offset & 3) * 8;
    
    data &= ~(0xFF << shift);
    data |= ((uint32_t)value) << shift;
    
    pci_config_write_dword(bus, device, function, offset & ~3, data);
}

/* Function to enable bus mastering for a device */
void pci_enable_bus_mastering(uint8_t bus, uint8_t device, uint8_t function) {
    uint16_t command = pci_config_read_word(bus, device, function, PCI_COMMAND);
    command |= PCI_CMD_BUS_MASTER;
    pci_config_write_word(bus, device, function, PCI_COMMAND, command);
}

/* Function to enable memory space for a device */
void pci_enable_memory_space(uint8_t bus, uint8_t device, uint8_t function) {
    uint16_t command = pci_config_read_word(bus, device, function, PCI_COMMAND);
    command |= PCI_CMD_MEMORY_SPACE;
    pci_config_write_word(bus, device, function, PCI_COMMAND, command);
}

/* Function to enable I/O space for a device */
void pci_enable_io_space(uint8_t bus, uint8_t device, uint8_t function) {
    uint16_t command = pci_config_read_word(bus, device, function, PCI_COMMAND);
    command |= PCI_CMD_IO_SPACE;
    pci_config_write_word(bus, device, function, PCI_COMMAND, command);
}

/* Get BAR size by writing all 1's to BAR and reading back */
uint32_t pci_get_bar_size(uint8_t bus, uint8_t device, uint8_t function, uint8_t bar_num) {
    uint8_t bar_offset = PCI_BAR0 + (bar_num * 4);
    uint32_t original_value = pci_config_read_dword(bus, device, function, bar_offset);
    
    // Write all 1's to the BAR
    pci_config_write_dword(bus, device, function, bar_offset, 0xFFFFFFFF);
    
    // Read back to get size mask
    uint32_t size_mask = pci_config_read_dword(bus, device, function, bar_offset);
    
    // Restore original value
    pci_config_write_dword(bus, device, function, bar_offset, original_value);
    
    // For I/O BARs
    if (original_value & PCI_BAR_IO_SPACE) {
        return ~(size_mask & 0xFFFFFFFC) + 1;
    } 
    // For Memory BARs
    else {
        return ~(size_mask & 0xFFFFFFF0) + 1;
    }
}

/* Structure to hold BAR information */
struct PciBarInfo {
    uint32_t base_address;
    uint32_t size;
    bool is_io;
    bool is_prefetchable;
    bool is_64bit;
};

/* Function to get detailed BAR information */
PciBarInfo pci_get_bar_info(uint8_t bus, uint8_t device, uint8_t function, uint8_t bar_num) {
    PciBarInfo bar_info;
    uint8_t bar_offset = PCI_BAR0 + (bar_num * 4);
    uint32_t bar_value = pci_config_read_dword(bus, device, function, bar_offset);
    
    bar_info.is_io = (bar_value & PCI_BAR_IO_SPACE) != 0;
    
    if (bar_info.is_io) {
        // I/O BAR
        bar_info.base_address = bar_value & 0xFFFFFFFC;
        bar_info.is_prefetchable = false;
        bar_info.is_64bit = false;
    } else {
        // Memory BAR
        bar_info.is_prefetchable = (bar_value & PCI_BAR_MEM_PREFETCH) != 0;
        uint8_t bar_type = (bar_value & PCI_BAR_MEM_TYPE_MASK);
        bar_info.is_64bit = (bar_type == PCI_BAR_MEM_TYPE_64);
        bar_info.base_address = bar_value & 0xFFFFFFF0;
        
        // For 64-bit BAR, read the upper 32 bits
        if (bar_info.is_64bit && bar_num < 5) {
            uint32_t upper = pci_config_read_dword(bus, device, function, bar_offset + 4);
            if (upper != 0) {
                // Our implementation doesn't support 64-bit addressing beyond 4GB
                // So we'll just note it for display purposes
                cout << "Warning: Device at " << (int)bus << ":" << (int)device << ":" 
                     << (int)function << " has 64-bit BAR with upper bits set\n";
            }
        }
    }
    
    // Get the size
    bar_info.size = pci_get_bar_size(bus, device, function, bar_num);
    
    return bar_info;
}

/* Function to configure a device's interrupt */
void pci_configure_interrupt(uint8_t bus, uint8_t device, uint8_t function, uint8_t interrupt_line) {
    uint8_t interrupt_pin = pci_config_read_byte(bus, device, function, PCI_INTERRUPT_PIN);
    
    if (interrupt_pin != 0) {
        // Device uses interrupts, so configure the interrupt line
        pci_config_write_byte(bus, device, function, PCI_INTERRUPT_LINE, interrupt_line);
        cout << "  Interrupt: PIN " << (int)interrupt_pin << " -> LINE " << (int)interrupt_line << "\n";
    } else {
        cout << "  Device does not use interrupts\n";
    }
}

/* Function to dump all BARs for a device */
void pci_dump_bars(uint8_t bus, uint8_t device, uint8_t function) {
    uint8_t header_type = pci_config_read_byte(bus, device, function, PCI_HEADER_TYPE) & 0x7F;
    uint8_t num_bars = (header_type == 0) ? 6 : 2;  // Type 0 has 6 BARs, Type 1 has 2
    
    cout << "  Base Address Registers:\n";
    
    for (uint8_t i = 0; i < num_bars; i++) {
        uint32_t bar_value = pci_config_read_dword(bus, device, function, PCI_BAR0 + (i * 4));
        
        // Skip if BAR is empty
        if (bar_value == 0) {
            continue;
        }
        
        PciBarInfo bar_info = pci_get_bar_info(bus, device, function, i);
        
        cout << "    BAR" << (int)i << ": ";
        cout.hex();
        cout << bar_info.base_address;
        cout.dec();
        
        if (bar_info.is_io) {
            cout << " (I/O Space, Size: ";
            cout.hex();
            cout << bar_info.size;
            cout.dec();
            cout << ")";
        } else {
            cout << " (Memory Space, Size: ";
            cout.hex();
            cout << bar_info.size;
            cout.dec();
            cout << ", ";
            
            if (bar_info.is_64bit) {
                cout << "64-bit, ";
            } else {
                cout << "32-bit, ";
            }
            
            if (bar_info.is_prefetchable) {
                cout << "Prefetchable)";
            } else {
                cout << "Non-Prefetchable)";
            }
        }
        
        cout << "\n";
        
        // Skip the next BAR if this was a 64-bit BAR
        if (!bar_info.is_io && bar_info.is_64bit) {
            i++;
        }
    }
}

/* Function to configure a specific PCIe device */
void pci_configure_device(uint8_t bus, uint8_t device, uint8_t function) {
    uint16_t vendor_id = pci_config_read_word(bus, device, function, PCI_VENDOR_ID);
    uint16_t device_id = pci_config_read_word(bus, device, function, PCI_DEVICE_ID);
    uint8_t class_code = pci_config_read_byte(bus, device, function, PCI_CLASS);
    uint8_t subclass = pci_config_read_byte(bus, device, function, PCI_SUBCLASS);
    
    cout << "Configuring device at ";
    cout.dec();
    cout << (int)bus << ":" << (int)device << ":" << (int)function;
    cout.hex();

    cout << " [Vendor: " << vendor_id << ", Device: " << device_id << "]\n";
    
    // Dump the device's BARs
    pci_dump_bars(bus, device, function);
    
    // Enable memory space, I/O space, and bus mastering for the device
    uint16_t command = pci_config_read_word(bus, device, function, PCI_COMMAND);
    command |= (PCI_CMD_MEMORY_SPACE | PCI_CMD_IO_SPACE | PCI_CMD_BUS_MASTER);
    pci_config_write_word(bus, device, function, PCI_COMMAND, command);
    
    // Configure interrupts with a simple round-robin assignment (for demonstration)
    // In a real system, you'd use more sophisticated interrupt routing
    static uint8_t next_irq = 0x20;  // Start with IRQ 32 (a typical starting point for PCI)
    pci_configure_interrupt(bus, device, function, next_irq++);
    
    // Special handling for specific device types
    if (class_code == 0x03 && subclass == 0x00) {
        cout << "  VGA Controller detected: Enabling VGA palette snoop\n";
        command |= PCI_CMD_VGA_PALETTE;
        pci_config_write_word(bus, device, function, PCI_COMMAND, command);
    }
    
    if (class_code == 0x06 && subclass == 0x04) {
        cout << "  PCI-to-PCI Bridge detected: Configuring secondary bus\n";
        // Here you might want to configure bridge-specific settings
    }
    
    cout << "  Configuration complete.\n\n";
}

/* Command to configure all PCIe devices */
void cmd_pci_config() {
    cout << "Configuring all PCIe devices...\n\n";
    
    // First enumerate all devices
    deviceCount = 0;
    scan_pci_bus(0);
    
    // Then configure each discovered device
    for (size_t i = 0; i < deviceCount; i++) {
        PCIDeviceInfo& device = discoveredDevices[i];
        pci_configure_device(device.bus, device.device, device.function);
    }
    
    cout << "PCIe device configuration complete.\n";
}



/* Command to run PCIe enumeration */
void cmd_pci_enum() {
    cout << "Scanning PCIe bus...\n\n";
    
    // Reset device count
    deviceCount = 0;
    
    // Start scanning from bus 0
    scan_pci_bus(0);
    
    // Display results
    cout << "Found " << deviceCount << " PCIe devices.\n";
    for (size_t i = 0; i < deviceCount; i++) {
        //print_device_info(discoveredDevices[i]);
    }
}
