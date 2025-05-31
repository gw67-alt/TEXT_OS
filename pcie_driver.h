#ifndef PCIE_DRIVER_H
#define PCIE_DRIVER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// --- Configuration Constants ---
#define PCIE_CONFIG_ADDRESS 0xCF8
#define PCIE_CONFIG_DATA    0xCFC

// PCIe Configuration Space Offsets
#define PCIE_CONFIG_VENDOR_ID   0x00 // Word
#define PCIE_CONFIG_DEVICE_ID   0x02 // Word
#define PCIE_CONFIG_COMMAND     0x04 // Word
#define PCIE_CONFIG_STATUS      0x06 // Word
#define PCIE_CONFIG_CLASS_REV   0x08 // DWord: [ClassCode(byte)|Subclass(byte)|ProgIF(byte)|RevisionID(byte)]
#define PCIE_CONFIG_HEADER_TYPE 0x0E // Byte
#define PCIE_CONFIG_BAR0        0x10 // DWord
#define PCIE_CONFIG_BAR1        0x14 // DWord
#define PCIE_CONFIG_BAR2        0x18 // DWord
#define PCIE_CONFIG_BAR3        0x1C // DWord
#define PCIE_CONFIG_BAR4        0x20 // DWord
#define PCIE_CONFIG_BAR5        0x24 // DWord

// PCIe Command Register Bits
#define PCIE_CMD_IO_ENABLE     0x0001
#define PCIE_CMD_MEMORY_ENABLE 0x0002
#define PCIE_CMD_BUS_MASTER    0x0004

// Driver Configuration
#define MAX_PCIE_DEVICES 32
#define MAX_RETURNED_READ_VALUES 16
#define MAX_COMMAND_LENGTH 256
#define MAX_WORD_LENGTH 64

// --- Data Structures ---

/**
 * @brief Structure representing a PCIe device
 */
typedef struct {
    bool valid;              // Whether this device entry is valid
    uint8_t bus;            // PCIe bus number
    uint8_t device;         // PCIe device number
    uint8_t function;       // PCIe function number
    uint16_t vendor_id;     // Vendor ID
    uint16_t device_id;     // Device ID
    uint32_t class_code;    // 24-bit class code (class:subclass:prog_if)
    uint32_t bar[6];        // Base Address Registers
} PCIeDevice;

/**
 * @brief Structure representing a parsed driver command
 */
typedef struct {
    bool use_pcie;          // True for PCIe config space, false for memory
    bool is_read;           // True for read operation, false for write
    uint8_t value;          // Value to write (for write operations)
    uint32_t address;       // Memory address (for memory operations)
    uint8_t bus;            // PCIe bus (for PCIe operations)
    uint8_t device;         // PCIe device (for PCIe operations)
    uint8_t function;       // PCIe function (for PCIe operations)
    uint16_t offset;        // PCIe config space offset (for PCIe operations)
} DriverCommand;

// --- Port I/O Function Declarations ---
#ifndef PORT_IO_DEFINED
#define PORT_IO_DEFINED

/**
 * @brief Output 32-bit value to port
 */
static inline void outl(uint16_t port, uint32_t value) { 
    asm volatile("outl %0, %1" : : "a"(value), "Nd"(port)); 
}

/**
 * @brief Output 16-bit value to port
 */
static inline void outw(uint16_t port, uint16_t value) { 
    asm volatile("outw %0, %1" : : "a"(value), "Nd"(port)); 
}

/**
 * @brief Input 32-bit value from port
 */
static inline uint32_t inl(uint16_t port) { 
    uint32_t v; 
    asm volatile("inl %1, %0" : "=a"(v) : "Nd"(port)); 
    return v; 
}

/**
 * @brief Input 16-bit value from port
 */
static inline uint16_t inw(uint16_t port) { 
    uint16_t v; 
    asm volatile("inw %1, %0" : "=a"(v) : "Nd"(port)); 
    return v; 
}


#endif // PORT_IO_DEFINED

// --- PCIe Configuration Space Access Functions ---

/**
 * @brief Read 32-bit value from PCIe configuration space
 */
uint32_t pcie_config_read32(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset);

/**
 * @brief Read 16-bit value from PCIe configuration space
 */
uint16_t pcie_config_read16(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset);

/**
 * @brief Read 8-bit value from PCIe configuration space
 */
uint8_t pcie_config_read8(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset);

/**
 * @brief Write 32-bit value to PCIe configuration space
 */
void pcie_config_write32(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint32_t value);

/**
 * @brief Write 16-bit value to PCIe configuration space
 */
void pcie_config_write16(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint16_t value);

/**
 * @brief Write 8-bit value to PCIe configuration space
 */
void pcie_config_write8(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint8_t value);

// --- Device Management Functions ---

/**
 * @brief Check if a PCIe device exists at the specified location
 */
bool pcie_device_exists(uint8_t bus, uint8_t device, uint8_t function);

/**
 * @brief Print information about a PCIe device
 */
void print_pcie_device_info(const PCIeDevice* dev);

/**
 * @brief Scan for PCIe devices and populate the device list
 */
void pcie_scan_devices(void);

// --- Driver Core Functions ---

/**
 * @brief Read 8-bit value from memory address
 */
uint8_t driver_memory_read(uint32_t address);

/**
 * @brief Write 8-bit value to memory address
 */
void driver_memory_write(uint32_t address, uint8_t value);

/**
 * @brief Read 8-bit value from PCIe configuration space
 */
uint8_t driver_pcie_read(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset);

/**
 * @brief Write 8-bit value to PCIe configuration space
 */
void driver_pcie_write(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint8_t value);

// --- Command Parsing and Execution ---

/**
 * @brief Parse a driver command string into a DriverCommand structure
 * @param input The input command string
 * @param cmd Pointer to DriverCommand structure to populate
 * @return true if parsing was successful, false otherwise
 */
bool parse_driver_command(const char* input, DriverCommand* cmd);

/**
 * @brief Execute driver script from buffer
 * @param script_buffer Buffer containing the script to execute
 */
void execute_driver_script_from_buffer(const char* script_buffer);

/**
 * @brief Process driver configuration commands
 * @param input_command_string The command string to process
 * @param overall_parsing_success Pointer to store overall parsing success status
 * @param out_read_values_array Array to store read values
 * @param buffer_capacity Capacity of the read values array
 * @param num_values_read_actual Pointer to store actual number of values read
 */
void driver_cfg(char* input_command_string, 
                bool* overall_parsing_success, 
                uint8_t* out_read_values_array, 
                int buffer_capacity, 
                int* num_values_read_actual);

/**
 * @brief Interactive driver command interface
 */
void cmd_driver(void);

// --- Utility Functions ---

/**
 * @brief Convert hex character to numeric value
 * @param c Hex character ('0'-'9', 'a'-'f', 'A'-'F')
 * @return Numeric value (0-15) or 0xFF for invalid input
 */
uint8_t hex_char_to_value(char c);

/**
 * @brief Convert hex string to 32-bit unsigned integer
 * @param hex_str Hex string (with or without "0x" prefix)
 * @return Converted value
 */
uint32_t hex_string_to_uint32(const char* hex_str);

/**
 * @brief Convert hex string to 8-bit unsigned integer
 * @param hex_str Hex string (with or without "0x" prefix)
 * @return Converted value
 */
uint8_t hex_string_to_uint8(const char* hex_str);

// --- Initialization ---

/**
 * @brief Initialize the PCIe driver system
 */
void init_pcie_driver(void);

// --- Global Variables Access Functions ---
// Note: Global variables are declared static in the implementation file
// Use these functions to access them from other modules

/**
 * @brief Get pointer to detected devices array
 * @return Pointer to the detected devices array
 */
const PCIeDevice* get_detected_devices(void);

/**
 * @brief Get the current device count
 * @return Number of detected devices
 */
int get_device_count(void);

#endif // PCIE_DRIVER_H
