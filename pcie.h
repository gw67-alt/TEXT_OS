#ifndef PCIE_H
#define PCIE_H

#include <stdint.h>
#include <stdbool.h>

/* PCIe Base Address Register (BAR) types */
enum pcie_bar_type {
    PCIE_BAR_IO,       // I/O Space
    PCIE_BAR_MEM32,    // 32-bit Memory Space
    PCIE_BAR_MEM16,    // 16-bit Memory Space (Below 1MB)
    PCIE_BAR_MEM64,    // 64-bit Memory Space
    PCIE_BAR_UNKNOWN   // Unknown BAR type
};

/* PCIe device structure - extended from basic PCI */
struct pcie_device {
    /* Basic PCI Configuration Space - First 256 bytes */
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    
    uint16_t vendor_id;
    uint16_t device_id;
    
    uint16_t command;
    uint16_t status;
    
    uint8_t revision_id;
    uint8_t prog_if;
    uint8_t subclass;
    uint8_t class_code;
    
    uint8_t cache_line_size;
    uint8_t latency_timer;
    uint8_t header_type;
    uint8_t bist;
    
    uint32_t bar[6];
    
    uint32_t cardbus_cis_ptr;
    uint16_t subsystem_vendor_id;
    uint16_t subsystem_id;
    uint32_t expansion_rom_base_addr;
    uint8_t capabilities_ptr;
    
    uint8_t interrupt_line;
    uint8_t interrupt_pin;
    uint8_t min_grant;
    uint8_t max_latency;
    
    /* PCIe specific capabilities */
    uint16_t pcie_cap_offset;    // Offset to PCIe capability structure
    uint8_t device_type;         // PCIe device/port type
    
    uint32_t link_capabilities;  // Link Capabilities Register
    uint16_t link_control;       // Link Control Register
    uint16_t link_status;        // Link Status Register
    
    uint8_t max_link_speed;      // Maximum link speed
    uint8_t max_link_width;      // Maximum link width
    uint8_t current_link_speed;  // Current link speed
    uint8_t current_link_width;  // Current link width
    
    uint16_t max_payload_size;   // Maximum payload size in bytes
    uint16_t max_read_request_size; // Maximum read request size in bytes
    
    const char* name;            // Device name or description
};

/* Function prototypes */

/* Configuration space access */
uint8_t pcie_config_read_byte(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset);
uint16_t pcie_config_read_word(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset);
uint32_t pcie_config_read_dword(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset);

void pcie_config_write_byte(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint8_t value);
void pcie_config_write_word(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint16_t value);
void pcie_config_write_dword(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint32_t value);

/* Device information and enumeration */
struct pcie_device pcie_get_device_info(uint8_t bus, uint8_t device, uint8_t function);
void enumerate_pcie_devices(void);
bool pcie_find_device(uint8_t class_code, uint8_t subclass, uint8_t prog_if,
                     uint8_t* out_bus, uint8_t* out_device, uint8_t* out_function);

/* Device control and optimization */
void pcie_enable_device(uint8_t bus, uint8_t device, uint8_t function);
void pcie_optimize_device(uint8_t bus, uint8_t device, uint8_t function);
void pcie_setup_aer(uint8_t bus, uint8_t device, uint8_t function);

/* Base Address Register (BAR) operations */
uint64_t pcie_get_bar_size(uint8_t bus, uint8_t device, uint8_t function, uint8_t bar_num);
enum pcie_bar_type pcie_get_bar_type(uint8_t bus, uint8_t device, uint8_t function, uint8_t bar_num);
uint64_t pcie_get_bar_address(uint8_t bus, uint8_t device, uint8_t function, uint8_t bar_num);

/* Helper functions */
const char* get_pcie_link_speed_str(uint8_t speed);
const char* get_pcie_device_type_str(uint8_t device_type);

/* Module initialization */
void init_pcie(void);
int pcie_test(void);

/* NVMe specific functions */
void list_nvme_devices(void);
void initialize_nvme_device(uint8_t bus, uint8_t device, uint8_t function);
void nvme_read_write_test(uint8_t bus, uint8_t device, uint8_t function);

#endif /* PCIE_H */