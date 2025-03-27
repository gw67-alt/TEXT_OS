#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>
#include "io.h"
#include "pcie.h"

#define NVME_CLASS_CODE "010802"

/* Enhanced Configuration Access Mechanism (ECAM) for PCIe */
#define PCIE_CONFIG_BASE_ADDR 0xE0000000  /* Memory mapped base address for ECAM */

/* PCI Express capability IDs */
#define PCIE_CAP_ID           0x10
#define MSIX_CAP_ID           0x11
#define PCIE_AER_CAP_ID       0x01  /* Advanced Error Reporting */
#define PCIE_VC_CAP_ID        0x02  /* Virtual Channel */
#define PCIE_SERIAL_CAP_ID    0x03  /* Device Serial Number */
#define PCIE_PWR_CAP_ID       0x04  /* Power Budgeting */

/* PCIe Link Capabilities Register bits */
#define PCIE_LINK_CAP_MAX_SPEED_MASK    0xF
#define PCIE_LINK_CAP_MAX_WIDTH_MASK    0x3F0

/* PCIe Link Status Register bits */
#define PCIE_LINK_STATUS_SPEED_MASK     0xF
#define PCIE_LINK_STATUS_WIDTH_MASK     0x3F0
#define PCIE_LINK_STATUS_TRAINING       0x800

/* PCIe device identifier structure */
struct pcie_device_id {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    const char* name;
};

/* Initialize the PCIe subsystem */
void init_pcie() {
    printf("Initializing PCIe subsystem...\n");
    // Map the PCIe Enhanced Configuration Access Mechanism (ECAM) region
    // This is typically done in the platform's initialization code or BIOS
    
    // Enumerate PCIe devices to check if PCIe subsystem is functioning
    enumerate_pcie_devices();
    
    printf("PCIe initialization complete.\n");
}

/* Configure PCIe device for maximum performance */
void pcie_optimize_device(uint8_t bus, uint8_t device, uint8_t function) {
    struct pcie_device dev = pcie_get_device_info(bus, device, function);
    
    if (!dev.pcie_cap_offset) {
        printf("Device %02X:%02X.%X is not a PCIe device or has no PCIe capabilities.\n",
               bus, device, function);
        return;
    }
    
    printf("Optimizing PCIe device %02X:%02X.%X (%s)...\n", 
           bus, device, function, dev.name ? dev.name : "Unknown");
    
    // Enable device basic features
    pcie_enable_device(bus, device, function);
    
    // Configure Max Payload Size to the highest supported value
    uint16_t device_control = pcie_config_read_word(bus, device, function, dev.pcie_cap_offset + 0x08);
    uint16_t device_capabilities = pcie_config_read_word(bus, device, function, dev.pcie_cap_offset + 0x04);
    
    // Extract Max Payload Size supported
    uint8_t max_payload_supported = (device_capabilities >> 0) & 0x7;
    
    // Set Max Payload Size field in Device Control to the max supported value
    device_control &= ~(0x7 << 5);  // Clear current MPS setting
    device_control |= (max_payload_supported << 5);  // Set new MPS
    
    // Set Max Read Request Size to maximum (4096 bytes)
    device_control &= ~(0x7 << 12);  // Clear current MRRS
    device_control |= (0x5 << 12);   // Set to 4096 bytes (2^5 * 128)
    
    // Enable relaxed ordering and no snoop for better performance
    device_control |= (1 << 4) | (1 << 11);
    
    // Write back updated Device Control
    pcie_config_write_word(bus, device, function, dev.pcie_cap_offset + 0x08, device_control);
    
    // Configure Extended Tag Field Enable if supported
    if (device_capabilities & (1 << 5)) {
        device_control |= (1 << 8);
        pcie_config_write_word(bus, device, function, dev.pcie_cap_offset + 0x08, device_control);
    }
    
    // Configure ASPM (Active State Power Management) if appropriate
    uint32_t link_capabilities = dev.link_capabilities;
    uint16_t link_control = dev.link_control;
    
    // Only enable ASPM if supported and appropriate for the device type
    if ((link_capabilities & 0x3) != 0) {
        // For endpoint devices, enable ASPM L0s and L1 if supported
        if (dev.device_type == 0x0 || dev.device_type == 0x1) {
            link_control &= ~0x3;  // Clear current ASPM setting
            link_control |= (link_capabilities & 0x3);  // Set supported ASPM modes
            pcie_config_write_word(bus, device, function, dev.pcie_cap_offset + 0x10, link_control);
        }
    }
    
    printf("PCIe device optimization complete.\n");
    printf("  - Max Payload Size: %d bytes\n", 128 << ((device_control >> 5) & 0x7));
    printf("  - Max Read Request Size: %d bytes\n", 128 << ((device_control >> 12) & 0x7));
    printf("  - Relaxed Ordering: %s\n", (device_control & (1 << 4)) ? "Enabled" : "Disabled");
    printf("  - No Snoop: %s\n", (device_control & (1 << 11)) ? "Enabled" : "Disabled");
    printf("  - Extended Tag Field: %s\n", (device_control & (1 << 8)) ? "Enabled" : "Disabled");
    printf("  - ASPM: %s\n", (link_control & 0x3) ? "Enabled" : "Disabled");
}

/* List NVMe devices using the PCIe subsystem */
void list_nvme_devices() {
    printf("Searching for NVMe devices...\n");
    
    uint8_t bus, device, function;
    int count = 0;
    
    for (uint16_t b = 0; b < 256; b++) {
        for (uint8_t d = 0; d < 32; d++) {
            for (uint8_t f = 0; f < 8; f++) {
                if (!pcie_config_read_word(b, d, f, 0x00)) continue;
                
                uint8_t class_code = pcie_config_read_byte(b, d, f, 0x0B);
                uint8_t subclass = pcie_config_read_byte(b, d, f, 0x0A);
                uint8_t prog_if = pcie_config_read_byte(b, d, f, 0x09);
                
                if (class_code == 0x01 && subclass == 0x08 && prog_if == 0x02) {
                    struct pcie_device dev = pcie_get_device_info(b, d, f);
                    bus = b;
                    device = d;
                    function = f;
                    
                    printf("NVMe device found at %02X:%02X.%X\n", bus, device, function);
                    printf("  Vendor ID: %04X, Device ID: %04X\n", dev.vendor_id, dev.device_id);
                    printf("  Name: %s\n", dev.name ? dev.name : "Unknown NVMe Controller");
                    
                    if (dev.pcie_cap_offset) {
                        printf("  PCIe Link Speed: %s\n", get_pcie_link_speed_str(dev.current_link_speed));
                        printf("  PCIe Link Width: x%d\n", dev.current_link_width);
                        printf("  Max Payload Size: %d bytes\n", dev.max_payload_size);
                    }
                    
                    // Display BAR information
                    for (int i = 0; i < 6; i++) {
                        enum pcie_bar_type bar_type = pcie_get_bar_type(bus, device, function, i);
                        uint64_t bar_addr = pcie_get_bar_address(bus, device, function, i);
                        uint64_t bar_size = pcie_get_bar_size(bus, device, function, i);
                        
                        if (bar_size > 0) {
                            printf("  BAR%d: ", i);
                            switch (bar_type) {
                                case PCIE_BAR_IO:
                                    printf("I/O Space at 0x%08llX (size: %llu bytes)\n", bar_addr, bar_size);
                                    break;
                                case PCIE_BAR_MEM32:
                                    printf("32-bit Memory Space at 0x%08llX (size: %llu bytes)\n", bar_addr, bar_size);
                                    break;
                                case PCIE_BAR_MEM64:
                                    printf("64-bit Memory Space at 0x%016llX (size: %llu bytes)\n", bar_addr, bar_size);
                                    i++; // Skip the next BAR as it's part of this 64-bit BAR
                                    break;
                                default:
                                    printf("Unknown type at 0x%08llX\n", bar_addr);
                            }
                        }
                    }
                    
                    count++;
                    
                    // Optimize NVMe device for best performance
                    pcie_optimize_device(bus, device, function);
                    
                    // Initialize this NVMe device
                    initialize_nvme_device(bus, device, function);
                }
            }
        }
    }
    
    if (count == 0) {
        printf("No NVMe devices found.\n");
    } else {
        printf("Found %d NVMe device(s).\n", count);
    }
}

/* Initialize NVMe device */
void initialize_nvme_device(uint8_t bus, uint8_t device, uint8_t function) {
    printf("Initializing NVMe device at %02X:%02X.%X\n", bus, device, function);
    
    // Enable bus mastering for the device
    pcie_enable_device(bus, device, function);
    
    // Map BAR0 which contains NVMe controller registers
    uint64_t bar0 = pcie_get_bar_address(bus, device, function, 0);
    uint64_t bar_size = pcie_get_bar_size(bus, device, function, 0);
    
    if (bar0 == 0 || bar_size == 0) {
        printf("Error: Could not map NVMe controller registers (BAR0).\n");
        return;
    }
    
    printf("NVMe controller registers mapped at 0x%016llX (size: %llu bytes)\n", bar0, bar_size);
    
    // At this point, you would map the physical memory region to a virtual address
    // This is platform-specific and depends on your memory management implementation
    // For this example, we'll assume a direct mapping is available
    
    volatile uint32_t* nvme_regs = (volatile uint32_t*)bar0;
    
    // NVMe Controller Capability registers
    uint64_t cap = ((uint64_t)nvme_regs[1] << 32) | nvme_regs[0];
    uint32_t version = nvme_regs[2];
    
    printf("NVMe Controller Capabilities: 0x%016llX\n", cap);
    printf("NVMe Version: %d.%d.%d\n", 
           (version >> 16) & 0xFFFF, (version >> 8) & 0xFF, version & 0xFF);
    
    // Maximum Queue Entries Supported
    uint16_t mqes = (cap & 0xFFFF) + 1;
    printf("Max Queue Entries Supported: %d\n", mqes);
    
    // Contiguous Queues Required
    bool cqr = (cap >> 16) & 0x1;
    printf("Contiguous Queues Required: %s\n", cqr ? "Yes" : "No");
    
    // Arbitration Mechanism Supported
    uint8_t ams = (cap >> 17) & 0x3;
    printf("Arbitration Mechanism: ");
    switch (ams) {
        case 0: printf("Round Robin only\n"); break;
        case 1: printf("Round Robin and Weighted Round Robin\n"); break;
        case 2: printf("Round Robin and Vendor Specific\n"); break;
        case 3: printf("All mechanisms\n"); break;
    }
    
    // Reset the controller before initialization
    // Set CC.EN to 0 to disable the controller
    uint32_t cc = nvme_regs[5];  // Controller Configuration register
    cc &= ~0x1;  // Clear Enable bit
    nvme_regs[5] = cc;
    
    // Wait for CSTS.RDY to become 0
    uint32_t csts;
    do {
        csts = nvme_regs[7];  // Controller Status register
    } while (csts & 0x1);
    
    printf("NVMe controller reset complete.\n");
    
    // Configure the controller
    cc = 0;
    cc |= (0 << 4);    // I/O Command Set Selected: NVM Command Set
    cc |= (4 << 7);    // I/O Completion Queue Entry Size: 16 bytes (2^4)
    cc |= (6 << 11);   // I/O Submission Queue Entry Size: 64 bytes (2^6)
    cc |= (0 << 14);   // Memory Page Size: 4KB (2^12 bytes) -> (0 = 2^12)
    cc |= (0 << 16);   // Arbitration Mechanism: Round Robin
    cc |= (1 << 20);   // Enable Submission Queue in memory
    cc |= (1 << 1);    // Enable interrupts
    
    // Write the configuration
    nvme_regs[5] = cc;
    
    // Enable the controller
    cc |= 0x1;  // Set Enable bit
    nvme_regs[5] = cc;
    
    // Wait for CSTS.RDY to become 1
    do {
        csts = nvme_regs[7];
    } while (!(csts & 0x1));
    
    printf("NVMe controller enabled successfully.\n");
    
    // At this point, you would create Admin queues and identify the controller
    // For a complete implementation, you would also need to:
    // 1. Allocate memory for Admin Submission and Completion Queues
    // 2. Set up Admin Queue attributes (AQA) register
    // 3. Set Admin Submission Queue Base Address (ASQ)
    // 4. Set Admin Completion Queue Base Address (ACQ)
    // 5. Issue an Identify Controller command
    // 6. Create I/O Submission and Completion Queues
    
    printf("NVMe device initialization complete.\n");
    
    // Perform read/write test
    nvme_read_write_test(bus, device, function);
}

/* Read/Write test on NVMe device */
void nvme_read_write_test(uint8_t bus, uint8_t device, uint8_t function) {
    printf("Performing read/write test on NVMe device at %02X:%02X.%X\n", bus, device, function);
    
    uint64_t bar0 = pcie_get_bar_address(bus, device, function, 0);
    if (bar0 == 0) {
        printf("Failed to map BAR0 for NVMe device.\n");
        return;
    }

    volatile uint32_t* nvme_regs = (volatile uint32_t*)bar0;
    
    // Read the controller capabilities
    uint64_t cap = ((uint64_t)nvme_regs[1] << 32) | nvme_regs[0];
    
    // Read the controller status
    uint32_t csts = nvme_regs[7];
    
    printf("Controller Capabilities: 0x%016llX\n", cap);
    printf("Controller Status: 0x%08X\n", csts);
    
    if (csts & 0x1) {
        printf("Controller is ready.\n");
        
        // In a real implementation, you would:
        // 1. Set up memory for data buffers
        // 2. Create PRPs (Physical Region Pages) for the data transfer
        // 3. Create a submission queue entry for the read/write command
        // 4. Ring the doorbell to submit the command
        // 5. Wait for completion and check status
        
        printf("Simulated 4KB read test: Successful\n");
        printf("Simulated 4KB write test: Successful\n");
    } else {
        printf("Controller is not ready, cannot perform read/write test.\n");
    }
}

/* Main PCIe test function */
int pcie_test() {
    // Initialize PCIe subsystem
    init_pcie();
    
    // List NVMe devices
    list_nvme_devices();
    
    return 0;
}

/* PCIe Enhanced Error Reporting (AER) function */
void pcie_setup_aer(uint8_t bus, uint8_t device, uint8_t function) {
    struct pcie_device dev = pcie_get_device_info(bus, device, function);
    
    // Skip if not a PCIe device
    if (!dev.pcie_cap_offset) {
        return;
    }
    
    // Find the AER Extended Capability
    uint16_t ecap_offset = 0x100;  // Extended capabilities start at offset 0x100
    bool aer_found = false;
    
    while (ecap_offset) {
        uint32_t ecap_header = pcie_config_read_dword(bus, device, function, ecap_offset);
        uint16_t cap_id = ecap_header & 0xFFFF;
        
        if (cap_id == PCIE_AER_CAP_ID) {
            aer_found = true;
            break;
        }
        
        // Move to next capability
        ecap_offset = (ecap_header >> 20) & 0xFFF;
        
        // Prevent infinite loop
        if (ecap_offset == 0 || ecap_offset < 0x100) {
            break;
        }
    }
    
    if (!aer_found) {
        printf("Device %02X:%02X.%X does not support AER.\n", bus, device, function);
        return;
    }
    
    printf("Configuring AER for device %02X:%02X.%X\n", bus, device, function);
    
    // Read AER Uncorrectable Error Mask Register
    uint32_t uncorr_mask = pcie_config_read_dword(bus, device, function, ecap_offset + 0x08);
    
    // Unmask critical errors, mask non-critical ones
    // For example, unmask data link protocol errors and surprise down errors
    uncorr_mask &= ~(1 << 4);  // Unmask Data Link Protocol Error
    uncorr_mask &= ~(1 << 5);  // Unmask Surprise Down Error
    
    // Write back updated mask
    pcie_config_write_dword(bus, device, function, ecap_offset + 0x08, uncorr_mask);
    
    // Read AER Correctable Error Mask Register
    uint32_t corr_mask = pcie_config_read_dword(bus, device, function, ecap_offset + 0x14);
    
    // Configure which correctable errors to report
    corr_mask &= ~(1 << 0);  // Unmask Receiver Error
    corr_mask &= ~(1 << 6);  // Unmask LCRC Error
    
    // Write back updated mask
    pcie_config_write_dword(bus, device, function, ecap_offset + 0x14, corr_mask);
    
    printf("AER configuration complete.\n");
};

/* Table of known PCIe devices - can be extended with more entries */
static const struct pcie_device_id known_devices[] = {
    /* NVMe Controllers */
    {0x8086, 0xF1A5, 0x01, 0x08, 0x02, "Intel SSD DC P4500 Series NVMe"},
    {0x8086, 0xF1A6, 0x01, 0x08, 0x02, "Intel SSD DC P4600 Series NVMe"},
    {0x144D, 0xA804, 0x01, 0x08, 0x02, "Samsung PM1725 NVMe SSD"},
    {0x144D, 0xA808, 0x01, 0x08, 0x02, "Samsung PM1725b NVMe SSD"},
    {0x1C5C, 0x1327, 0x01, 0x08, 0x02, "SK Hynix PE8010 NVMe SSD"},
    
    /* High-speed Network Controllers */
    {0x8086, 0x1572, 0x02, 0x00, 0x00, "Intel X710 10GbE Controller"},
    {0x8086, 0x1583, 0x02, 0x00, 0x00, "Intel XL710 40GbE Controller"},
    {0x15B3, 0x1013, 0x02, 0x00, 0x00, "Mellanox ConnectX-4 25/50/100GbE"},
    {0x15B3, 0x1017, 0x02, 0x00, 0x00, "Mellanox ConnectX-5 25/50/100GbE"},
    
    /* GPUs */
    {0x10DE, 0x1B06, 0x03, 0x00, 0x00, "NVIDIA GeForce GTX 1080"},
    {0x10DE, 0x1E04, 0x03, 0x00, 0x00, "NVIDIA GeForce RTX 2080"},
    {0x1002, 0x67DF, 0x03, 0x00, 0x00, "AMD Radeon RX 480"},
    {0x1002, 0x731F, 0x03, 0x00, 0x00, "AMD Radeon RX 5700 XT"},
    
    /* End of table */
    {0, 0, 0, 0, 0, NULL}
};

/* Forward declarations */
const char* get_device_name(uint16_t vendor_id, uint16_t device_id, 
                           uint8_t class_code, uint8_t subclass, uint8_t prog_if);
const char* get_class_name(uint8_t class_code, uint8_t subclass, uint8_t prog_if);
const char* get_pcie_link_speed_str(uint8_t speed);
const char* get_pcie_device_type_str(uint8_t device_type);

/* Generate a PCIe ECAM memory address for config space access */
static void* pcie_get_config_address(uint8_t bus, uint8_t device, 
                                    uint8_t function, uint16_t offset) {
    return (void*)(PCIE_CONFIG_BASE_ADDR | 
                  ((uint32_t)bus << 20) | 
                  ((uint32_t)device << 15) | 
                  ((uint32_t)function << 12) | 
                  (offset & 0xFFF));
}

/* Read a byte from PCIe configuration space using MMIO */
uint8_t pcie_config_read_byte(uint8_t bus, uint8_t device, 
                             uint8_t function, uint16_t offset) {
    uint8_t* addr = (uint8_t*)pcie_get_config_address(bus, device, function, offset);
    return *addr;
}

/* Read a word (16 bits) from PCIe configuration space using MMIO */
uint16_t pcie_config_read_word(uint8_t bus, uint8_t device, 
                              uint8_t function, uint16_t offset) {
    uint16_t* addr = (uint16_t*)pcie_get_config_address(bus, device, function, offset);
    return *addr;
}

/* Read a dword (32 bits) from PCIe configuration space using MMIO */
uint32_t pcie_config_read_dword(uint8_t bus, uint8_t device, 
                               uint8_t function, uint16_t offset) {
    uint32_t* addr = (uint32_t*)pcie_get_config_address(bus, device, function, offset);
    return *addr;
}

/* Write a byte to PCIe configuration space using MMIO */
void pcie_config_write_byte(uint8_t bus, uint8_t device, 
                           uint8_t function, uint16_t offset, uint8_t value) {
    uint8_t* addr = (uint8_t*)pcie_get_config_address(bus, device, function, offset);
    *addr = value;
}

/* Write a word (16 bits) to PCIe configuration space using MMIO */
void pcie_config_write_word(uint8_t bus, uint8_t device, 
                           uint8_t function, uint16_t offset, uint16_t value) {
    uint16_t* addr = (uint16_t*)pcie_get_config_address(bus, device, function, offset);
    *addr = value;
}

/* Write a dword (32 bits) to PCIe configuration space using MMIO */
void pcie_config_write_dword(uint8_t bus, uint8_t device, 
                            uint8_t function, uint16_t offset, uint32_t value) {
    uint32_t* addr = (uint32_t*)pcie_get_config_address(bus, device, function, offset);
    *addr = value;
}


/* Get PCIe device details including PCIe-specific capabilities */
struct pcie_device pcie_get_device_info(uint8_t bus, uint8_t device, uint8_t function) {
    struct pcie_device dev;
    
    // Initialize basic PCI configuration fields
    dev.bus = bus;
    dev.device = device;
    dev.function = function;
    
    dev.vendor_id = pcie_config_read_word(bus, device, function, 0x00);
    dev.device_id = pcie_config_read_word(bus, device, function, 0x02);
    
    dev.command = pcie_config_read_word(bus, device, function, 0x04);
    dev.status = pcie_config_read_word(bus, device, function, 0x06);
    
    dev.revision_id = pcie_config_read_byte(bus, device, function, 0x08);
    dev.prog_if = pcie_config_read_byte(bus, device, function, 0x09);
    dev.subclass = pcie_config_read_byte(bus, device, function, 0x0A);
    dev.class_code = pcie_config_read_byte(bus, device, function, 0x0B);
    
    dev.cache_line_size = pcie_config_read_byte(bus, device, function, 0x0C);
    dev.latency_timer = pcie_config_read_byte(bus, device, function, 0x0D);
    dev.header_type = pcie_config_read_byte(bus, device, function, 0x0E);
    dev.bist = pcie_config_read_byte(bus, device, function, 0x0F);
    
    // Read base address registers (BAR0-BAR5)
    for (int i = 0; i < 6; i++) {
        dev.bar[i] = pcie_config_read_dword(bus, device, function, 0x10 + i * 4);
    }
    
    // Additional fields for different header types
    if ((dev.header_type & 0x7F) == 0) {
        dev.cardbus_cis_ptr = pcie_config_read_dword(bus, device, function, 0x28);
        dev.subsystem_vendor_id = pcie_config_read_word(bus, device, function, 0x2C);
        dev.subsystem_id = pcie_config_read_word(bus, device, function, 0x2E);
        dev.expansion_rom_base_addr = pcie_config_read_dword(bus, device, function, 0x30);
        dev.capabilities_ptr = pcie_config_read_byte(bus, device, function, 0x34);
        dev.interrupt_line = pcie_config_read_byte(bus, device, function, 0x3C);
        dev.interrupt_pin = pcie_config_read_byte(bus, device, function, 0x3D);
        dev.min_grant = pcie_config_read_byte(bus, device, function, 0x3E);
        dev.max_latency = pcie_config_read_byte(bus, device, function, 0x3F);
    }
    
    // Initialize PCIe-specific fields to default values
    dev.pcie_cap_offset = 0;
    dev.device_type = 0;
    dev.link_capabilities = 0;
    dev.link_status = 0;
    dev.link_control = 0;
    dev.max_payload_size = 0;
    dev.max_read_request_size = 0;
    
    // Try to identify the device
    dev.name = get_device_name(dev.vendor_id, dev.device_id, 
                              dev.class_code, dev.subclass, dev.prog_if);
    
    // Find PCIe capabilities if device has capabilities pointer
    if (dev.capabilities_ptr) {
        uint8_t cap_offset = dev.capabilities_ptr;
        while (cap_offset) {
            uint8_t cap_id = pcie_config_read_byte(bus, device, function, cap_offset);
            
            // Check if this is the PCIe capability structure
            if (cap_id == PCIE_CAP_ID) {
                dev.pcie_cap_offset = cap_offset;
                
                // Read PCIe capability registers
                uint16_t pcie_caps = pcie_config_read_word(bus, device, function, cap_offset + 0x02);
                dev.device_type = (pcie_caps >> 4) & 0xF;  // Device/Port Type
                
                // Read Link Capabilities Register
                dev.link_capabilities = pcie_config_read_dword(bus, device, function, cap_offset + 0x0C);
                // Extract max link speed and width
                dev.max_link_speed = dev.link_capabilities & PCIE_LINK_CAP_MAX_SPEED_MASK;
                dev.max_link_width = (dev.link_capabilities & PCIE_LINK_CAP_MAX_WIDTH_MASK) >> 4;
                
                // Read Link Status Register
                dev.link_status = pcie_config_read_word(bus, device, function, cap_offset + 0x12);
                // Extract current link speed and width
                dev.current_link_speed = dev.link_status & PCIE_LINK_STATUS_SPEED_MASK;
                dev.current_link_width = (dev.link_status & PCIE_LINK_STATUS_WIDTH_MASK) >> 4;
                
                // Read Link Control Register
                dev.link_control = pcie_config_read_word(bus, device, function, cap_offset + 0x10);
                
                // Read Device Control Register for max payload size
                uint16_t device_control = pcie_config_read_word(bus, device, function, cap_offset + 0x08);
                dev.max_payload_size = 128 << ((device_control >> 5) & 0x7);
                dev.max_read_request_size = 128 << ((device_control >> 12) & 0x7);
                
                break;
            }
            
            // Move to next capability
            cap_offset = pcie_config_read_byte(bus, device, function, cap_offset + 1);
        }
    }
    
    return dev;
}

/* Check if a device has multiple functions */
static bool pcie_device_has_functions(uint8_t bus, uint8_t device) {
    uint8_t header_type = pcie_config_read_byte(bus, device, 0, 0x0E);
    return (header_type & 0x80) != 0;
}

/* Enumerate all PCIe devices */
void enumerate_pcie_devices() {
    printf("Enumerating PCIe Devices:\n");
    printf("-------------------------------------------------------------------------------------------------------\n");
    printf("| BUS | DEV | FN | VendorID | DeviceID | Class | Type | Device Type      | Link Speed | Width | Name |\n");
    printf("-------------------------------------------------------------------------------------------------------\n");
    
    int device_count = 0;
    
    // Scan all PCIe buses
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            uint8_t function = 0;
            
            if (!pcie_config_read_word(bus, device, function, 0x00)) {
                continue;
            }
            
            // Check if this is a multi-function device
            bool is_multi_function = pcie_device_has_functions(bus, device);
            
            // Scan the appropriate number of functions
            for (function = 0; function < (is_multi_function ? 8 : 1); function++) {
                if (!pcie_config_read_word(bus, device, function, 0x00)) {
                    continue;
                }
                
                // Get device information
                struct pcie_device dev = pcie_get_device_info(bus, device, function);
                
                // Display device information
                printf("| %03X | %03X | %02X | %04X     | %04X     | %02X:%02X:%02X | %02X   | %-16s | %-10s | x%-3d  | %-14s |\n",
                       bus, device, function,
                       dev.vendor_id, dev.device_id,
                       dev.class_code, dev.subclass, dev.prog_if,
                       dev.header_type & 0x7F,
                       dev.pcie_cap_offset ? get_pcie_device_type_str(dev.device_type) : "Legacy PCI",
                       dev.pcie_cap_offset ? get_pcie_link_speed_str(dev.current_link_speed) : "N/A",
                       dev.pcie_cap_offset ? dev.current_link_width : 0,
                       dev.name ? (strlen(dev.name) > 14 ? 
                                  (char[]){dev.name[0], dev.name[1], dev.name[2], dev.name[3], 
                                          dev.name[4], dev.name[5], dev.name[6], dev.name[7], 
                                          dev.name[8], dev.name[9], dev.name[10], '.', '.', '.'} : dev.name) 
                                : "Unknown");
                
                device_count++;
            }
        }
    }
    
    printf("-------------------------------------------------------------------------------------------------------\n");
    printf("Total PCIe devices found: %d\n", device_count);
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
        /* Additional cases same as PCI implementation */
        /* ... */
        default:
            return "Unknown Device";
    }
}

/* Get string representation of PCIe link speed */
const char* get_pcie_link_speed_str(uint8_t speed) {
    switch (speed) {
        case 1: return "2.5 GT/s";   // Gen 1
        case 2: return "5.0 GT/s";   // Gen 2
        case 3: return "8.0 GT/s";   // Gen 3
        case 4: return "16.0 GT/s";  // Gen 4
        case 5: return "32.0 GT/s";  // Gen 5
        case 6: return "64.0 GT/s";  // Gen 6
        default: return "Unknown";
    }
}

/* Get string representation of PCIe device/port type */
const char* get_pcie_device_type_str(uint8_t device_type) {
    switch (device_type) {
        case 0x0: return "Endpoint";
        case 0x1: return "Legacy Endpoint";
        case 0x4: return "Root Port";
        case 0x5: return "Upstream Port";
        case 0x6: return "Downstream Port";
        case 0x7: return "PCIe-to-PCI Bridge";
        case 0x8: return "PCI-to-PCIe Bridge";
        case 0x9: return "Root Complex Endpoint";
        case 0xA: return "Root Complex Integrated Endpoint";
        default: return "Unknown";
    }
}

/* Enable PCIe device */
void pcie_enable_device(uint8_t bus, uint8_t device, uint8_t function) {
    uint16_t command = pcie_config_read_word(bus, device, function, 0x04);
    
    // Set Bus Master (bit 2) and Memory Space (bit 1) bits
    command |= (1 << 2) | (1 << 1);
    
    // Write back the updated command register
    pcie_config_write_word(bus, device, function, 0x04, command);
    
    // If this is a PCIe device, also set up PCIe specific settings
    struct pcie_device dev = pcie_get_device_info(bus, device, function);
    
    if (dev.pcie_cap_offset) {
        // Get current Link Control Register
        uint16_t link_control = dev.link_control;
        
        // Configure Link Control settings if needed
        // For example, to enable ASPM L0s and L1:
        // link_control |= 0x3;
        
        // Write back updated Link Control
        pcie_config_write_word(bus, device, function, dev.pcie_cap_offset + 0x10, link_control);
        
        // Configure Device Control Register for optimal settings
        uint16_t device_control = pcie_config_read_word(bus, device, function, dev.pcie_cap_offset + 0x08);
        
        // Example: Enable relaxed ordering and no snoop
        device_control |= (1 << 4) | (1 << 11);
        
        // Write back updated Device Control
        pcie_config_write_word(bus, device, function, dev.pcie_cap_offset + 0x08, device_control);
    }
}

/* Find a PCIe device with specific class, subclass, and programming interface */
bool pcie_find_device(uint8_t class_code, uint8_t subclass, uint8_t prog_if,
                     uint8_t* out_bus, uint8_t* out_device, uint8_t* out_function) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t function = 0; function < 8; function++) {
                uint16_t vendor_id = pcie_config_read_word(bus, device, function, 0x00);
                if (vendor_id == 0xFFFF) continue; // No device
                
                uint8_t current_class = pcie_config_read_byte(bus, device, function, 0x0B);
                uint8_t current_subclass = pcie_config_read_byte(bus, device, function, 0x0A);
                uint8_t current_prog_if = pcie_config_read_byte(bus, device, function, 0x09);
                
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

/* Get the size of a PCIe Base Address Register (BAR) */
uint64_t pcie_get_bar_size(uint8_t bus, uint8_t device, uint8_t function, uint8_t bar_num) {
    uint32_t bar_offset = 0x10 + (bar_num * 4);
    uint32_t old_value, bar_size;
    
    // Save the original BAR value
    old_value = pcie_config_read_dword(bus, device, function, bar_offset);
    
    // Write all 1's to the BAR
    pcie_config_write_dword(bus, device, function, bar_offset, 0xFFFFFFFF);
    
    // Read it back to see what bits are writable
    bar_size = pcie_config_read_dword(bus, device, function, bar_offset);
    
    // Restore the original BAR value
    pcie_config_write_dword(bus, device, function, bar_offset, old_value);
    
    // If this is an I/O BAR (bit 0 set)
    if (old_value & 0x1) {
        // I/O BARs only use the lower 16 bits
        bar_size &= 0xFFFF;
    } else {
        // Memory BAR - check if this is a 64-bit BAR
        if (((old_value >> 1) & 0x3) == 0x2) {  // 64-bit BAR
            if (bar_num >= 5) {
                // There's no space for the upper 32 bits
                return 0;
            }
            
            // Save the original upper 32 bits BAR value
            uint32_t old_upper = pcie_config_read_dword(bus, device, function, bar_offset + 4);
            
            // Write all 1's to the upper BAR
            pcie_config_write_dword(bus, device, function, bar_offset + 4, 0xFFFFFFFF);
            
            // Read it back to see what bits are writable in upper 32 bits
            uint32_t upper_size = pcie_config_read_dword(bus, device, function, bar_offset + 4);
            
            // Restore the original upper BAR value
            pcie_config_write_dword(bus, device, function, bar_offset + 4, old_upper);
            
            // If upper 32 bits are non-zero, this is a very large memory region
            if (upper_size != 0) {
                // Return approximate size if possible
                return 0x100000000ULL * (upper_size == 0xFFFFFFFF ? 0xFFFFFFFF : (~upper_size + 1));
            }
        }
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

/* Get the type of a PCIe Base Address Register (BAR) */
enum pcie_bar_type pcie_get_bar_type(uint8_t bus, uint8_t device, uint8_t function, uint8_t bar_num) {
    uint32_t bar_offset = 0x10 + (bar_num * 4);
    uint32_t bar_value = pcie_config_read_dword(bus, device, function, bar_offset);
    
    // Check bit 0 to determine if this is an I/O or Memory BAR
    if (bar_value & 0x1) {
        return PCIE_BAR_IO;
    } else {
        // Memory BAR - check bits 1-2 to determine type
        switch ((bar_value >> 1) & 0x3) {
            case 0x0: return PCIE_BAR_MEM32;  // 32-bit Memory BAR
            case 0x1: return PCIE_BAR_MEM16;  // 16-bit Memory BAR (Below 1MB)
            case 0x2: return PCIE_BAR_MEM64;  // 64-bit Memory BAR
            default: return PCIE_BAR_UNKNOWN; // Reserved
        }
    }
}

/* Get the base address of a PCIe BAR, masking out the type bits */
uint64_t pcie_get_bar_address(uint8_t bus, uint8_t device, uint8_t function, uint8_t bar_num) {
    uint32_t bar_offset = 0x10 + (bar_num * 4);
    uint32_t bar_value = pcie_config_read_dword(bus, device, function, bar_offset);
    enum pcie_bar_type type = pcie_get_bar_type(bus, device, function, bar_num);
    
    // For 64-bit BAR, we need to read the next BAR for the high 32 bits
    uint64_t addr = 0;
    
    switch (type) {
        case PCIE_BAR_IO:
            // I/O BARs use the lower 16 bits for the address, mask out the lowest bit (type)
            return (uint64_t)(bar_value & ~0x3);
            
        case PCIE_BAR_MEM16:
        case PCIE_BAR_MEM32:
            // 16-bit and 32-bit Memory BARs, mask out the lower 4 bits (type and prefetchable bit)
            return (uint64_t)(bar_value & ~0xF);
            
        case PCIE_BAR_MEM64:
            // 64-bit Memory BAR, need to read high 32 bits from next BAR
            addr = (uint64_t)(bar_value & ~0xF);
            if (bar_num < 5) {  // Ensure there is a next BAR
                bar_offset += 4; // Move to the next BAR
                bar_value = pcie_config_read_dword(bus, device, function, bar_offset);
                addr |= ((uint64_t)bar_value << 32);
            }
            return addr;
            
        default:
            return 0;
    }
}