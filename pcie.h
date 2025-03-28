#ifndef PCIE_H
#define PCIE_H

#include <stdint.h>
#include <stdbool.h>

/* PCIe BAR types */
enum pcie_bar_type {
    PCIE_BAR_UNKNOWN,
    PCIE_BAR_IO,
    PCIE_BAR_MEM16,
    PCIE_BAR_MEM32,
    PCIE_BAR_MEM64
};

/* PCIe device information structure */
struct pcie_device {
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
    
    /* PCIe-specific capabilities */
    uint16_t pcie_cap_offset;
    uint8_t device_type;
    uint32_t link_capabilities;
    uint16_t link_status;
    uint16_t link_control;
    
    uint8_t max_link_speed;
    uint8_t max_link_width;
    uint8_t current_link_speed;
    uint8_t current_link_width;
    
    uint32_t max_payload_size;
    uint32_t max_read_request_size;
    
    const char* name;
};

/* Function prototypes */
void init_pcie(void);
void pcie_optimize_device(uint8_t bus, uint8_t device, uint8_t function);
void list_nvme_devices(void);
void initialize_nvme_device(uint8_t bus, uint8_t device, uint8_t function);
void nvme_read_write_test(uint8_t bus, uint8_t device, uint8_t function);
int pcie_test(void);
void pcie_setup_aer(uint8_t bus, uint8_t device, uint8_t function);
void enumerate_pcie_devices(void);
struct pcie_device pcie_get_device_info(uint8_t bus, uint8_t device, uint8_t function);
void pcie_enable_device(uint8_t bus, uint8_t device, uint8_t function);
bool pcie_find_device(uint8_t class_code, uint8_t subclass, uint8_t prog_if,
                     uint8_t* out_bus, uint8_t* out_device, uint8_t* out_function);
uint64_t pcie_get_bar_size(uint8_t bus, uint8_t device, uint8_t function, uint8_t bar_num);
enum pcie_bar_type pcie_get_bar_type(uint8_t bus, uint8_t device, uint8_t function, uint8_t bar_num);
uint64_t pcie_get_bar_address(uint8_t bus, uint8_t device, uint8_t function, uint8_t bar_num);

/* PCIe configuration space access functions */
uint8_t pcie_config_read_byte(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset);
uint16_t pcie_config_read_word(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset);
uint32_t pcie_config_read_dword(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset);
void pcie_config_write_byte(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint8_t value);
void pcie_config_write_word(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint16_t value);
void pcie_config_write_dword(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint32_t value);

/* Helper functions */
const char* get_pcie_link_speed_str(uint8_t speed);
const char* get_pcie_device_type_str(uint8_t device_type);

#endif /* PCIE_H */