#ifndef PCI_H
#define PCI_H

#include <stdbool.h>
#include <stdint.h>

/* PCI device structure */
struct pci_device {
    uint8_t bus;              // Bus number
    uint8_t device;           // Device number
    uint8_t function;         // Function number
    
    uint16_t vendor_id;       // Vendor ID
    uint16_t device_id;       // Device ID
    
    uint16_t command;         // Command register
    uint16_t status;          // Status register
    
    uint8_t revision_id;      // Revision ID
    uint8_t prog_if;          // Programming Interface
    uint8_t subclass;         // Device Subclass
    uint8_t class_code;       // Device Class
    
    uint8_t cache_line_size;  // Cache Line Size
    uint8_t latency_timer;    // Latency Timer
    uint8_t header_type;      // Header Type
    uint8_t bist;             // Built-in Self Test
    
    uint32_t bar[6];          // Base Address Registers
    
    uint32_t cardbus_cis_ptr; // CardBus CIS Pointer
    uint16_t subsystem_vendor_id; // Subsystem Vendor ID
    uint16_t subsystem_id;    // Subsystem ID
    uint32_t expansion_rom_base_addr; // Expansion ROM Base Address
    
    uint8_t capabilities_ptr; // Capabilities Pointer
    
    uint8_t interrupt_line;   // Interrupt Line
    uint8_t interrupt_pin;    // Interrupt Pin
    uint8_t min_grant;        // Min Grant
    uint8_t max_latency;      // Max Latency
    
    const char* name;         // Device name (if known)
};

/* Base Address Register (BAR) types */
enum pci_bar_type {
    PCI_BAR_UNKNOWN,  // Unknown BAR type
    PCI_BAR_IO,       // I/O space BAR
    PCI_BAR_MEM32,    // 32-bit memory space BAR
    PCI_BAR_MEM16,    // 16-bit memory space BAR (below 1MB)
    PCI_BAR_MEM64     // 64-bit memory space BAR
};

/* PCI configuration space access functions */

/**
 * Read a byte from PCI configuration space
 * 
 * @param bus Bus number
 * @param device Device number
 * @param function Function number
 * @param offset Register offset
 * @return Byte value read from configuration space
 */
uint8_t pci_config_read_byte(uint8_t bus, uint8_t device, 
                            uint8_t function, uint8_t offset);

/**
 * Read a word (16 bits) from PCI configuration space
 * 
 * @param bus Bus number
 * @param device Device number
 * @param function Function number
 * @param offset Register offset
 * @return Word value read from configuration space
 */
uint16_t pci_config_read_word(uint8_t bus, uint8_t device, 
                             uint8_t function, uint8_t offset);

/**
 * Read a dword (32 bits) from PCI configuration space
 * 
 * @param bus Bus number
 * @param device Device number
 * @param function Function number
 * @param offset Register offset
 * @return Dword value read from configuration space
 */
uint32_t pci_config_read_dword(uint8_t bus, uint8_t device, 
                              uint8_t function, uint8_t offset);

/**
 * Write a byte to PCI configuration space
 * 
 * @param bus Bus number
 * @param device Device number
 * @param function Function number
 * @param offset Register offset
 * @param value Byte value to write
 */
void pci_config_write_byte(uint8_t bus, uint8_t device, 
                          uint8_t function, uint8_t offset, uint8_t value);

/**
 * Write a word (16 bits) to PCI configuration space
 * 
 * @param bus Bus number
 * @param device Device number
 * @param function Function number
 * @param offset Register offset
 * @param value Word value to write
 */
void pci_config_write_word(uint8_t bus, uint8_t device, 
                          uint8_t function, uint8_t offset, uint16_t value);

/**
 * Write a dword (32 bits) to PCI configuration space
 * 
 * @param bus Bus number
 * @param device Device number
 * @param function Function number
 * @param offset Register offset
 * @param value Dword value to write
 */
void pci_config_write_dword(uint8_t bus, uint8_t device, 
                           uint8_t function, uint8_t offset, uint32_t value);

/**
 * Get detailed information about a PCI device
 * 
 * @param bus Bus number
 * @param device Device number
 * @param function Function number
 * @return Structure containing device information
 */
struct pci_device pci_get_device_info(uint8_t bus, uint8_t device, uint8_t function);

/**
 * Enable PCI bus mastering for a device
 * This is required for DMA operations
 * 
 * @param bus Bus number
 * @param device Device number
 * @param function Function number
 */
void pci_enable_bus_mastering(uint8_t bus, uint8_t device, uint8_t function);

/**
 * Find a PCI device with specific class, subclass, and programming interface
 * 
 * @param class_code Class code to search for
 * @param subclass Subclass code to search for
 * @param prog_if Programming interface to search for
 * @param out_bus Pointer to store the bus number
 * @param out_device Pointer to store the device number
 * @param out_function Pointer to store the function number
 * @return true if device found, false otherwise
 */
bool pci_find_device(uint8_t class_code, uint8_t subclass, uint8_t prog_if,
                    uint8_t* out_bus, uint8_t* out_device, uint8_t* out_function);

/**
 * Get the size of a PCI Base Address Register (BAR)
 * 
 * @param bus Bus number
 * @param device Device number
 * @param function Function number
 * @param bar_num BAR number (0-5)
 * @return Size of the BAR in bytes
 */
uint32_t pci_get_bar_size(uint8_t bus, uint8_t device, uint8_t function, uint8_t bar_num);

/**
 * Get the type of a PCI Base Address Register (BAR)
 * 
 * @param bus Bus number
 * @param device Device number
 * @param function Function number
 * @param bar_num BAR number (0-5)
 * @return BAR type (I/O, Memory 32-bit, Memory 64-bit, etc.)
 */
enum pci_bar_type pci_get_bar_type(uint8_t bus, uint8_t device, uint8_t function, uint8_t bar_num);

/**
 * Get the base address of a PCI BAR, masking out the type bits
 * 
 * @param bus Bus number
 * @param device Device number
 * @param function Function number
 * @param bar_num BAR number (0-5)
 * @return Base address of the BAR
 */
uint64_t pci_get_bar_address(uint8_t bus, uint8_t device, uint8_t function, uint8_t bar_num);

/**
 * Enumerate all PCI devices in the system
 * Displays information about each device found
 */
void enumerate_pci_devices();

/**
 * Initialize the PCI subsystem
 */
void init_pci();

#endif /* PCI_H */