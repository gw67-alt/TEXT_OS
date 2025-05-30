#ifndef PCIE_DRIVER_H
#define PCIE_DRIVER_H

#include <stdint.h>
#include <stddef.h>


// PCIe Configuration Space Registers
#define PCIE_CONFIG_VENDOR_ID     0x00
#define PCIE_CONFIG_DEVICE_ID     0x02
#define PCIE_CONFIG_COMMAND       0x04
#define PCIE_CONFIG_STATUS        0x06
#define PCIE_CONFIG_CLASS_CODE    0x08
#define PCIE_CONFIG_BAR0          0x10
#define PCIE_CONFIG_BAR1          0x14
#define PCIE_CONFIG_BAR2          0x18
#define PCIE_CONFIG_BAR3          0x1C
#define PCIE_CONFIG_BAR4          0x20
#define PCIE_CONFIG_BAR5          0x24

// PCIe Command Register Bits
#define PCIE_CMD_IO_ENABLE        0x0001
#define PCIE_CMD_MEMORY_ENABLE    0x0002
#define PCIE_CMD_BUS_MASTER       0x0004
#define PCIE_CMD_INTERRUPT_DISABLE 0x0400

// In pcie_driver.h (expected structure based on usage)
typedef struct {
    bool use_pcie;
    bool is_read; // <--- ADD THIS MEMBER
    uint8_t value;
    uint32_t address;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t offset;
    // Potentially other members
} DriverCommand;


// PCIe device structure
struct PCIeDevice {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t class_code;
    uint32_t bar[6];
    bool valid;
};

// Function declarations
void init_pcie_driver();
void driver_cfg(char* input);
void cmd_driver();
bool parse_driver_command(const char* input, DriverCommand* cmd);
uint32_t pcie_config_read32(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset);
uint16_t pcie_config_read16(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset);
uint8_t pcie_config_read8(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset);
void pcie_config_write32(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint32_t value);
void pcie_config_write16(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint16_t value);
void pcie_config_write8(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint8_t value);
bool pcie_device_exists(uint8_t bus, uint8_t device, uint8_t function);
void pcie_enable_device(uint8_t bus, uint8_t device, uint8_t function);
void pcie_scan_devices();
void driver_memory_write(uint32_t address, uint8_t value);
void driver_pcie_write(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint8_t value);
void print_pcie_device_info(const PCIeDevice* dev);

// Utility functions
uint8_t hex_char_to_value(char c);
uint32_t hex_string_to_uint32(const char* hex_str);
uint8_t hex_string_to_uint8(const char* hex_str);

#endif // PCIE_DRIVER_H
