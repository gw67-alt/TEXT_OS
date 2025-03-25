#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include "stdio.h"
#include "driver.h"


// PCI Configuration Space Access Ports
#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

// PCI Class Codes
#define PCI_CLASS_STORAGE             0x01
#define PCI_SUBCLASS_SATA             0x06
#define PCI_PROG_IF_AHCI              0x01

// PCI Configuration Space Register Offsets
#define PCI_VENDOR_ID                 0x00 // 2 bytes
#define PCI_DEVICE_ID                 0x02 // 2 bytes
#define PCI_COMMAND                   0x04 // 2 bytes
#define PCI_STATUS                    0x06 // 2 bytes
#define PCI_REVISION_ID               0x08 // 1 byte
#define PCI_PROG_IF                   0x09 // 1 byte
#define PCI_SUBCLASS                  0x0A // 1 byte
#define PCI_CLASS                     0x0B // 1 byte
#define PCI_HEADER_TYPE               0x0E // 1 byte
#define PCI_BAR5                      0x24 // 4 bytes (AHCI base address register)
#define PCI_CAPABILITY_LIST           0x34 // 1 byte

// PCI Command Register Bits
#define PCI_COMMAND_IO_SPACE          0x0001
#define PCI_COMMAND_MEMORY_SPACE      0x0002
#define PCI_COMMAND_BUS_MASTER        0x0004
#define PCI_COMMAND_INTERRUPTS        0x0400

// Common PCI Class Codes for device identification
typedef struct {
    uint8_t class_code;
    uint8_t subclass;
    const char* name;
} pci_class_name_t;

// Table of common PCI device classes
static const pci_class_name_t pci_class_names[] = {
    {0x00, 0x00, "Non-VGA-Compatible Unclassified Device"},
    {0x00, 0x01, "VGA-Compatible Unclassified Device"},
    {0x01, 0x00, "SCSI Bus Controller"},
    {0x01, 0x01, "IDE Controller"},
    {0x01, 0x02, "Floppy Disk Controller"},
    {0x01, 0x03, "IPI Bus Controller"},
    {0x01, 0x04, "RAID Controller"},
    {0x01, 0x05, "ATA Controller"},
    {0x01, 0x06, "SATA Controller"},
    {0x01, 0x07, "Serial Attached SCSI Controller"},
    {0x01, 0x08, "Non-Volatile Memory Controller"},
    {0x01, 0x80, "Other Mass Storage Controller"},
    {0x02, 0x00, "Ethernet Controller"},
    {0x02, 0x01, "Token Ring Controller"},
    {0x02, 0x02, "FDDI Controller"},
    {0x02, 0x03, "ATM Controller"},
    {0x02, 0x04, "ISDN Controller"},
    {0x02, 0x05, "WorldFip Controller"},
    {0x02, 0x06, "PICMG 2.14 Multi Computing"},
    {0x02, 0x07, "Infiniband Controller"},
    {0x02, 0x08, "Fabric Controller"},
    {0x02, 0x80, "Other Network Controller"},
    {0x03, 0x00, "VGA Compatible Controller"},
    {0x03, 0x01, "XGA Controller"},
    {0x03, 0x02, "3D Controller"},
    {0x03, 0x80, "Other Display Controller"},
    {0x04, 0x00, "Multimedia Video Controller"},
    {0x04, 0x01, "Multimedia Audio Controller"},
    {0x04, 0x02, "Computer Telephony Device"},
    {0x04, 0x03, "Audio Device"},
    {0x04, 0x80, "Other Multimedia Controller"},
    {0x05, 0x00, "RAM Controller"},
    {0x05, 0x01, "Flash Controller"},
    {0x05, 0x80, "Other Memory Controller"},
    {0x06, 0x00, "Host Bridge"},
    {0x06, 0x01, "ISA Bridge"},
    {0x06, 0x02, "EISA Bridge"},
    {0x06, 0x03, "MCA Bridge"},
    {0x06, 0x04, "PCI-to-PCI Bridge"},
    {0x06, 0x05, "PCMCIA Bridge"},
    {0x06, 0x06, "NuBus Bridge"},
    {0x06, 0x07, "CardBus Bridge"},
    {0x06, 0x08, "RACEway Bridge"},
    {0x06, 0x09, "PCI-to-PCI Bridge"},
    {0x06, 0x0A, "InfiniBand-to-PCI Host Bridge"},
    {0x06, 0x80, "Other Bridge"},
    {0x07, 0x00, "Serial Controller"},
    {0x07, 0x01, "Parallel Controller"},
    {0x07, 0x02, "Multiport Serial Controller"},
    {0x07, 0x03, "Modem"},
    {0x07, 0x04, "GPIB Controller"},
    {0x07, 0x05, "Smart Card Controller"},
    {0x07, 0x80, "Other Communications Device"},
    {0x08, 0x00, "PIC"},
    {0x08, 0x01, "DMA Controller"},
    {0x08, 0x02, "Timer"},
    {0x08, 0x03, "RTC Controller"},
    {0x08, 0x04, "PCI Hot-Plug Controller"},
    {0x08, 0x05, "SD Host Controller"},
    {0x08, 0x06, "IOMMU"},
    {0x08, 0x80, "Other System Peripheral"},
    {0x09, 0x00, "Keyboard Controller"},
    {0x09, 0x01, "Digitizer Pen"},
    {0x09, 0x02, "Mouse Controller"},
    {0x09, 0x03, "Scanner Controller"},
    {0x09, 0x04, "Gameport Controller"},
    {0x09, 0x80, "Other Input Controller"},
    {0x0A, 0x00, "Generic Docking Station"},
    {0x0A, 0x80, "Other Docking Station"},
    {0x0B, 0x00, "386 Processor"},
    {0x0B, 0x01, "486 Processor"},
    {0x0B, 0x02, "Pentium Processor"},
    {0x0B, 0x10, "Alpha Processor"},
    {0x0B, 0x20, "PowerPC Processor"},
    {0x0B, 0x30, "MIPS Processor"},
    {0x0B, 0x40, "Co-Processor"},
    {0x0B, 0x80, "Other Processor"},
    {0x0C, 0x00, "FireWire (IEEE 1394) Controller"},
    {0x0C, 0x01, "ACCESS Bus Controller"},
    {0x0C, 0x02, "SSA Controller"},
    {0x0C, 0x03, "USB Controller"},
    {0x0C, 0x04, "Fibre Channel Controller"},
    {0x0C, 0x05, "SMBus Controller"},
    {0x0C, 0x06, "InfiniBand Controller"},
    {0x0C, 0x07, "IPMI Interface"},
    {0x0C, 0x08, "SERCOS Interface"},
    {0x0C, 0x09, "CANbus Controller"},
    {0x0C, 0x80, "Other Serial Bus Controller"},
    {0x0D, 0x00, "IRDA Controller"},
    {0x0D, 0x01, "Consumer IR Controller"},
    {0x0D, 0x10, "RF Controller"},
    {0x0D, 0x11, "Bluetooth Controller"},
    {0x0D, 0x12, "Broadband Controller"},
    {0x0D, 0x20, "Ethernet Controller (802.1a)"},
    {0x0D, 0x21, "Ethernet Controller (802.1b)"},
    {0x0D, 0x80, "Other Wireless Controller"},
    {0x0E, 0x00, "I2O Controller"},
    {0x0F, 0x01, "Satellite TV Controller"},
    {0x0F, 0x02, "Satellite Audio Controller"},
    {0x0F, 0x03, "Satellite Voice Controller"},
    {0x0F, 0x04, "Satellite Data Controller"},
    {0x10, 0x00, "Network and Computing Encryption Device"},
    {0x10, 0x10, "Entertainment Encryption Device"},
    {0x10, 0x80, "Other Encryption Controller"},
    {0x11, 0x00, "DPIO Modules"},
    {0x11, 0x01, "Performance Counters"},
    {0x11, 0x10, "Communications Synchronization Plus Time and Frequency Test/Measurement"},
    {0x11, 0x20, "Management Card"},
    {0x11, 0x80, "Other Data Acquisition/Signal Processing Controller"},
    {0xFF, 0xFF, "Unknown Device"}
};

// Create a PCI address from bus, device, function, and register
static uint32_t pci_make_address(uint8_t bus, uint8_t device, uint8_t function, uint8_t reg) {
    uint32_t address = 0;
    address |= (uint32_t)0x80000000;    // Enable bit (bit 31)
    address |= (uint32_t)(bus << 16);   // Bus (bits 16-23)
    address |= (uint32_t)(device << 11); // Device (bits 11-15)
    address |= (uint32_t)(function << 8); // Function (bits 8-10)
    address |= (uint32_t)(reg & 0xFC);  // Register (bits 2-7)
    return address;
}

// Read 8 bits from PCI configuration space
static uint8_t pci_config_read8(uint8_t bus, uint8_t device, uint8_t function, uint8_t reg) {
    uint32_t address = pci_make_address(bus, device, function, reg);
    outl(PCI_CONFIG_ADDRESS, address);
    
    // Read from the appropriate byte of the 32-bit register
    uint8_t offset = reg & 0x03;
    uint8_t value = (uint8_t)((inl(PCI_CONFIG_DATA) >> (offset * 8)) & 0xFF);
    return value;
}

// Read 16 bits from PCI configuration space
static uint16_t pci_config_read16(uint8_t bus, uint8_t device, uint8_t function, uint8_t reg) {
    uint32_t address = pci_make_address(bus, device, function, reg);
    outl(PCI_CONFIG_ADDRESS, address);
    
    // Read from the appropriate 16-bits of the 32-bit register
    uint8_t offset = (reg & 0x02) >> 1;
    uint16_t value = (uint16_t)((inl(PCI_CONFIG_DATA) >> (offset * 16)) & 0xFFFF);
    return value;
}

// Read 32 bits from PCI configuration space
static uint32_t pci_config_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t reg) {
    uint32_t address = pci_make_address(bus, device, function, reg);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

// Write 32 bits to PCI configuration space
static void pci_config_write32(uint8_t bus, uint8_t device, uint8_t function, uint8_t reg, uint32_t value) {
    uint32_t address = pci_make_address(bus, device, function, reg);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

// Write 16 bits to PCI configuration space
static void pci_config_write16(uint8_t bus, uint8_t device, uint8_t function, uint8_t reg, uint16_t value) {
    uint32_t address = pci_make_address(bus, device, function, reg);
    outl(PCI_CONFIG_ADDRESS, address);
    
    uint32_t data = inl(PCI_CONFIG_DATA);
    uint8_t offset = (reg & 0x02) >> 1;
    data &= ~(0xFFFF << (offset * 16));
    data |= ((uint32_t)value << (offset * 16));
    
    outl(PCI_CONFIG_DATA, data);
}

// Check if a PCI device exists
static bool pci_device_exists(uint8_t bus, uint8_t device, uint8_t function) {
    uint16_t vendor = pci_config_read16(bus, device, function, PCI_VENDOR_ID);
    return vendor != 0xFFFF; // 0xFFFF indicates no device
}

// Get device type name based on class and subclass codes
static const char* pci_get_device_type_name(uint8_t class_code, uint8_t subclass) {
    for (int i = 0; i < sizeof(pci_class_names) / sizeof(pci_class_name_t); i++) {
        if (pci_class_names[i].class_code == class_code && 
            pci_class_names[i].subclass == subclass) {
            return pci_class_names[i].name;
        }
    }
    
    // If not found, return the last entry which is "Unknown Device"
    return pci_class_names[sizeof(pci_class_names) / sizeof(pci_class_name_t) - 1].name;
}

// Check if a device is an AHCI controller
static bool is_ahci_controller(uint8_t bus, uint8_t device, uint8_t function) {
    uint8_t class_code = pci_config_read8(bus, device, function, PCI_CLASS);
    uint8_t subclass = pci_config_read8(bus, device, function, PCI_SUBCLASS);
    uint8_t prog_if = pci_config_read8(bus, device, function, PCI_PROG_IF);
    
    return (class_code == PCI_CLASS_STORAGE &&
            subclass == PCI_SUBCLASS_SATA &&
            prog_if == PCI_PROG_IF_AHCI);
}

// Enable AHCI controller in PCI configuration
static void enable_ahci_controller(uint8_t bus, uint8_t device, uint8_t function) {
    uint16_t command = pci_config_read16(bus, device, function, PCI_COMMAND);
    
    // Enable memory space, bus mastering, and interrupts
    command |= PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER;
    
    // Write back the command register
    pci_config_write16(bus, device, function, PCI_COMMAND, command);
}

// Get a vendor name from vendor ID (limited set of common vendors)
static const char* pci_get_vendor_name(uint16_t vendor_id) {
    switch (vendor_id) {
        case 0x1022: return "AMD";
        case 0x1039: return "SiS";
        case 0x104C: return "Texas Instruments";
        case 0x10DE: return "NVIDIA";
        case 0x10EC: return "Realtek";
        case 0x1106: return "VIA";
        case 0x1234: return "Bochs/QEMU";
        case 0x15AD: return "VMware";
        case 0x1AF4: return "Red Hat/Qumranet (Virtio)";
        case 0x8086: return "Intel";
        case 0x80EE: return "VirtualBox";
        default: return "Unknown Vendor";
    }
}

// Enumerate all PCI devices and print information about them
void enumerate_pci_devices() {
    char buffer[100];
    int device_count = 0;
    
    printf("==== PCI Device Enumeration ====\n");
    
    // Scan all buses, devices, and functions
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t function = 0; function < 8; function++) {
                // Check if device exists
                if (!pci_device_exists(bus, device, function)) {
                    // Skip to next device if function 0 doesn't exist
                    if (function == 0) break;
                    else continue;
                }
                ++device_count;
                // Device found - get details
                uint16_t vendor_id = pci_config_read16(bus, device, function, PCI_VENDOR_ID);
                uint16_t device_id = pci_config_read16(bus, device, function, PCI_DEVICE_ID);
                uint8_t class_code = pci_config_read8(bus, device, function, PCI_CLASS);
                uint8_t subclass = pci_config_read8(bus, device, function, PCI_SUBCLASS);
                uint8_t prog_if = pci_config_read8(bus, device, function, PCI_PROG_IF);
                uint8_t revision = pci_config_read8(bus, device, function, PCI_REVISION_ID);
                uint8_t header_type = pci_config_read8(bus, device, function, PCI_HEADER_TYPE) & 0x7F;
            }
        }
    }
    
    if (device_count == 0) {
        printf("No PCI devices found.\n");
    } else {
        printf("Total PCI devices found: %d", device_count);
		printf("\n");
    }
    
    printf("==== End of PCI Enumeration ====\n\n");
}

// Find an AHCI controller and return its base address
uint32_t find_ahci_controller() {
    char buffer[100];
    printf("Scanning PCI bus for AHCI controller...\n");
    
    // Scan all buses, devices, and functions
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t function = 0; function < 8; function++) {
                // Check if device exists
                if (!pci_device_exists(bus, device, function)) {
                    // Skip to next device if function 0 doesn't exist
                    if (function == 0) break;
                    else continue;
                }
                
                // Check if this is an AHCI controller
                if (is_ahci_controller(bus, device, function)) {
                    printf("Found AHCI controller at PCI %d:%d.%d\n", 
                            bus, device, function);
                    
                    
                    // Get vendor and device IDs for debugging
                    uint16_t vendor = pci_config_read16(bus, device, function, PCI_VENDOR_ID);
                    uint16_t dev_id = pci_config_read16(bus, device, function, PCI_DEVICE_ID);
                    printf("Vendor: %s (0x%x), Device: 0x%x", 
                            pci_get_vendor_name(vendor), vendor, dev_id);
					printf("\n");
                    
                    
                    // Enable the controller
                    enable_ahci_controller(bus, device, function);
                    
                    // Get the AHCI base address from BAR5
                    uint32_t bar5 = pci_config_read32(bus, device, function, PCI_BAR5);
                    
                    // Extract the base address (mask off the lower bits)
                    uint32_t abar = bar5 & 0xFFFFFFF0;
                    
                    printf("AHCI Base Address: 0x%x\n", abar);
                    
                    
                    return abar;
                }
            }
        }
    }
    
    printf("No AHCI controller found on PCI bus.\n");
    return 0;
}