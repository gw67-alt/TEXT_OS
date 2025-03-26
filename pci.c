#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>
#include "io.h"
#include "pci.h"

#define PCI_PATH "/sys/bus/pci/devices/"
#define NVME_CLASS_CODE "010802"

/* PCI configuration space access ports */
#define PCI_CONFIG_ADDRESS  0xCF8
#define PCI_CONFIG_DATA     0xCFC

/* PCI device identifier structure */
struct pci_device_id {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    const char* name;
};

/* Table of known PCI devices - can be extended with more entries */
static const struct pci_device_id known_devices[] = {
    /* Mass Storage Controllers */
    {0x8086, 0x2922, 0x01, 0x06, 0x01, "Intel ICH9 AHCI Controller"},
    {0x8086, 0x2829, 0x01, 0x06, 0x01, "Intel ICH8M AHCI Controller"},
    {0x8086, 0x3B22, 0x01, 0x06, 0x01, "Intel 5 Series/3400 Series AHCI Controller"},
    {0x8086, 0x3B32, 0x01, 0x06, 0x01, "Intel 5 Series/3400 Series AHCI Controller"},
    {0x1022, 0x7801, 0x01, 0x06, 0x01, "AMD AHCI Controller"},
    {0x1002, 0x4380, 0x01, 0x06, 0x01, "AMD AHCI Controller"},
    {0x1B4B, 0x9172, 0x01, 0x06, 0x01, "Marvell AHCI Controller"},
    {0x1B4B, 0x9182, 0x01, 0x06, 0x01, "Marvell AHCI Controller"},
    
    /* Network Controllers */
    {0x8086, 0x100E, 0x02, 0x00, 0x00, "Intel PRO/1000 Network Controller"},
    {0x8086, 0x10EA, 0x02, 0x00, 0x00, "Intel I217 Network Controller"},
    {0x8086, 0x153A, 0x02, 0x00, 0x00, "Intel I217-LM Network Controller"},
    {0x8086, 0x15A3, 0x02, 0x00, 0x00, "Intel I219-LM Network Controller"},
    {0x10EC, 0x8168, 0x02, 0x00, 0x00, "Realtek RTL8168 Network Controller"},
    
    /* Display Controllers */
    {0x8086, 0x0046, 0x03, 0x00, 0x00, "Intel HD Graphics"},
    {0x8086, 0x0162, 0x03, 0x00, 0x00, "Intel HD Graphics 4000"},
    {0x1002, 0x9802, 0x03, 0x00, 0x00, "AMD Radeon HD 7000 Series"},
    {0x10DE, 0x0641, 0x03, 0x00, 0x00, "NVIDIA GeForce GT 630"},
    
    /* End of table */
    {0, 0, 0, 0, 0, NULL}
};

/* Forward declarations */
const char* get_device_name(uint16_t vendor_id, uint16_t device_id, 
                           uint8_t class_code, uint8_t subclass, uint8_t prog_if);
const char* get_class_name(uint8_t class_code, uint8_t subclass, uint8_t prog_if);

/* Generate a PCI configuration address */
static uint32_t pci_get_address(uint8_t bus, uint8_t device, 
                               uint8_t function, uint8_t offset) {
    return (uint32_t)(((uint32_t)bus << 16) | 
                     ((uint32_t)(device & 0x1F) << 11) |
                     ((uint32_t)(function & 0x07) << 8) | 
                     (offset & 0xFC) | 
                     ((uint32_t)0x80000000));
}

/* Read a byte from PCI configuration space */
uint8_t pci_config_read_byte(uint8_t bus, uint8_t device, 
                            uint8_t function, uint8_t offset) {
    uint32_t address = pci_get_address(bus, device, function, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    return inb(PCI_CONFIG_DATA + (offset & 0x03));
}

/* Read a word (16 bits) from PCI configuration space */
uint16_t pci_config_read_word(uint8_t bus, uint8_t device, 
                             uint8_t function, uint8_t offset) {
    uint32_t address = pci_get_address(bus, device, function, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    return inw(PCI_CONFIG_DATA + (offset & 0x02));
}

/* Read a dword (32 bits) from PCI configuration space */
uint32_t pci_config_read_dword(uint8_t bus, uint8_t device, 
                              uint8_t function, uint8_t offset) {
    uint32_t address = pci_get_address(bus, device, function, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

/* Write a byte to PCI configuration space */
void pci_config_write_byte(uint8_t bus, uint8_t device, 
                          uint8_t function, uint8_t offset, uint8_t value) {
    uint32_t address = pci_get_address(bus, device, function, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    outb(PCI_CONFIG_DATA + (offset & 0x03), value);
}

/* Write a word (16 bits) to PCI configuration space */
void pci_config_write_word(uint8_t bus, uint8_t device, 
                          uint8_t function, uint8_t offset, uint16_t value) {
    uint32_t address = pci_get_address(bus, device, function, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    outw(PCI_CONFIG_DATA + (offset & 0x02), value);
}

/* Write a dword (32 bits) to PCI configuration space */
void pci_config_write_dword(uint8_t bus, uint8_t device, 
                           uint8_t function, uint8_t offset, uint32_t value) {
    uint32_t address = pci_get_address(bus, device, function, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

/* Check if a device exists */
static bool pci_device_exists(uint8_t bus, uint8_t device, uint8_t function) {
    uint16_t vendor_id = pci_config_read_word(bus, device, function, 0x00);
    return vendor_id != 0xFFFF; // 0xFFFF indicates no device
}

/* Get PCI device details */
struct pci_device pci_get_device_info(uint8_t bus, uint8_t device, uint8_t function) {
    struct pci_device dev;
    
    dev.bus = bus;
    dev.device = device;
    dev.function = function;
    
    dev.vendor_id = pci_config_read_word(bus, device, function, 0x00);
    dev.device_id = pci_config_read_word(bus, device, function, 0x02);
    
    dev.command = pci_config_read_word(bus, device, function, 0x04);
    dev.status = pci_config_read_word(bus, device, function, 0x06);
    
    dev.revision_id = pci_config_read_byte(bus, device, function, 0x08);
    dev.prog_if = pci_config_read_byte(bus, device, function, 0x09);
    dev.subclass = pci_config_read_byte(bus, device, function, 0x0A);
    dev.class_code = pci_config_read_byte(bus, device, function, 0x0B);
    
    dev.cache_line_size = pci_config_read_byte(bus, device, function, 0x0C);
    dev.latency_timer = pci_config_read_byte(bus, device, function, 0x0D);
    dev.header_type = pci_config_read_byte(bus, device, function, 0x0E);
    dev.bist = pci_config_read_byte(bus, device, function, 0x0F);
    
    // Read base address registers (BAR0-BAR5)
    for (int i = 0; i < 6; i++) {
        dev.bar[i] = pci_config_read_dword(bus, device, function, 0x10 + i * 4);
    }
    
    // Additional fields for different header types - here we only initialize for Type 0
    if ((dev.header_type & 0x7F) == 0) {
        dev.cardbus_cis_ptr = pci_config_read_dword(bus, device, function, 0x28);
        dev.subsystem_vendor_id = pci_config_read_word(bus, device, function, 0x2C);
        dev.subsystem_id = pci_config_read_word(bus, device, function, 0x2E);
        dev.expansion_rom_base_addr = pci_config_read_dword(bus, device, function, 0x30);
        dev.capabilities_ptr = pci_config_read_byte(bus, device, function, 0x34);
        dev.interrupt_line = pci_config_read_byte(bus, device, function, 0x3C);
        dev.interrupt_pin = pci_config_read_byte(bus, device, function, 0x3D);
        dev.min_grant = pci_config_read_byte(bus, device, function, 0x3E);
        dev.max_latency = pci_config_read_byte(bus, device, function, 0x3F);
    }
    
    // Try to identify the device
    dev.name = get_device_name(dev.vendor_id, dev.device_id, 
                               dev.class_code, dev.subclass, dev.prog_if);
    
    return dev;
}

/* Check if a device has multiple functions */
static bool pci_device_has_functions(uint8_t bus, uint8_t device) {
    uint8_t header_type = pci_config_read_byte(bus, device, 0, 0x0E);
    return (header_type & 0x80) != 0;
}

/* Enumerate all PCI devices */
void enumerate_pci_devices() {
    printf("Enumerating PCI Devices:\n");
    printf("-------------------------------------------------------------------------\n");
    printf("| BUS | DEV | FN | VendorID | DeviceID | Class | Type | Name          |\n");
    printf("-------------------------------------------------------------------------\n");
    
    int device_count = 0;
    
    // Scan all PCI buses
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            uint8_t function = 0;
            
            if (!pci_device_exists(bus, device, function)) {
                continue;
            }
            
            // Check if this is a multi-function device
            bool is_multi_function = pci_device_has_functions(bus, device);
            
            // Scan the appropriate number of functions
            for (function = 0; function < (is_multi_function ? 8 : 1); function++) {
                if (!pci_device_exists(bus, device, function)) {
                    continue;
                }
                
                // Get device information
                struct pci_device dev = pci_get_device_info(bus, device, function);
                
                // Display device information
                printf("| %03X | %03X | %02X | %04X     | %04X     | %02X:%02X:%02X | %02X   | %-14s |\n",
                       bus, device, function,
                       dev.vendor_id, dev.device_id,
                       dev.class_code, dev.subclass, dev.prog_if,
                       dev.header_type & 0x7F,
                       dev.name ? (strlen(dev.name) > 14 ? 
                                  (char[]){dev.name[0], dev.name[1], dev.name[2], dev.name[3], 
                                          dev.name[4], dev.name[5], dev.name[6], dev.name[7], 
                                          dev.name[8], dev.name[9], dev.name[10], '.', '.', '.'} : dev.name) 
                                : "Unknown");
                
                device_count++;
            }
        }
    }
    
    printf("-------------------------------------------------------------------------\n");
    printf("Total PCI devices found: %d\n", device_count);
}

/* Get device name from known device table */
const char* get_device_name(uint16_t vendor_id, uint16_t device_id, 
                           uint8_t class_code, uint8_t subclass, uint8_t prog_if) {
    // First try to find an exact match in the known devices table
    for (int i = 0; known_devices[i].name != NULL; i++) {
        if (known_devices[i].vendor_id == vendor_id && 
            known_devices[i].device_id == device_id) {
            return known_devices[i].name;
        }
    }
    
    // If no exact match, try to find a class match
    for (int i = 0; known_devices[i].name != NULL; i++) {
        if (known_devices[i].class_code == class_code && 
            known_devices[i].subclass == subclass && 
            known_devices[i].prog_if == prog_if) {
            return known_devices[i].name;
        }
    }
    
    // If no match found, return a generic class description
    return get_class_name(class_code, subclass, prog_if);
}

/* Get generic class name based on class/subclass codes */
const char* get_class_name(uint8_t class_code, uint8_t subclass, uint8_t prog_if) {
    switch (class_code) {
        case 0x00:
            return "Legacy Device";
        case 0x01:
            switch (subclass) {
                case 0x00: return "SCSI Controller";
                case 0x01: return "IDE Controller";
                case 0x02: return "Floppy Controller";
                case 0x03: return "IPI Controller";
                case 0x04: return "RAID Controller";
                case 0x05: return "ATA Controller";
                case 0x06: 
                    if (prog_if == 0x01) 
                        return "AHCI Controller";
                    return "SATA Controller";
                case 0x07: return "SAS Controller";
                case 0x08: return "NVMe Controller";
                default: return "Storage Controller";
            }
        case 0x02:
            switch (subclass) {
                case 0x00: return "Ethernet Controller";
                case 0x01: return "Token Ring Controller";
                case 0x02: return "FDDI Controller";
                case 0x03: return "ATM Controller";
                case 0x04: return "ISDN Controller";
                case 0x05: return "WorldFip Controller";
                case 0x06: return "PICMG Controller";
                case 0x07: return "InfiniBand Controller";
                case 0x08: return "Fabric Controller";
                default: return "Network Controller";
            }
        case 0x03:
            switch (subclass) {
                case 0x00: return "VGA Controller";
                case 0x01: return "XGA Controller";
                case 0x02: return "3D Controller";
                default: return "Display Controller";
            }
        case 0x04:
            switch (subclass) {
                case 0x00: return "Video Controller";
                case 0x01: return "Audio Controller";
                case 0x02: return "Phone Controller";
                case 0x03: return "HD Audio Controller";
                default: return "Multimedia Controller";
            }
        case 0x05:
            switch (subclass) {
                case 0x00: return "RAM Controller";
                case 0x01: return "Flash Controller";
                default: return "Memory Controller";
            }
        case 0x06:
            switch (subclass) {
                case 0x00: return "Host Bridge";
                case 0x01: return "ISA Bridge";
                case 0x02: return "EISA Bridge";
                case 0x03: return "MCA Bridge";
                case 0x04: return "PCI-to-PCI Bridge";
                case 0x05: return "PCMCIA Bridge";
                case 0x06: return "NuBus Bridge";
                case 0x07: return "CardBus Bridge";
                case 0x08: return "RACEway Bridge";
                case 0x09: return "Semi-PCI-to-PCI Bridge";
                case 0x0A: return "InfiniBand-to-PCI Bridge";
                default: return "Bridge Device";
            }
        case 0x07:
            switch (subclass) {
                case 0x00: return "Serial Controller";
                case 0x01: return "Parallel Controller";
                case 0x02: return "Multiport Serial Controller";
                case 0x03: return "Modem";
                case 0x04: return "GPIB Controller";
                case 0x05: return "Smart Card Controller";
                default: return "Communication Controller";
            }
        case 0x08:
            switch (subclass) {
                case 0x00: return "PIC";
                case 0x01: return "DMA Controller";
                case 0x02: return "Timer";
                case 0x03: return "RTC Controller";
                case 0x04: return "PCI Hot-Plug Controller";
                case 0x05: return "SD Host Controller";
                case 0x06: return "IOMMU";
                default: return "System Peripheral";
            }
        case 0x09:
            switch (subclass) {
                case 0x00: return "Keyboard Controller";
                case 0x01: return "Digitizer";
                case 0x02: return "Mouse Controller";
                case 0x03: return "Scanner Controller";
                case 0x04: return "Gameport Controller";
                default: return "Input Controller";
            }
        case 0x0A:
            return "Docking Station";
        case 0x0B:
            return "Processor";
        case 0x0C:
            switch (subclass) {
                case 0x00: return "FireWire Controller";
                case 0x01: return "ACCESS Bus Controller";
                case 0x02: return "SSA Controller";
                case 0x03: 
                    switch (prog_if) {
                        case 0x00: return "USB UHCI Controller";
                        case 0x10: return "USB OHCI Controller";
                        case 0x20: return "USB EHCI Controller";
                        case 0x30: return "USB XHCI Controller";
                        default: return "USB Controller";
                    }
                case 0x04: return "Fibre Channel Controller";
                case 0x05: return "SMBus Controller";
                case 0x06: return "InfiniBand Controller";
                case 0x07: return "IPMI Controller";
                case 0x08: return "SERCOS Controller";
                case 0x09: return "CANbus Controller";
                default: return "Serial Bus Controller";
            }
        case 0x0D:
            return "Wireless Controller";
        case 0x0E:
            return "Intelligent I/O Controller";
        case 0x0F:
            return "Satellite Communication Controller";
        case 0x10:
            return "Encryption Controller";
        case 0x11:
            return "Signal Processing Controller";
        case 0x12:
            return "Processing Accelerator";
        case 0x13:
            return "Non-Essential Instrumentation";
        case 0x40:
            return "Co-Processor";
        default:
            return "Unknown Device";
    }
}

/* Enable PCI Bus Mastering for a device */
void pci_enable_bus_mastering(uint8_t bus, uint8_t device, uint8_t function) {
    uint16_t command = pci_config_read_word(bus, device, function, 0x04);
    
    // Set Bus Master (bit 2) and Memory Space (bit 1) bits
    command |= (1 << 2) | (1 << 1);
    
    // Write back the updated command register
    pci_config_write_word(bus, device, function, 0x04, command);
}

/* Find a PCI device with specific class, subclass, and programming interface */
bool pci_find_device(uint8_t class_code, uint8_t subclass, uint8_t prog_if,
                    uint8_t* out_bus, uint8_t* out_device, uint8_t* out_function) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t function = 0; function < 8; function++) {
                uint16_t vendor_id = pci_config_read_word(bus, device, function, 0x00);
                if (vendor_id == 0xFFFF) continue; // No device
                
                uint8_t current_class = pci_config_read_byte(bus, device, function, 0x0B);
                uint8_t current_subclass = pci_config_read_byte(bus, device, function, 0x0A);
                uint8_t current_prog_if = pci_config_read_byte(bus, device, function, 0x09);
                
                if (current_class == class_code && 
                    current_subclass == subclass && 
                    current_prog_if == prog_if) {
                    *out_bus = bus;
                    *out_device = device;
                    *out_function = function;
                    return true;
                }
            }
        }
    }
    
    return false;
}

/* Get the size of a PCI Base Address Register (BAR) */
uint32_t pci_get_bar_size(uint8_t bus, uint8_t device, uint8_t function, uint8_t bar_num) {
    uint32_t bar_offset = 0x10 + (bar_num * 4);
    uint32_t old_value, bar_size;
    
    // Save the original BAR value
    old_value = pci_config_read_dword(bus, device, function, bar_offset);
    
    // Write all 1's to the BAR
    pci_config_write_dword(bus, device, function, bar_offset, 0xFFFFFFFF);
    
    // Read it back to see what bits are writable
    bar_size = pci_config_read_dword(bus, device, function, bar_offset);
    
    // Restore the original BAR value
    pci_config_write_dword(bus, device, function, bar_offset, old_value);
    
    // If this is an I/O BAR (bit 0 set)
    if (old_value & 0x1) {
        // I/O BARs only use the lower 16 bits
        bar_size &= 0xFFFF;
    }
    
    // Mask out the non-writable bits and BAR type bits
    bar_size &= ~0xF;
    
    // If no writable bits, BAR isn't implemented
    if (bar_size == 0) {
        return 0;
    }
    
    // Invert the bits and add 1 to get the size
    return (~bar_size) + 1;
}

/* Get the type of a PCI Base Address Register (BAR) */
enum pci_bar_type pci_get_bar_type(uint8_t bus, uint8_t device, uint8_t function, uint8_t bar_num) {
    uint32_t bar_offset = 0x10 + (bar_num * 4);
    uint32_t bar_value = pci_config_read_dword(bus, device, function, bar_offset);
    
    // Check bit 0 to determine if this is an I/O or Memory BAR
    if (bar_value & 0x1) {
        return PCI_BAR_IO;
    } else {
        // Memory BAR - check bits 1-2 to determine type
        switch ((bar_value >> 1) & 0x3) {
            case 0x0: return PCI_BAR_MEM32;  // 32-bit Memory BAR
            case 0x1: return PCI_BAR_MEM16;  // 16-bit Memory BAR (Below 1MB)
            case 0x2: return PCI_BAR_MEM64;  // 64-bit Memory BAR
            default: return PCI_BAR_UNKNOWN; // Reserved
        }
    }
}

/* Get the base address of a PCI BAR, masking out the type bits */
uint64_t pci_get_bar_address(uint8_t bus, uint8_t device, uint8_t function, uint8_t bar_num) {
    uint32_t bar_offset = 0x10 + (bar_num * 4);
    uint32_t bar_value = pci_config_read_dword(bus, device, function, bar_offset);
    enum pci_bar_type type = pci_get_bar_type(bus, device, function, bar_num);
    
    // For 64-bit BAR, we need to read the next BAR for the high 32 bits
    uint64_t addr = 0;
    
    switch (type) {
        case PCI_BAR_IO:
            // I/O BARs use the lower 16 bits for the address, mask out the lowest bit (type)
            return (uint64_t)(bar_value & ~0x3);
            
        case PCI_BAR_MEM16:
        case PCI_BAR_MEM32:
            // 16-bit and 32-bit Memory BARs, mask out the lower 4 bits (type and prefetchable bit)
            return (uint64_t)(bar_value & ~0xF);
            
        case PCI_BAR_MEM64:
            // 64-bit Memory BAR, need to read high 32 bits from next BAR
            addr = (uint64_t)(bar_value & ~0xF);
            bar_offset += 4; // Move to the next BAR
            bar_value = pci_config_read_dword(bus, device, function, bar_offset);
            addr |= ((uint64_t)bar_value << 32);
            return addr;
            
        default:
            return 0;
    }
}

/* Initialize the PCI subsystem */
void init_pci() {
    printf("Initializing PCI subsystem...\n");
    // Not much to do for basic PCI initialization
    // Just scan for devices to check if PCI is functioning properly
    enumerate_pci_devices();
    printf("PCI initialization complete.\n");
}

/* List NVMe devices using the PCI subsystem */
void list_nvme_devices() {
    uint8_t bus, device, function;
    if (pci_find_device(0x01, 0x08, 0x02, &bus, &device, &function)) {
        printf("NVMe device found at %02X:%02X.%X\n", bus, device, function);
        struct pci_device dev = pci_get_device_info(bus, device, function);
        printf("Vendor ID: %04X, Device ID: %04X, Name: %s\n",
               dev.vendor_id, dev.device_id, dev.name ? dev.name : "Unknown");
        // Handle backward compatibility for PCI addresses
        char pci_address[256];
        snprintf(pci_address, sizeof(pci_address), "%02X:%02X.%X", bus, device, function);
        printf("PCI Address: %s\n", pci_address);
        
        // Initialize the NVMe device here
        initialize_nvme_device(bus, device, function);
    } else {
        printf("No NVMe devices found.\n");
    }
}

/* Initialize NVMe device */
void initialize_nvme_device(uint8_t bus, uint8_t device, uint8_t function) {
    printf("Initializing NVMe device at %02X:%02X.%X\n", bus, device, function);
    
    // Enable bus mastering
    pci_enable_bus_mastering(bus, device, function);
    
    // Map the BARs
    uint64_t bar0 = pci_get_bar_address(bus, device, function, 0);
    uint64_t bar1 = pci_get_bar_address(bus, device, function, 1);
    
    printf("BAR0: 0x%016llX\n", bar0);
    printf("BAR1: 0x%016llX\n", bar1);
    
    // NVMe identify command
    uint32_t* nvme_regs = (uint32_t*)bar0;
    nvme_regs[0] = 1; // Enable NVMe controller
    // Additional NVMe initialization steps
    printf("Sending identify command to NVMe controller...\n");
    nvme_regs[1] = 0x00000006; // Set opcode for identify command
    
    // Wait for completion
    while (nvme_regs[1] & 0x00000001) {
        // Spin until command is complete
    }
    
    // Perform read/write test on the first NVMe device found
    if (pci_find_device(0x01, 0x08, 0x02, &bus, &device, &function)) {
        nvme_read_write_test(bus, device, function);
    }
    printf("NVMe device initialization complete.\n");
}
/* Read/Write test file on NVMe device */
void nvme_read_write_test(uint8_t bus, uint8_t device, uint8_t function) {
    printf("Performing read/write test on NVMe device at %02X:%02X.%X\n", bus, device, function);
    
    uint64_t bar0 = pci_get_bar_address(bus, device, function, 0);
    if (bar0 == 0) {
        printf("Failed to map BAR0 for NVMe device.\n");
        return;
    }

    uint32_t* nvme_regs = (uint32_t*)bar0;
    
    // Write test
    printf("Writing to NVMe device...\n");
    nvme_regs[2] = 0xDEADBEEF; // Write a test value
    printf("Write complete. Value written: 0xDEADBEEF\n");
    
    // Read test
    printf("Reading from NVMe device...\n");
    uint32_t read_value = nvme_regs[2];
    printf("Read value: 0x%08X\n", read_value);
    
    if (read_value == 0xDEADBEEF) {
        printf("Read/Write test successful.\n");
    } else {
        printf("Read/Write test failed. Expected: 0xDEADBEEF, Got: 0x%08X\n", read_value);
    }
}
int nvme_test() {
    
    // List NVMe devices
    list_nvme_devices();
    
    
    return 0;
}