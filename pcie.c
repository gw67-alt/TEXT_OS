#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "pcie.h"


#include <unistd.h>
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
                    //TODO
                    //Write to nvme here
                    //nvme_read_write_test(bus, device, function); 


                    printf("NVMe initialization attempted\n");
                    
                    
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
        
        // 1. Set up memory for data buffers
        void *read_buffer = malloc(4096);
        void *write_buffer = malloc(4096);
        if (!read_buffer || !write_buffer) {
            printf("Failed to allocate memory for data buffers\n");
            if (read_buffer) free(read_buffer);
            if (write_buffer) free(write_buffer);
            return;
        }
        
        // Initialize write buffer with test pattern
        for (int i = 0; i < 4096; i++) {
            ((uint8_t *)write_buffer)[i] = i & 0xFF;
        }
        
        // 2. Create PRPs (Physical Region Pages) for the data transfer
        uint64_t read_prp_addr = (uint64_t)read_buffer;
        uint64_t write_prp_addr = (uint64_t)write_buffer;
        uint64_t read_prp_list[2] = {read_prp_addr, 0};
        uint64_t write_prp_list[2] = {write_prp_addr, 0};
        
        // 3. Define necessary structs and constants
        #define NVME_CMD_READ 0x02
        #define NVME_CMD_WRITE 0x01
        #define NVME_CQE_STATUS_P 0x1
        #define PCI_CONFIG_ADDRESS 0xCF8
        #define PCI_CONFIG_DATA 0xCFC
        
        // Inline implementation of pci_config_read32
        uint32_t pci_addr = (1U << 31) | (bus << 16) | (device << 11) | (function << 8) | (0x10 & 0xFC);
        outl(PCI_CONFIG_ADDRESS, pci_addr);
        uint32_t bar0_addr_low = inl(PCI_CONFIG_DATA);
        
        pci_addr = (1U << 31) | (bus << 16) | (device << 11) | (function << 8) | (0x14 & 0xFC);
        outl(PCI_CONFIG_ADDRESS, pci_addr);
        uint32_t bar0_addr_high = inl(PCI_CONFIG_DATA);
        
        uint64_t bar0_addr = ((uint64_t)bar0_addr_high << 32) | (bar0_addr_low & ~0xF);
        
        // Calculate doorbell register offsets
        uint32_t cap_offset = 0;  // Capability register offset in BAR0
        uint32_t cap_low = *(volatile uint32_t *)(bar0_addr + cap_offset);
        uint32_t cap_high = *(volatile uint32_t *)(bar0_addr + cap_offset + 4);
        uint64_t cap = ((uint64_t)cap_high << 32) | cap_low;
        
        uint32_t dstrd = (cap >> 32) & 0xF;  // Doorbell stride
        uint32_t db_offset = 0x1000;  // Base doorbell offset
        
        // Calculate admin queue doorbell addresses
        volatile uint32_t *doorbell_reg = (volatile uint32_t *)(bar0_addr + db_offset + (0 * (4 << dstrd)));
        volatile uint32_t *cq_doorbell_reg = (volatile uint32_t *)(bar0_addr + db_offset + (1 * (4 << dstrd)));
        
        struct nvme_rw_command {
            uint8_t opcode;
            uint8_t flags;
            uint16_t command_id;
            uint32_t nsid;
            uint64_t reserved1;
            uint64_t slba;
            uint16_t length;
            uint16_t control;
            uint32_t dsmgmt;
            uint64_t prp1;
            uint64_t prp2;
        };
        
        struct nvme_command {
            struct nvme_rw_command rw;
        };
        
        struct nvme_completion {
            uint32_t result;
            uint32_t reserved;
            uint16_t sq_head;
            uint16_t sq_id;
            uint16_t command_id;
            uint16_t status;
        };
        
        // Define queue parameters based on controller capabilities
        uint16_t sq_size = 64;
        uint16_t cq_size = 64;
        uint16_t sq_tail = 0;
        uint16_t cq_head = 0;
        
        struct nvme_command *sq_entry = malloc(sq_size * sizeof(struct nvme_command));
        struct nvme_completion *cq_entry = malloc(cq_size * sizeof(struct nvme_completion));
        
        if (!sq_entry || !cq_entry) {
            printf("Failed to allocate memory for queues\n");
            if (read_buffer) free(read_buffer);
            if (write_buffer) free(write_buffer);
            if (sq_entry) free(sq_entry);
            if (cq_entry) free(cq_entry);
            return;
        }
        
        // Inline implementation of writel, inl, and outl functions
        void writel(uint32_t value, volatile uint32_t *addr) {
            *addr = value;
        }
        
       
        
        // First perform the write operation
        struct nvme_command write_cmd;
        memset(&write_cmd, 0, sizeof(write_cmd));
        write_cmd.rw.opcode = NVME_CMD_WRITE;
        write_cmd.rw.nsid = 1;  // Use namespace 1
        write_cmd.rw.prp1 = write_prp_list[0];
        write_cmd.rw.prp2 = write_prp_list[1];
        write_cmd.rw.slba = 0;  // Start at LBA 0
        write_cmd.rw.length = 0;  // 0-based, so 0 means 1 block (4KB)
        write_cmd.rw.control = 0;
        write_cmd.rw.command_id = 1;
        
        // Submit the write command
        sq_tail = (sq_tail + 1) % sq_size;
        sq_entry[sq_tail] = write_cmd;
        writel(sq_tail, doorbell_reg);
        
        // Wait for write completion
        struct nvme_completion write_cqe;
        memset(&write_cqe, 0, sizeof(write_cqe));
        while (!(write_cqe.status & NVME_CQE_STATUS_P)) {
            write_cqe = cq_entry[cq_head];
            if (write_cqe.status & NVME_CQE_STATUS_P) {
                cq_head = (cq_head + 1) % cq_size;
                writel(cq_head, cq_doorbell_reg);
                break;
            }
            // Add a small delay to avoid busy-waiting
            usleep(1000);
        }
        
        if ((write_cqe.status >> 1) != 0) {
            printf("Write command failed with status: 0x%x\n", write_cqe.status >> 1);
        } else {
            printf("4KB write test: Successful\n");
            
            // Now perform the read operation
            struct nvme_command read_cmd;
            memset(&read_cmd, 0, sizeof(read_cmd));
            read_cmd.rw.opcode = NVME_CMD_READ;
            read_cmd.rw.nsid = 1;
            read_cmd.rw.prp1 = read_prp_list[0];
            read_cmd.rw.prp2 = read_prp_list[1];
            read_cmd.rw.slba = 0;  // Read from the same LBA we wrote to
            read_cmd.rw.length = 0;
            read_cmd.rw.control = 0;
            read_cmd.rw.command_id = 2;
            
            // Submit the read command
            sq_tail = (sq_tail + 1) % sq_size;
            sq_entry[sq_tail] = read_cmd;
            writel(sq_tail, doorbell_reg);
            
            // Wait for read completion
            struct nvme_completion read_cqe;
            memset(&read_cqe, 0, sizeof(read_cqe));
            while (!(read_cqe.status & NVME_CQE_STATUS_P)) {
                read_cqe = cq_entry[cq_head];
                if (read_cqe.status & NVME_CQE_STATUS_P) {
                    cq_head = (cq_head + 1) % cq_size;
                    writel(cq_head, cq_doorbell_reg);
                    break;
                }
                usleep(1000);
            }
            
            if ((read_cqe.status >> 1) != 0) {
                printf("Read command failed with status: 0x%x\n", read_cqe.status >> 1);
            } else {
                // Verify the data read matches what was written
                int data_verified = 1;
                for (int i = 0; i < 4096; i++) {
                    if (((uint8_t *)read_buffer)[i] != ((uint8_t *)write_buffer)[i]) {
                        data_verified = 0;
                        printf("Data verification failed at offset %d\n", i);
                        break;
                    }
                }
                
                if (data_verified) {
                    printf("4KB read test: Successful\n");
                }
            }
        }
    
        // Free allocated memory
        free(read_buffer);
        free(write_buffer);
        //free(sq_entry);
        //free(cq_entry);
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





/* NVMe command status codes */
#define NVME_SC_SUCCESS 0x0

/* Define ETIMEDOUT if not already defined */
#ifndef ETIMEDOUT
#define ETIMEDOUT 110  /* Standard timeout error code */
#endif

/**
 * Poll NVMe completion queue for command completion without using structs
 *
 * @param cq_buffer         Pointer to completion queue memory
 * @param cq_size           Size of the completion queue (entries)
 * @param cq_head_ptr       Pointer to completion queue head index (will be updated)
 * @param cq_phase_ptr      Pointer to completion queue phase tag (will be updated)
 * @param cq_doorbell       Pointer to completion queue doorbell register
 * @param cmd_id            Command ID to wait for
 * @param timeout_ms        Timeout in milliseconds
 * @param sq_head_ptr       Pointer to submission queue head index (will be updated)
 *
 * @return 0 on success, negative error code on failure
 */
int nvme_poll_completion_nostruct(
    void *cq_buffer,
    uint32_t cq_size,
    uint32_t *cq_head_ptr,
    uint32_t *cq_phase_ptr,
    uint32_t *cq_doorbell,
    uint16_t cmd_id,
    uint32_t timeout_ms,
    uint32_t *sq_head_ptr)
{
    uint32_t cq_head = *cq_head_ptr;
    uint32_t cq_phase = *cq_phase_ptr;
    uint32_t elapsed = 0;
    const uint32_t poll_interval_us = 100;
    
    /* Completion queue entry fields offsets */
    const int CQE_RESULT_OFFSET = 0;       /* 4 bytes */
    const int CQE_RESERVED_OFFSET = 4;     /* 4 bytes */
    const int CQE_SQ_HEAD_OFFSET = 8;      /* 2 bytes */
    const int CQE_SQ_ID_OFFSET = 10;       /* 2 bytes */
    const int CQE_COMMAND_ID_OFFSET = 12;  /* 2 bytes */
    const int CQE_STATUS_OFFSET = 14;      /* 2 bytes */
    const int CQE_SIZE = 16;               /* Total size in bytes */
    
    printf("Polling completion queue: Head=%u, Phase=%u, CMD_ID=%u\n", 
           cq_head, cq_phase, cmd_id);
    
    /* Poll completion queue until command completes or timeout */
    while (elapsed < timeout_ms * 1000) {
        /* Check all completion entries between head and tail */
        uint16_t current_index = cq_head;
        
        while (1) {
            /* Calculate pointer to the current completion queue entry */
            uint8_t *cq_entry = (uint8_t *)cq_buffer + (current_index * CQE_SIZE);
            
            /* Extract status field (2 bytes) */
            uint16_t status = *(uint16_t *)(cq_entry + CQE_STATUS_OFFSET);
            
            /* Check phase bit to see if entry is valid */
            if ((status & 0x1) == cq_phase) {
                /* Extract command ID (2 bytes) */
                uint16_t entry_cmd_id = *(uint16_t *)(cq_entry + CQE_COMMAND_ID_OFFSET);
                
                /* Entry is valid, check if it's for our command */
                if (entry_cmd_id == cmd_id) {
                    /* Extract status code */
                    uint16_t status_code = (status >> 1) & 0xFF;
                    uint16_t status_type = (status >> 9) & 0x7;
                    
                    /* Extract SQ head pointer (2 bytes) */
                    uint16_t sq_head = *(uint16_t *)(cq_entry + CQE_SQ_HEAD_OFFSET);
                    *sq_head_ptr = sq_head;
                    
                    /* Update completion queue head */
                    *cq_head_ptr = (current_index + 1) % cq_size;
                    
                    /* Check if we wrapped around */
                    if (*cq_head_ptr == 0) {
                        *cq_phase_ptr = !cq_phase;
                    }
                    
                    /* Ring the completion queue doorbell */
                    *cq_doorbell = *cq_head_ptr;
                    
                    if (status_code == NVME_SC_SUCCESS) {
                        printf("Command %u completed successfully\n", cmd_id);
                        return 0; /* Success */
                    } else {
                        printf("Error: Command %u failed with status type %u, code 0x%X\n", 
                               cmd_id, status_type, status_code);
                        return -status_code;
                    }
                }
                
                /* Move to next entry */
                current_index = (current_index + 1) % cq_size;
                
                /* If we've wrapped around to head, we've checked all valid entries */
                if (current_index == cq_head) {
                    break;
                }
            } else {
                /* No more valid entries */
                break;
            }
        }
        
        /* Sleep for a short time before polling again */
        usleep(poll_interval_us);
        elapsed += poll_interval_us;
    }
    
    printf("Error: Timeout waiting for completion of command %u\n", cmd_id);
    return -ETIMEDOUT;
}


/* NVMe Opcodes */
#define NVME_CMD_WRITE 0x01

/* NVMe Submission Queue Entry offsets within a 64-byte entry */
#define SQE_OPCODE_OFFSET     0   /* 1 byte */
#define SQE_FLAGS_OFFSET      1   /* 1 byte */
#define SQE_COMMAND_ID_OFFSET 2   /* 2 bytes */
#define SQE_NSID_OFFSET       4   /* 4 bytes */
#define SQE_RESERVED1_OFFSET  8   /* 8 bytes */
#define SQE_METADATA_OFFSET  16   /* 8 bytes */
#define SQE_PRP1_OFFSET      24   /* 8 bytes */
#define SQE_PRP2_OFFSET      32   /* 8 bytes */
#define SQE_SLBA_OFFSET      40   /* 8 bytes */
#define SQE_NLB_OFFSET       48   /* 4 bytes */
#define SQE_CONTROL_OFFSET   52   /* 2 bytes */
#define SQE_DSMGMT_OFFSET    54   /* 2 bytes */
#define SQE_REFTAG_OFFSET    56   /* 4 bytes */
#define SQE_APPTAG_OFFSET    60   /* 2 bytes */
#define SQE_APPMASK_OFFSET   62   /* 2 bytes */
#define SQE_SIZE             64   /* Total size in bytes */

/**
 * Submit a write command to NVMe device without using structs
 *
 * @param sq_buffer     Pointer to submission queue memory
 * @param sq_size       Size of the submission queue (entries)
 * @param sq_tail_ptr   Pointer to submission queue tail index (will be updated)
 * @param sq_head       Current head position of submission queue
 * @param sq_doorbell   Pointer to submission queue doorbell register
 * @param nsid          Namespace ID
 * @param lba           Starting Logical Block Address
 * @param num_blocks    Number of blocks to write
 * @param data          Pointer to data buffer
 * @param data_len      Length of data buffer in bytes
 * @param next_cmd_id   Next command ID to use
 * @param cmd_id_ptr    Pointer to store assigned command ID
 *
 * @return 0 on success, negative error code on failure
 */
int nvme_submit_write_nostruct(
    void *sq_buffer,
    uint32_t sq_size,
    uint32_t *sq_tail_ptr,
    uint32_t sq_head,
    uint32_t *sq_doorbell,
    uint32_t nsid,
    uint64_t lba,
    uint32_t num_blocks,
    void *data,
    uint32_t data_len,
    uint16_t next_cmd_id,
    uint16_t *cmd_id_ptr)
{
    uint32_t sq_tail = *sq_tail_ptr;
    
    /* Check if there's space in the submission queue */
    uint32_t next_tail = (sq_tail + 1) % sq_size;
    if (next_tail == sq_head) {
        printf("Error: Submission queue is full\n");
        return -1;
    }
    
    /* Calculate pointer to the next available submission queue entry */
    uint8_t *sq_entry = (uint8_t *)sq_buffer + (sq_tail * SQE_SIZE);
    
    /* Clear the entire entry */
    memset(sq_entry, 0, SQE_SIZE);
    
    /* Assign a command ID and store it for the caller */
    uint16_t cmd_id = next_cmd_id;
    *cmd_id_ptr = cmd_id;
    
    /* Log the write operation details */
    printf("Submitting write command: LBA=0x%lx, Blocks=%u, Data=%p, CMD_ID=%u\n",
           lba, num_blocks, data, cmd_id);
    
    /* Fill in the command fields */
    sq_entry[SQE_OPCODE_OFFSET] = NVME_CMD_WRITE;          /* Opcode: Write */
    sq_entry[SQE_FLAGS_OFFSET] = 0;                        /* Flags */
    *(uint16_t *)(sq_entry + SQE_COMMAND_ID_OFFSET) = cmd_id; /* Command ID */
    *(uint32_t *)(sq_entry + SQE_NSID_OFFSET) = nsid;      /* Namespace ID */
    
    /* Set up Physical Region Page (PRP) entries for data buffer */
    *(uint64_t *)(sq_entry + SQE_PRP1_OFFSET) = (uint64_t)data;   /* PRP1: First 4K */
    
    /* If data spans multiple pages, set up PRP2 */
    uint32_t page_size = 4096;
    if (data_len > page_size) {
        /* If data is between 4K and 8K, PRP2 points to second 4K chunk */
        if (data_len <= 2 * page_size) {
            *(uint64_t *)(sq_entry + SQE_PRP2_OFFSET) = (uint64_t)data + page_size;
        } else {
            /* For transfers > 8K, we need a PRP list, but this is simplified */
            printf("Warning: Large transfers (>8KB) not fully implemented\n");
            *(uint64_t *)(sq_entry + SQE_PRP2_OFFSET) = (uint64_t)data + page_size;
        }
    }
    
    /* Set LBA (Logical Block Address) */
    *(uint64_t *)(sq_entry + SQE_SLBA_OFFSET) = lba;
    
    /* Set Number of Logical Blocks (0-based value) */
    *(uint32_t *)(sq_entry + SQE_NLB_OFFSET) = num_blocks - 1;
    
    /* Update submission queue tail pointer */
    *sq_tail_ptr = next_tail;
    
    /* Ring the submission queue doorbell to submit the command */
    printf("Ringing submission queue doorbell: Tail=%u\n", *sq_tail_ptr);
    *sq_doorbell = *sq_tail_ptr;
    
    return 0;
}

/**
 * Write data to NVMe device at specified LBA without using structs
 * This function combines submission and completion polling
 *
 * @param sq_buffer     Pointer to submission queue memory
 * @param sq_size       Size of the submission queue (entries)
 * @param sq_tail_ptr   Pointer to submission queue tail index
 * @param sq_head_ptr   Pointer to submission queue head index
 * @param sq_doorbell   Pointer to submission queue doorbell register
 * @param cq_buffer     Pointer to completion queue memory
 * @param cq_size       Size of the completion queue (entries)
 * @param cq_head_ptr   Pointer to completion queue head index
 * @param cq_phase_ptr  Pointer to completion queue phase tag
 * @param cq_doorbell   Pointer to completion queue doorbell register
 * @param nsid          Namespace ID
 * @param lba           Starting Logical Block Address
 * @param num_blocks    Number of blocks to write
 * @param data          Pointer to data buffer
 * @param next_cmd_id   Next command ID to use
 *
 * @return 0 on success, negative error code on failure
 */
int nvme_write_blocks_nostruct(
    void *sq_buffer,
    uint32_t sq_size,
    uint32_t *sq_tail_ptr,
    uint32_t *sq_head_ptr,
    uint32_t *sq_doorbell,
    void *cq_buffer,
    uint32_t cq_size,
    uint32_t *cq_head_ptr,
    uint32_t *cq_phase_ptr,
    uint32_t *cq_doorbell,
    uint32_t nsid,
    uint64_t lba,
    uint32_t num_blocks,
    void *data,
    uint16_t next_cmd_id)
{
    int ret;
    uint16_t cmd_id;
    uint32_t data_len = num_blocks * 512; /* Assuming 512-byte sectors */
    
    /* Submit write command */
    ret = nvme_submit_write_nostruct(
        sq_buffer,
        sq_size,
        sq_tail_ptr,
        *sq_head_ptr,
        sq_doorbell,
        nsid,
        lba,
        num_blocks,
        data,
        data_len,
        next_cmd_id,
        &cmd_id
    );
    
    if (ret) {
        printf("Error: Failed to submit write command\n");
        return ret;
    }
    
    
    
    /* Wait for completion */
    ret = nvme_poll_completion_nostruct(
        cq_buffer,
        cq_size,
        cq_head_ptr,
        cq_phase_ptr,
        cq_doorbell,
        cmd_id,
        5000, /* 5 second timeout */
        sq_head_ptr
    );
    
    if (ret) {
        printf("Error: Write command failed\n");
        return ret;
    }
    
    return 0;
}

/**
 * Example usage function demonstrating how to use the no-struct write functions
 */
int example_usage(void) {
    /* Allocate queues (simplified example) */
    void *sq_buffer = aligned_alloc(4096, 1024 * 64);  /* 1024 entries, 64 bytes each */
    void *cq_buffer = aligned_alloc(4096, 1024 * 16);  /* 1024 entries, 16 bytes each */
    
    if (!sq_buffer || !cq_buffer) {
        if (sq_buffer) free(sq_buffer);
        if (cq_buffer) free(cq_buffer);
        return -1;
    }
    
    /* Clear queue memory */
    memset(sq_buffer, 0, 1024 * 64);
    memset(cq_buffer, 0, 1024 * 16);
    
    /* Initialize tracking variables */
    uint32_t sq_tail = 0;
    uint32_t sq_head = 0;
    uint32_t cq_head = 0;
    uint32_t cq_phase = 1;
    
    /* These would be memory-mapped registers in real hardware */
    uint32_t sq_doorbell_value = 0;
    uint32_t cq_doorbell_value = 0;
    uint32_t *sq_doorbell = &sq_doorbell_value;
    uint32_t *cq_doorbell = &cq_doorbell_value;
    
    /* Allocate a data buffer for writing */
    void *data_buffer = aligned_alloc(4096, 4096);
    if (!data_buffer) {
        free(sq_buffer);
        free(cq_buffer);
        return -1;
    }
    
    /* Fill buffer with test pattern */
    for (int i = 0; i < 4096; i++) {
        ((uint8_t *)data_buffer)[i] = i & 0xFF;
    }
    
    /* Write data (this is just an example, not a real call in context) */
    int result = nvme_write_blocks_nostruct(
        sq_buffer,      /* SQ buffer */
        1024,           /* SQ size */
        &sq_tail,       /* SQ tail */
        &sq_head,       /* SQ head */
        sq_doorbell,    /* SQ doorbell */
        cq_buffer,      /* CQ buffer */
        1024,           /* CQ size */
        &cq_head,       /* CQ head */
        &cq_phase,      /* CQ phase */
        cq_doorbell,    /* CQ doorbell */
        1,              /* Namespace ID */
        0,              /* LBA */
        8,              /* Number of blocks (4KB) */
        data_buffer,    /* Data buffer */
        0               /* First command ID */
    );
    
    /* Clean up */
    free(data_buffer);
    free(sq_buffer);
    free(cq_buffer);
    
    return result;
}



/* NVMe Admin command opcodes */
#define NVME_ADMIN_CMD_CREATE_CQ    0x05
#define NVME_ADMIN_CMD_CREATE_SQ    0x01
#define NVME_ADMIN_CMD_IDENTIFY     0x06

/* NVMe Doorbell Register Offsets */
#define NVME_REG_CAP_LO       0x00  /* Controller Capabilities Lower Dword */
#define NVME_REG_CAP_HI       0x04  /* Controller Capabilities Higher Dword */
#define NVME_REG_VS           0x08  /* Version */
#define NVME_REG_CC           0x14  /* Controller Configuration */
#define NVME_REG_CSTS         0x1C  /* Controller Status */
#define NVME_REG_AQA          0x24  /* Admin Queue Attributes */
#define NVME_REG_ASQ          0x28  /* Admin Submission Queue Base Address */
#define NVME_REG_ACQ          0x30  /* Admin Completion Queue Base Address */
#define NVME_REG_SQ0TDBL      0x1000 /* Submission Queue 0 Tail Doorbell */
#define NVME_REG_CQ0HDBL      0x1004 /* Completion Queue 0 Head Doorbell */

/**
 * Initialize NVMe admin queue without using structs
 *
 * @param reg_base        Base address of NVMe controller registers
 * @param admin_sq_buffer Pointer to admin submission queue memory
 * @param admin_sq_size   Size of admin submission queue (entries)
 * @param admin_cq_buffer Pointer to admin completion queue memory
 * @param admin_cq_size   Size of admin completion queue (entries)
 * @param db_stride       Doorbell stride from NVMe capabilities
 * @param sq_tail_ptr     Pointer to store submission queue tail index
 * @param sq_head_ptr     Pointer to store submission queue head index
 * @param cq_head_ptr     Pointer to store completion queue head index
 * @param cq_phase_ptr    Pointer to store completion queue phase bit
 * @param sq_doorbell_ptr Pointer to store SQ doorbell register address
 * @param cq_doorbell_ptr Pointer to store CQ doorbell register address
 *
 * @return 0 on success, negative error code on failure
 */
int nvme_init_admin_queue_nostruct(
    volatile uint32_t *reg_base,
    void *admin_sq_buffer,
    uint32_t admin_sq_size,
    void *admin_cq_buffer,
    uint32_t admin_cq_size,
    uint32_t db_stride,
    uint32_t *sq_tail_ptr,
    uint32_t *sq_head_ptr,
    uint32_t *cq_head_ptr,
    uint32_t *cq_phase_ptr,
    volatile uint32_t **sq_doorbell_ptr,
    volatile uint32_t **cq_doorbell_ptr)
{
    /* Initialize indices and phase bit */
    *sq_tail_ptr = 0;
    *sq_head_ptr = 0;
    *cq_head_ptr = 0;
    *cq_phase_ptr = 1;
    
    /* Clear queue memory */
    memset(admin_sq_buffer, 0, admin_sq_size * 64);  /* 64 bytes per entry */
    memset(admin_cq_buffer, 0, admin_cq_size * 16);  /* 16 bytes per entry */
    
    /* Set up Admin Queue Attributes register */
    uint32_t aqa = ((admin_cq_size - 1) << 16) | (admin_sq_size - 1);
    reg_base[NVME_REG_AQA/4] = aqa;
    
    /* Set Admin Submission Queue Base Address */
    uint64_t admin_sq_addr = (uint64_t)admin_sq_buffer;
    reg_base[NVME_REG_ASQ/4] = (uint32_t)admin_sq_addr;
    reg_base[(NVME_REG_ASQ/4) + 1] = (uint32_t)(admin_sq_addr >> 32);
    
    /* Set Admin Completion Queue Base Address */
    uint64_t admin_cq_addr = (uint64_t)admin_cq_buffer;
    reg_base[NVME_REG_ACQ/4] = (uint32_t)admin_cq_addr;
    reg_base[(NVME_REG_ACQ/4) + 1] = (uint32_t)(admin_cq_addr >> 32);
    
    /* Calculate doorbell register addresses */
    *sq_doorbell_ptr = &reg_base[NVME_REG_SQ0TDBL/4];
    *cq_doorbell_ptr = &reg_base[NVME_REG_CQ0HDBL/4];
    
    return 0;
}

/**
 * Reset and enable NVMe controller without using structs
 *
 * @param reg_base      Base address of NVMe controller registers
 * @param timeout_ms    Timeout in milliseconds
 *
 * @return 0 on success, negative error code on failure
 */
int nvme_reset_and_enable_controller_nostruct(
    volatile uint32_t *reg_base,
    uint32_t timeout_ms)
{
    uint32_t elapsed = 0;
    const uint32_t poll_interval_us = 100;
    
    /* Read controller capabilities */
    uint32_t cap_lo = reg_base[NVME_REG_CAP_LO/4];
    uint32_t cap_hi = reg_base[NVME_REG_CAP_HI/4];
    uint64_t cap = ((uint64_t)cap_hi << 32) | cap_lo;
    
    /* Extract important capabilities */
    uint32_t timeout = (cap >> 24) & 0xFF;
    uint32_t db_stride = (cap >> 32) & 0xF;
    uint32_t mqes = cap & 0xFFFF;
    
    printf("NVMe Controller Capabilities: 0x%016lx\n", cap);
    printf("  Max Queue Entries: %u\n", mqes + 1);
    printf("  Doorbell Stride: %u\n", db_stride);
    printf("  Timeout: %u sec\n", timeout);
    
    /* Read version register */
    uint32_t version = reg_base[NVME_REG_VS/4];
    printf("NVMe Version: %d.%d.%d\n", 
           (version >> 16) & 0xFFFF, (version >> 8) & 0xFF, version & 0xFF);
    
    /* Reset the controller - disable it first */
    uint32_t cc = reg_base[NVME_REG_CC/4];
    cc &= ~0x1;  /* Clear Enable bit */
    reg_base[NVME_REG_CC/4] = cc;
    
    /* Wait for CSTS.RDY to become 0 */
    elapsed = 0;
    while (elapsed < timeout_ms * 1000) {
        uint32_t csts = reg_base[NVME_REG_CSTS/4];
        if ((csts & 0x1) == 0) {
            break;
        }
        usleep(poll_interval_us);
        elapsed += poll_interval_us;
    }
    
    /* Check for timeout */
    if (elapsed >= timeout_ms * 1000) {
        printf("Error: Timeout waiting for controller to reset\n");
        return -1;
    }
    
    printf("NVMe controller reset complete.\n");
    
    /* Configure the controller */
    cc = 0;
    cc |= (0 << 4);    /* I/O Command Set Selected: NVM Command Set */
    cc |= (4 << 7);    /* I/O Completion Queue Entry Size: 16 bytes (2^4) */
    cc |= (6 << 11);   /* I/O Submission Queue Entry Size: 64 bytes (2^6) */
    cc |= (0 << 14);   /* Memory Page Size: 4KB (2^12 bytes) -> (0 = 2^12) */
    cc |= (0 << 16);   /* Arbitration Mechanism: Round Robin */
    cc |= (1 << 20);   /* Enable Submission Queue in memory */
    cc |= (1 << 1);    /* Enable interrupts */
    
    /* Write the configuration */
    reg_base[NVME_REG_CC/4] = cc;
    
    /* Enable the controller */
    cc |= 0x1;  /* Set Enable bit */
    reg_base[NVME_REG_CC/4] = cc;
    
    /* Wait for CSTS.RDY to become 1 */
    elapsed = 0;
    while (elapsed < timeout_ms * 1000) {
        uint32_t csts = reg_base[NVME_REG_CSTS/4];
        if ((csts & 0x1) != 0) {
            break;
        }
        usleep(poll_interval_us);
        elapsed += poll_interval_us;
    }
    
    /* Check for timeout */
    if (elapsed >= timeout_ms * 1000) {
        printf("Error: Timeout waiting for controller to enable\n");
        return -1;
    }
    
    printf("NVMe controller enabled successfully.\n");
    return 0;
}




/* NVMe Admin Command Opcodes */
#define NVME_ADMIN_CMD_IDENTIFY   0x06
#define NVME_ADMIN_CMD_CREATE_CQ  0x05
#define NVME_ADMIN_CMD_CREATE_SQ  0x01

/* NVMe Submission Queue Entry offsets within a 64-byte entry */
#define SQE_OPCODE_OFFSET     0   /* 1 byte */
#define SQE_FLAGS_OFFSET      1   /* 1 byte */
#define SQE_COMMAND_ID_OFFSET 2   /* 2 bytes */
#define SQE_NSID_OFFSET       4   /* 4 bytes */
#define SQE_RESERVED1_OFFSET  8   /* 8 bytes */
#define SQE_METADATA_OFFSET  16   /* 8 bytes */
#define SQE_PRP1_OFFSET      24   /* 8 bytes */
#define SQE_PRP2_OFFSET      32   /* 8 bytes */
#define SQE_CDW10_OFFSET     40   /* 4 bytes - Command Dword 10 */
#define SQE_CDW11_OFFSET     44   /* 4 bytes - Command Dword 11 */
#define SQE_CDW12_OFFSET     48   /* 4 bytes - Command Dword 12 */
#define SQE_CDW13_OFFSET     52   /* 4 bytes - Command Dword 13 */
#define SQE_CDW14_OFFSET     56   /* 4 bytes - Command Dword 14 */
#define SQE_CDW15_OFFSET     60   /* 4 bytes - Command Dword 15 */
#define SQE_SIZE             64   /* Total size in bytes */

/**
 * Submit Identify Controller Command without using structs
 *
 * @param sq_buffer      Pointer to submission queue memory
 * @param sq_tail_ptr    Pointer to submission queue tail index (will be updated)
 * @param sq_size        Size of the submission queue (entries)
 * @param sq_doorbell    Pointer to submission queue doorbell register
 * @param identify_data  Buffer to store identify data (4KB)
 * @param cmd_id         Command ID to use
 *
 * @return 0 on success, negative error code on failure
 */
int nvme_submit_identify_controller_nostruct(
    void *sq_buffer,
    uint32_t *sq_tail_ptr,
    uint32_t sq_size,
    volatile uint32_t *sq_doorbell,
    void *identify_data,
    uint16_t cmd_id)
{
    uint32_t sq_tail = *sq_tail_ptr;
    
    /* Calculate pointer to the next available submission queue entry */
    uint8_t *sq_entry = (uint8_t *)sq_buffer + (sq_tail * SQE_SIZE);
    
    /* Clear the entire entry */
    memset(sq_entry, 0, SQE_SIZE);
    
    /* Fill in the command */
    sq_entry[SQE_OPCODE_OFFSET] = NVME_ADMIN_CMD_IDENTIFY;    /* Opcode: Identify */
    sq_entry[SQE_FLAGS_OFFSET] = 0;                          /* Flags */
    *(uint16_t *)(sq_entry + SQE_COMMAND_ID_OFFSET) = cmd_id;/* Command ID */
    *(uint32_t *)(sq_entry + SQE_NSID_OFFSET) = 0;           /* No specific namespace */
    
    /* Set up PRP for data buffer */
    *(uint64_t *)(sq_entry + SQE_PRP1_OFFSET) = (uint64_t)identify_data;
    
    /* Command Dword 10: Identify Controller structure (CNS=1) */
    *(uint32_t *)(sq_entry + SQE_CDW10_OFFSET) = 1;
    
    /* Update submission queue tail pointer */
    *sq_tail_ptr = (sq_tail + 1) % sq_size;
    
    /* Ring the submission queue doorbell */
    *sq_doorbell = *sq_tail_ptr;
    
    printf("Submitted Identify Controller command (CMD_ID=%u)\n", cmd_id);
    return 0;
}

/**
 * Process Identify Controller data without using structs
 *
 * @param identify_data  Pointer to identify controller data (4KB buffer)
 * @param max_queues_ptr Pointer to store max queue entries supported
 *
 * @return 0 on success, negative error code on failure
 */
int nvme_process_identify_controller_nostruct(
    void *identify_data,
    uint16_t *max_queues_ptr)
{
    /* Define important identify data offsets */
    const int ID_VID_OFFSET = 0;      /* Vendor ID - 2 bytes */
    const int ID_SSVID_OFFSET = 2;    /* Subsystem Vendor ID - 2 bytes */
    const int ID_SN_OFFSET = 4;       /* Serial Number - 20 bytes */
    const int ID_MN_OFFSET = 24;      /* Model Number - 40 bytes */
    const int ID_FR_OFFSET = 64;      /* Firmware Revision - 8 bytes */
    const int ID_MAXCMD_OFFSET = 514; /* Maximum Combined Queue Size - 2 bytes */
    const int ID_NN_OFFSET = 516;     /* Number of Namespaces - 4 bytes */
    
    uint8_t *data = (uint8_t *)identify_data;
    
    /* Extract and print device identifiers */
    uint16_t vid = *(uint16_t *)(data + ID_VID_OFFSET);
    uint16_t ssvid = *(uint16_t *)(data + ID_SSVID_OFFSET);
    
    /* Process serial number (20 bytes, space-padded ASCII) */
    char serial_number[21] = {0};
    memcpy(serial_number, data + ID_SN_OFFSET, 20);
    /* Trim trailing spaces */
    for (int i = 19; i >= 0; i--) {
        if (serial_number[i] == ' ') {
            serial_number[i] = '\0';
        } else if (serial_number[i] != '\0') {
            break;
        }
    }
    
    /* Process model number (40 bytes, space-padded ASCII) */
    char model_number[41] = {0};
    memcpy(model_number, data + ID_MN_OFFSET, 40);
    /* Trim trailing spaces */
    for (int i = 39; i >= 0; i--) {
        if (model_number[i] == ' ') {
            model_number[i] = '\0';
        } else if (model_number[i] != '\0') {
            break;
        }
    }
    
    /* Process firmware revision (8 bytes, space-padded ASCII) */
    char firmware_rev[9] = {0};
    memcpy(firmware_rev, data + ID_FR_OFFSET, 8);
    /* Trim trailing spaces */
    for (int i = 7; i >= 0; i--) {
        if (firmware_rev[i] == ' ') {
            firmware_rev[i] = '\0';
        } else if (firmware_rev[i] != '\0') {
            break;
        }
    }
    
    /* Extract maximum queue entries */
    uint16_t max_queues = *(uint16_t *)(data + ID_MAXCMD_OFFSET);
    *max_queues_ptr = max_queues;
    
    /* Extract number of namespaces */
    uint32_t num_namespaces = *(uint32_t *)(data + ID_NN_OFFSET);
    
    /* Print controller information */
    printf("NVMe Controller Information:\n");
    printf("  Vendor ID: 0x%04X\n", vid);
    printf("  Subsystem Vendor ID: 0x%04X\n", ssvid);
    printf("  Serial Number: %s\n", serial_number);
    printf("  Model Number: %s\n", model_number);
    printf("  Firmware Revision: %s\n", firmware_rev);
    printf("  Max Queue Entries: %u\n", max_queues);
    printf("  Number of Namespaces: %u\n", num_namespaces);
    
    return 0;
}

/**
 * Create NVMe I/O Completion Queue without using structs
 *
 * @param admin_sq_buffer    Pointer to admin submission queue memory
 * @param admin_sq_tail_ptr  Pointer to admin submission queue tail index
 * @param admin_sq_size      Size of admin submission queue (entries)
 * @param admin_sq_doorbell  Pointer to admin submission queue doorbell register
 * @param cq_buffer          Pointer to I/O completion queue memory
 * @param cq_size            Size of I/O completion queue (entries)
 * @param cq_id              Completion queue identifier
 * @param cmd_id             Command ID to use
 *
 * @return 0 on success, negative error code on failure
 */
int nvme_create_io_completion_queue_nostruct(
    void *admin_sq_buffer,
    uint32_t *admin_sq_tail_ptr,
    uint32_t admin_sq_size,
    volatile uint32_t *admin_sq_doorbell,
    void *cq_buffer,
    uint32_t cq_size,
    uint16_t cq_id,
    uint16_t cmd_id)
{
    uint32_t sq_tail = *admin_sq_tail_ptr;
    
    /* Calculate pointer to the next available submission queue entry */
    uint8_t *sq_entry = (uint8_t *)admin_sq_buffer + (sq_tail * SQE_SIZE);
    
    /* Clear the entire entry */
    memset(sq_entry, 0, SQE_SIZE);
    
    /* Fill in the command */
    sq_entry[SQE_OPCODE_OFFSET] = NVME_ADMIN_CMD_CREATE_CQ;   /* Opcode: Create I/O Completion Queue */
    sq_entry[SQE_FLAGS_OFFSET] = 0;                          /* Flags */
    *(uint16_t *)(sq_entry + SQE_COMMAND_ID_OFFSET) = cmd_id;/* Command ID */
    
    /* Set up PRP for completion queue */
    *(uint64_t *)(sq_entry + SQE_PRP1_OFFSET) = (uint64_t)cq_buffer;
    
    /* Command Dword 10: Queue Size and Queue Identifier */
    uint32_t cdw10 = ((cq_size - 1) & 0xFFFF) | (cq_id << 16);
    *(uint32_t *)(sq_entry + SQE_CDW10_OFFSET) = cdw10;
    
    /* Command Dword 11: Physically Contiguous, Interrupts Enabled */
    uint32_t cdw11 = (1 << 0) | (1 << 1);
    *(uint32_t *)(sq_entry + SQE_CDW11_OFFSET) = cdw11;
    
    /* Update submission queue tail pointer */
    *admin_sq_tail_ptr = (sq_tail + 1) % admin_sq_size;
    
    /* Ring the submission queue doorbell */
    *admin_sq_doorbell = *admin_sq_tail_ptr;
    
    printf("Submitted Create I/O Completion Queue %u command (CMD_ID=%u)\n", 
           cq_id, cmd_id);
    return 0;
}

/**
 * Create NVMe I/O Submission Queue without using structs
 *
 * @param admin_sq_buffer    Pointer to admin submission queue memory
 * @param admin_sq_tail_ptr  Pointer to admin submission queue tail index
 * @param admin_sq_size      Size of admin submission queue (entries)
 * @param admin_sq_doorbell  Pointer to admin submission queue doorbell register
 * @param sq_buffer          Pointer to I/O submission queue memory
 * @param sq_size            Size of I/O submission queue (entries)
 * @param sq_id              Submission queue identifier
 * @param cq_id              Associated completion queue identifier
 * @param cmd_id             Command ID to use
 *
 * @return 0 on success, negative error code on failure
 */
int nvme_create_io_submission_queue_nostruct(
    void *admin_sq_buffer,
    uint32_t *admin_sq_tail_ptr,
    uint32_t admin_sq_size,
    volatile uint32_t *admin_sq_doorbell,
    void *sq_buffer,
    uint32_t sq_size,
    uint16_t sq_id,
    uint16_t cq_id,
    uint16_t cmd_id)
{
    uint32_t sq_tail = *admin_sq_tail_ptr;
    
    /* Calculate pointer to the next available submission queue entry */
    uint8_t *sq_entry = (uint8_t *)admin_sq_buffer + (sq_tail * SQE_SIZE);
    
    /* Clear the entire entry */
    memset(sq_entry, 0, SQE_SIZE);
    
    /* Fill in the command */
    sq_entry[SQE_OPCODE_OFFSET] = NVME_ADMIN_CMD_CREATE_SQ;   /* Opcode: Create I/O Submission Queue */
    sq_entry[SQE_FLAGS_OFFSET] = 0;                          /* Flags */
    *(uint16_t *)(sq_entry + SQE_COMMAND_ID_OFFSET) = cmd_id;/* Command ID */
    
    /* Set up PRP for submission queue */
    *(uint64_t *)(sq_entry + SQE_PRP1_OFFSET) = (uint64_t)sq_buffer;
    
    /* Command Dword 10: Queue Size and Queue Identifier */
    uint32_t cdw10 = ((sq_size - 1) & 0xFFFF) | (sq_id << 16);
    *(uint32_t *)(sq_entry + SQE_CDW10_OFFSET) = cdw10;
    
    /* Command Dword 11: Physically Contiguous, Queue Priority = Medium (bit 1),
       and associated CQ ID */
    uint32_t cdw11 = (1 << 0) | (1 << 1) | (cq_id << 16);
    *(uint32_t *)(sq_entry + SQE_CDW11_OFFSET) = cdw11;
    
    /* Update submission queue tail pointer */
    *admin_sq_tail_ptr = (sq_tail + 1) % admin_sq_size;
    
    /* Ring the submission queue doorbell */
    *admin_sq_doorbell = *admin_sq_tail_ptr;
    
    printf("Submitted Create I/O Submission Queue %u command (CMD_ID=%u, CQ_ID=%u)\n", 
           sq_id, cmd_id, cq_id);
    return 0;
}

/**
 * Create NVMe I/O Queue Pairs without using structs
 *
 * @param reg_base           Base address of NVMe controller registers
 * @param admin_sq_buffer    Pointer to admin submission queue memory
 * @param admin_sq_size      Size of admin submission queue (entries)
 * @param admin_sq_tail_ptr  Pointer to admin submission queue tail index
 * @param admin_sq_head_ptr  Pointer to admin submission queue head index
 * @param admin_sq_doorbell  Pointer to admin submission queue doorbell register
 * @param admin_cq_buffer    Pointer to admin completion queue memory
 * @param admin_cq_size      Size of admin completion queue (entries)
 * @param admin_cq_head_ptr  Pointer to admin completion queue head index
 * @param admin_cq_phase_ptr Pointer to admin completion queue phase bit
 * @param admin_cq_doorbell  Pointer to admin completion queue doorbell register
 * @param num_queues         Number of I/O queue pairs to create
 * @param queue_size         Size of each I/O queue (entries)
 * @param db_stride          Doorbell stride from NVMe capabilities
 * @param next_cmd_id_ptr    Pointer to next command ID to use (will be updated)
 * @param poll_completion_fn Pointer to completion polling function
 *
 * @return 0 on success, negative error code on failure
 */
int nvme_create_io_queues_nostruct(
    volatile uint32_t *reg_base,
    void *admin_sq_buffer,
    uint32_t admin_sq_size,
    uint32_t *admin_sq_tail_ptr,
    uint32_t *admin_sq_head_ptr,
    volatile uint32_t *admin_sq_doorbell,
    void *admin_cq_buffer,
    uint32_t admin_cq_size,
    uint32_t *admin_cq_head_ptr,
    uint32_t *admin_cq_phase_ptr,
    volatile uint32_t *admin_cq_doorbell,
    uint32_t num_queues,
    uint32_t queue_size,
    uint32_t db_stride,
    uint16_t *next_cmd_id_ptr,
    int (*poll_completion_fn)(
        void *cq_buffer,
        uint32_t cq_size,
        uint32_t *cq_head_ptr,
        uint32_t *cq_phase_ptr,
        volatile uint32_t *cq_doorbell,
        uint16_t cmd_id,
        uint32_t timeout_ms,
        uint32_t *sq_head_ptr))
{
    int ret;
    uint16_t cmd_id;
    
    /* Array to hold queue memory pointers */
    void **io_cq_buffers = calloc(num_queues, sizeof(void*));
    void **io_sq_buffers = calloc(num_queues, sizeof(void*));
    
    if (!io_cq_buffers || !io_sq_buffers) {
        printf("Error: Failed to allocate queue pointer arrays\n");
        if (io_cq_buffers) free(io_cq_buffers);
        if (io_sq_buffers) free(io_sq_buffers);
        return -1;
    }
    
    /* Skip admin queue (ID 0) which is already initialized */
    for (uint16_t i = 1; i < num_queues; i++) {
        /* Allocate and clear memory for I/O queues */
        void *cq_buffer = aligned_alloc(4096, queue_size * 16);  /* 16 bytes per CQ entry */
        void *sq_buffer = aligned_alloc(4096, queue_size * 64);  /* 64 bytes per SQ entry */
        
        if (!cq_buffer || !sq_buffer) {
            printf("Error: Failed to allocate memory for I/O queue %u\n", i);
            if (cq_buffer) free(cq_buffer);
            if (sq_buffer) free(sq_buffer);
            
            /* Clean up already allocated queues */
            for (uint16_t j = 1; j < i; j++) {
                if (io_cq_buffers[j]) free(io_cq_buffers[j]);
                if (io_sq_buffers[j]) free(io_sq_buffers[j]);
            }
            free(io_cq_buffers);
            free(io_sq_buffers);
            return -1;
        }
        
        /* Store allocated buffers */
        io_cq_buffers[i] = cq_buffer;
        io_sq_buffers[i] = sq_buffer;
        
        /* Clear queue memory */
        memset(cq_buffer, 0, queue_size * 16);
        memset(sq_buffer, 0, queue_size * 64);
        
        /* First create I/O completion queue */
        cmd_id = (*next_cmd_id_ptr)++;
        ret = nvme_create_io_completion_queue_nostruct(
            admin_sq_buffer,
            admin_sq_tail_ptr,
            admin_sq_size,
            admin_sq_doorbell,
            cq_buffer,
            queue_size,
            i,   /* CQ ID = queue index */
            cmd_id
        );
        
        if (ret) {
            printf("Error: Failed to create I/O completion queue %u\n", i);
            /* Clean up allocated queues */
            for (uint16_t j = 1; j <= i; j++) {
                if (io_cq_buffers[j]) free(io_cq_buffers[j]);
                if (io_sq_buffers[j]) free(io_sq_buffers[j]);
            }
            free(io_cq_buffers);
            free(io_sq_buffers);
            return ret;
        }
        
        /* Wait for completion */
        ret = poll_completion_fn(
            admin_cq_buffer,
            admin_cq_size,
            admin_cq_head_ptr,
            admin_cq_phase_ptr,
            admin_cq_doorbell,
            cmd_id,
            5000,  /* 5 second timeout */
            admin_sq_head_ptr
        );
        
        if (ret) {
            printf("Error: Create I/O completion queue %u command failed\n", i);
            /* Clean up allocated queues */
            for (uint16_t j = 1; j <= i; j++) {
                if (io_cq_buffers[j]) free(io_cq_buffers[j]);
                if (io_sq_buffers[j]) free(io_sq_buffers[j]);
            }
            free(io_cq_buffers);
            free(io_sq_buffers);
            return ret;
        }
        
        printf("Created I/O Completion Queue %u with %u entries\n", i, queue_size);
        
        /* Now create I/O submission queue associated with the completion queue */
        cmd_id = (*next_cmd_id_ptr)++;
        ret = nvme_create_io_submission_queue_nostruct(
            admin_sq_buffer,
            admin_sq_tail_ptr,
            admin_sq_size,
            admin_sq_doorbell,
            sq_buffer,
            queue_size,
            i,   /* SQ ID = queue index */
            i,   /* CQ ID = same as SQ ID */
            cmd_id
        );
        
        if (ret) {
            printf("Error: Failed to create I/O submission queue %u\n", i);
            /* Clean up allocated queues */
            for (uint16_t j = 1; j <= i; j++) {
                if (io_cq_buffers[j]) free(io_cq_buffers[j]);
                if (io_sq_buffers[j]) free(io_sq_buffers[j]);
            }
            free(io_cq_buffers);
            free(io_sq_buffers);
            return ret;
        }
        
        /* Wait for completion */
        ret = poll_completion_fn(
            admin_cq_buffer,
            admin_cq_size,
            admin_cq_head_ptr,
            admin_cq_phase_ptr,
            admin_cq_doorbell,
            cmd_id,
            5000,  /* 5 second timeout */
            admin_sq_head_ptr
        );
        
        if (ret) {
            printf("Error: Create I/O submission queue %u command failed\n", i);
            /* Clean up allocated queues */
            for (uint16_t j = 1; j <= i; j++) {
                if (io_cq_buffers[j]) free(io_cq_buffers[j]);
                if (io_sq_buffers[j]) free(io_sq_buffers[j]);
            }
            free(io_cq_buffers);
            free(io_sq_buffers);
            return ret;
        }
        
        printf("Created I/O Submission Queue %u with %u entries\n", i, queue_size);
    }
    
    /* Successfully created all queues */
    printf("Successfully created %u I/O queue pairs with %u entries each\n", 
           num_queues - 1, queue_size);
           
    /* Note: We're deliberately not freeing io_cq_buffers and io_sq_buffers arrays
       or their contents because these are the actual queue memory that will be
       used for I/O operations. In a real implementation, you would store these
       pointers somewhere for later use and cleanup. */
    free(io_cq_buffers);
    free(io_sq_buffers);
    
    return 0;
}




/**
 * Calculate NVMe doorbell register address without using structs
 *
 * @param reg_base     Base address of NVMe controller registers
 * @param is_sq        True for submission queue, false for completion queue
 * @param queue_id     Queue identifier
 * @param db_stride    Doorbell stride from controller capabilities
 *
 * @return Pointer to doorbell register
 */
 volatile uint32_t* nvme_get_doorbell_address(
    volatile uint32_t *reg_base,
    bool is_sq,
    uint16_t queue_id,
    uint32_t db_stride)
{
    /* Base doorbell registers for admin queues - use local constants to avoid conflict with macros */
    const uint32_t REG_SQ0TDBL_OFFSET = 0x1000;  /* Submission Queue 0 Tail Doorbell */
    const uint32_t REG_CQ0HDBL_OFFSET = 0x1004;  /* Completion Queue 0 Head Doorbell */
    
    /* Calculate doorbell stride in bytes */
    uint32_t stride_bytes = 1 << (2 + db_stride);
    uint32_t offset;
    
    if (is_sq) {
        /* Submission Queue Tail Doorbell */
        if (queue_id == 0) {
            /* Admin Submission Queue */
            offset = REG_SQ0TDBL_OFFSET;
        } else {
            /* I/O Submission Queue */
            offset = REG_SQ0TDBL_OFFSET + (2 * queue_id) * stride_bytes;
        }
    } else {
        /* Completion Queue Head Doorbell */
        if (queue_id == 0) {
            /* Admin Completion Queue */
            offset = REG_CQ0HDBL_OFFSET;
        } else {
            /* I/O Completion Queue */
            offset = REG_CQ0HDBL_OFFSET + (2 * queue_id) * stride_bytes;
        }
    }
    
    /* Calculate and return the address */
    return (volatile uint32_t*)((volatile uint8_t*)reg_base + offset);
}

/**
 * Ring NVMe submission queue doorbell without using structs
 *
 * @param reg_base     Base address of NVMe controller registers
 * @param queue_id     Queue identifier
 * @param tail_ptr     Pointer to submission queue tail index (will be updated)
 * @param size         Size of the queue (entries)
 * @param db_stride    Doorbell stride from controller capabilities
 *
 * @return 0 on success, negative error code on failure
 */
int nvme_ring_sq_doorbell(
    volatile uint32_t *reg_base,
    uint16_t queue_id,
    uint32_t *tail_ptr,
    uint32_t size,
    uint32_t db_stride)
{
    /* Update tail index (caller is responsible for incrementing it) */
    *tail_ptr = *tail_ptr % size;
    
    /* Get doorbell register address */
    volatile uint32_t *doorbell = nvme_get_doorbell_address(
        reg_base,
        true,   /* is_sq = true for submission queue */
        queue_id,
        db_stride
    );
    
    /* Ring the doorbell */
    *doorbell = *tail_ptr;
    
    return 0;
}

/**
 * Ring NVMe completion queue doorbell without using structs
 *
 * @param reg_base     Base address of NVMe controller registers
 * @param queue_id     Queue identifier
 * @param head_ptr     Pointer to completion queue head index (will be updated)
 * @param size         Size of the queue (entries)
 * @param phase_ptr    Pointer to completion queue phase tag (will be updated)
 * @param db_stride    Doorbell stride from controller capabilities
 *
 * @return 0 on success, negative error code on failure
 */
int nvme_ring_cq_doorbell(
    volatile uint32_t *reg_base,
    uint16_t queue_id,
    uint32_t *head_ptr,
    uint32_t size,
    uint32_t *phase_ptr,
    uint32_t db_stride)
{
    /* Update head index (caller is responsible for incrementing it) */
    *head_ptr = *head_ptr % size;
    
    /* Check if we wrapped around, if so toggle phase bit */
    if (*head_ptr == 0) {
        *phase_ptr = !(*phase_ptr);
    }
    
    /* Get doorbell register address */
    volatile uint32_t *doorbell = nvme_get_doorbell_address(
        reg_base,
        false,  /* is_sq = false for completion queue */
        queue_id,
        db_stride
    );
    
    /* Ring the doorbell */
    *doorbell = *head_ptr;
    
    return 0;
}


/**
 * Submit command to NVMe submission queue and ring doorbell without using structs
 *
 * @param reg_base       Base address of NVMe controller registers
 * @param sq_buffer      Pointer to submission queue memory
 * @param sq_size        Size of the submission queue (entries)
 * @param sq_entry_size  Size of each submission queue entry in bytes
 * @param sq_tail_ptr    Pointer to submission queue tail index (will be updated)
 * @param queue_id       Queue identifier
 * @param db_stride      Doorbell stride from controller capabilities
 * @param entry_data     Command data to be copied to submission queue entry
 * @param entry_size     Size of command data in bytes
 *
 * @return 0 on success, negative error code on failure
 */
int nvme_submit_command_nostruct(
    volatile uint32_t *reg_base,
    void *sq_buffer,
    uint32_t sq_size,
    uint32_t sq_entry_size,
    uint32_t *sq_tail_ptr,
    uint16_t queue_id,
    uint32_t db_stride,
    const void *entry_data,
    uint32_t entry_size)
{
    /* Calculate pointer to the next available submission queue entry */
    uint8_t *sq_entry = (uint8_t *)sq_buffer + (*sq_tail_ptr * sq_entry_size);
    
    /* Clear the entry and copy command data */
    memset(sq_entry, 0, sq_entry_size);
    memcpy(sq_entry, entry_data, entry_size < sq_entry_size ? entry_size : sq_entry_size);
    
    /* Increment tail pointer */
    *sq_tail_ptr = (*sq_tail_ptr + 1) % sq_size;
    
    /* Ring the doorbell */
    return nvme_ring_sq_doorbell(
        reg_base,
        queue_id,
        sq_tail_ptr,
        sq_size,
        db_stride
    );
}







/* For forward declarations - would typically be in a header file */
extern int nvme_reset_and_enable_controller_nostruct(
    volatile uint32_t *reg_base, uint32_t timeout_ms);
    
extern int nvme_init_admin_queue_nostruct(
    volatile uint32_t *reg_base,
    void *admin_sq_buffer,
    uint32_t admin_sq_size,
    void *admin_cq_buffer,
    uint32_t admin_cq_size,
    uint32_t db_stride,
    uint32_t *sq_tail_ptr,
    uint32_t *sq_head_ptr,
    uint32_t *cq_head_ptr,
    uint32_t *cq_phase_ptr,
    volatile uint32_t **sq_doorbell_ptr,
    volatile uint32_t **cq_doorbell_ptr);
    
extern int nvme_submit_identify_controller_nostruct(
    void *sq_buffer,
    uint32_t *sq_tail_ptr,
    uint32_t sq_size,
    volatile uint32_t *sq_doorbell,
    void *identify_data,
    uint16_t cmd_id);
    
    
extern int nvme_process_identify_controller_nostruct(
    void *identify_data,
    uint16_t *max_queues_ptr);
    
extern int nvme_create_io_queues_nostruct(
    volatile uint32_t *reg_base,
    void *admin_sq_buffer,
    uint32_t admin_sq_size,
    uint32_t *admin_sq_tail_ptr,
    uint32_t *admin_sq_head_ptr,
    volatile uint32_t *admin_sq_doorbell,
    void *admin_cq_buffer,
    uint32_t admin_cq_size,
    uint32_t *admin_cq_head_ptr,
    uint32_t *admin_cq_phase_ptr,
    volatile uint32_t *admin_cq_doorbell,
    uint32_t num_queues,
    uint32_t queue_size,
    uint32_t db_stride,
    uint16_t *next_cmd_id_ptr,
    int (*poll_completion_fn)(
        void *cq_buffer,
        uint32_t cq_size,
        uint32_t *cq_head_ptr,
        uint32_t *cq_phase_ptr,
        volatile uint32_t *cq_doorbell,
        uint16_t cmd_id,
        uint32_t timeout_ms,
        uint32_t *sq_head_ptr));


/* External PCIe functions */
extern uint64_t pcie_get_bar_address(uint8_t bus, uint8_t device, uint8_t function, uint8_t bar_num);
extern uint64_t pcie_get_bar_size(uint8_t bus, uint8_t device, uint8_t function, uint8_t bar_num);
extern void pcie_enable_device(uint8_t bus, uint8_t device, uint8_t function);
extern bool pcie_find_device(uint8_t class_code, uint8_t subclass, uint8_t prog_if,
                           uint8_t* out_bus, uint8_t* out_device, uint8_t* out_function);

/**
 * Initialize NVMe controller and prepare for I/O operations without using structs
 *
 * @param bus                 PCIe bus number
 * @param device              PCIe device number
 * @param function            PCIe function number
 * @param reg_base_ptr        Pointer to store register base address
 * @param admin_sq_buffer_ptr Pointer to store admin submission queue buffer
 * @param admin_sq_size_ptr   Pointer to store admin submission queue size
 * @param admin_sq_tail_ptr   Pointer to store admin submission queue tail
 * @param admin_sq_head_ptr   Pointer to store admin submission queue head
 * @param admin_sq_doorbell_ptr Pointer to store admin submission queue doorbell
 * @param admin_cq_buffer_ptr Pointer to store admin completion queue buffer
 * @param admin_cq_size_ptr   Pointer to store admin completion queue size
 * @param admin_cq_head_ptr   Pointer to store admin completion queue head
 * @param admin_cq_phase_ptr  Pointer to store admin completion queue phase
 * @param admin_cq_doorbell_ptr Pointer to store admin completion queue doorbell
 * @param db_stride_ptr       Pointer to store doorbell stride
 * @param next_cmd_id_ptr     Pointer to store next command ID
 *
 * @return 0 on success, negative error code on failure
 */
int nvme_init_device_nostruct(
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    volatile uint32_t **reg_base_ptr,
    void **admin_sq_buffer_ptr,
    uint32_t *admin_sq_size_ptr,
    uint32_t *admin_sq_tail_ptr,
    uint32_t *admin_sq_head_ptr,
    volatile uint32_t **admin_sq_doorbell_ptr,
    void **admin_cq_buffer_ptr,
    uint32_t *admin_cq_size_ptr,
    uint32_t *admin_cq_head_ptr,
    uint32_t *admin_cq_phase_ptr,
    volatile uint32_t **admin_cq_doorbell_ptr,
    uint32_t *db_stride_ptr,
    uint16_t *next_cmd_id_ptr)
{
    int ret;
    
    /* Enable PCIe device */
    pcie_enable_device(bus, device, function);
    
    /* Map BAR0 which contains controller registers */
    uint64_t bar0_addr = pcie_get_bar_address(bus, device, function, 0);
    uint64_t bar0_size = pcie_get_bar_size(bus, device, function, 0);
    
    if (bar0_addr == 0 || bar0_size == 0) {
        printf("Error: Failed to map NVMe controller registers (BAR0)\n");
        return -1;
    }
    
    printf("NVMe controller registers mapped at 0x%016lx (size: %lu bytes)\n", 
           bar0_addr, bar0_size);
    
    /* Map controller registers */
    volatile uint32_t *reg_base = (volatile uint32_t *)bar0_addr;
    *reg_base_ptr = reg_base;
    
    /* Reset and enable controller */
    ret = nvme_reset_and_enable_controller_nostruct(reg_base, 5000);
    if (ret) {
        printf("Error: Failed to reset and enable NVMe controller\n");
        return ret;
    }
    
    /* Read controller capabilities for doorbell stride */
    uint32_t cap_lo = reg_base[0];
    uint32_t cap_hi = reg_base[1];
    uint64_t cap = ((uint64_t)cap_hi << 32) | cap_lo;
    
    /* Extract doorbell stride */
    uint32_t db_stride = (cap >> 32) & 0xF;
    *db_stride_ptr = db_stride;
    
    /* Initialize admin queue variables */
    uint32_t admin_sq_size = 64;
    uint32_t admin_cq_size = 64;
    *admin_sq_size_ptr = admin_sq_size;
    *admin_cq_size_ptr = admin_cq_size;
    *admin_sq_tail_ptr = 0;
    *admin_sq_head_ptr = 0;
    *admin_cq_head_ptr = 0;
    *admin_cq_phase_ptr = 1;
    *next_cmd_id_ptr = 0;
    
    /* Allocate admin queue memory */
    void *admin_sq_buffer = aligned_alloc(4096, admin_sq_size * 64);  /* 64 bytes per entry */
    void *admin_cq_buffer = aligned_alloc(4096, admin_cq_size * 16);  /* 16 bytes per entry */
    if (!admin_sq_buffer || !admin_cq_buffer) {
        printf("Error: Failed to allocate admin queue memory\n");
        if (admin_sq_buffer) free(admin_sq_buffer);
        if (admin_cq_buffer) free(admin_cq_buffer);
        return -1;
    }
    
    *admin_sq_buffer_ptr = admin_sq_buffer;
    *admin_cq_buffer_ptr = admin_cq_buffer;
    
    /* Initialize admin queues */
    ret = nvme_init_admin_queue_nostruct(
        reg_base,
        admin_sq_buffer,
        admin_sq_size,
        admin_cq_buffer,
        admin_cq_size,
        db_stride,
        admin_sq_tail_ptr,
        admin_sq_head_ptr,
        admin_cq_head_ptr,
        admin_cq_phase_ptr,
        admin_sq_doorbell_ptr,
        admin_cq_doorbell_ptr
    );
    
    if (ret) {
        printf("Error: Failed to initialize admin queues\n");
        free(admin_sq_buffer);
        free(admin_cq_buffer);
        return ret;
    }
    
    return 0;
}

/**
 * Identify NVMe controller and create I/O queues without using structs
 *
 * @param reg_base            Register base address
 * @param admin_sq_buffer     Admin submission queue buffer
 * @param admin_sq_size       Admin submission queue size
 * @param admin_sq_tail_ptr   Admin submission queue tail pointer
 * @param admin_sq_head_ptr   Admin submission queue head pointer
 * @param admin_sq_doorbell   Admin submission queue doorbell register
 * @param admin_cq_buffer     Admin completion queue buffer
 * @param admin_cq_size       Admin completion queue size
 * @param admin_cq_head_ptr   Admin completion queue head pointer
 * @param admin_cq_phase_ptr  Admin completion queue phase pointer
 * @param admin_cq_doorbell   Admin completion queue doorbell register
 * @param db_stride           Doorbell stride
 * @param next_cmd_id_ptr     Next command ID (will be updated)
 * @param num_io_queues       Number of I/O queues to create
 *
 * @return 0 on success, negative error code on failure
 */
int nvme_setup_io_queues_nostruct(
    volatile uint32_t *reg_base,
    void *admin_sq_buffer,
    uint32_t admin_sq_size,
    uint32_t *admin_sq_tail_ptr,
    uint32_t *admin_sq_head_ptr,
    volatile uint32_t *admin_sq_doorbell,
    void *admin_cq_buffer,
    uint32_t admin_cq_size,
    uint32_t *admin_cq_head_ptr,
    uint32_t *admin_cq_phase_ptr,
    volatile uint32_t *admin_cq_doorbell,
    uint32_t db_stride,
    uint16_t *next_cmd_id_ptr,
    uint32_t num_io_queues)
{
    int ret;
    uint16_t cmd_id;
    
    /* Allocate identify data buffer */
    void *identify_data = aligned_alloc(4096, 4096);
    if (!identify_data) {
        printf("Error: Failed to allocate identify data buffer\n");
        return -1;
    }
    
    /* Submit identify controller command */
    cmd_id = (*next_cmd_id_ptr)++;
    ret = nvme_submit_identify_controller_nostruct(
        admin_sq_buffer,
        admin_sq_tail_ptr,
        admin_sq_size,
        admin_sq_doorbell,
        identify_data,
        cmd_id
    );
    
    if (ret) {
        printf("Error: Failed to submit identify controller command\n");
        free(identify_data);
        return ret;
    }
    
    /* Wait for completion */
    ret = nvme_poll_completion_nostruct(
        admin_cq_buffer,
        admin_cq_size,
        admin_cq_head_ptr,
        admin_cq_phase_ptr,
        admin_cq_doorbell,
        cmd_id,
        5000, /* 5 second timeout */
        admin_sq_head_ptr
    );
    
    if (ret) {
        printf("Error: Identify controller command failed\n");
        free(identify_data);
        return ret;
    }
    
    /* Process identify controller data */
    uint16_t max_queues;
    ret = nvme_process_identify_controller_nostruct(identify_data, &max_queues);
    if (ret) {
        printf("Error: Failed to process identify controller data\n");
        free(identify_data);
        return ret;
    }
    
    /* Check queue limits */
    if (num_io_queues > max_queues + 1) {
        printf("Warning: Controller only supports %u queues, requested %u\n", 
               max_queues + 1, num_io_queues);
        num_io_queues = max_queues + 1;
    }
    
    free(identify_data);
    
    /* Create I/O queues */
    ret = nvme_create_io_queues_nostruct(
        reg_base,
        admin_sq_buffer,
        admin_sq_size,
        admin_sq_tail_ptr,
        admin_sq_head_ptr,
        admin_sq_doorbell,
        admin_cq_buffer,
        admin_cq_size,
        admin_cq_head_ptr,
        admin_cq_phase_ptr,
        admin_cq_doorbell,
        num_io_queues,
        1024, /* Queue size */
        db_stride,
        next_cmd_id_ptr,
        nvme_poll_completion_nostruct
    );
    
    if (ret) {
        printf("Error: Failed to create I/O queues\n");
        return ret;
    }
    
    return 0;
}
