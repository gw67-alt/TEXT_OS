#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "io.h"
#include "pci.h"
#include "stdio.h"








#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "io.h"
#include "pci.h"
#include "stdio.h"

/* 
 * PCI Admin Command Module
 * Extends the PCI subsystem to support sending admin commands to PCI devices
 */

/* Admin command structure */
struct pci_admin_cmd {
    uint8_t cmd_opcode;      /* Command opcode */
    uint8_t flags;           /* Command flags */
    uint32_t data_len;       /* Data length in bytes */
    void* data_ptr;          /* Pointer to command data */
    uint32_t metadata_len;   /* Metadata length in bytes */
    void* metadata_ptr;      /* Pointer to command metadata */
    uint32_t timeout_ms;     /* Command timeout in milliseconds */
};

/* Admin command status codes */
#define PCI_ADMIN_SUCCESS            0x00
#define PCI_ADMIN_INVALID_OPCODE     0x01
#define PCI_ADMIN_INVALID_PARAM      0x02
#define PCI_ADMIN_TIMEOUT            0x03
#define PCI_ADMIN_DEVICE_ERROR       0x04
#define PCI_ADMIN_ACCESS_DENIED      0x05
#define PCI_ADMIN_RESOURCE_ERROR     0x06

/* Common admin command opcodes */
#define ADMIN_CMD_GET_LOG_PAGE       0x02
#define ADMIN_CMD_IDENTIFY           0x06
#define ADMIN_CMD_SET_FEATURES       0x09
#define ADMIN_CMD_GET_FEATURES       0x0A
#define ADMIN_CMD_RESET              0x0F
#define ADMIN_CMD_FIRMWARE_COMMIT    0x10
#define ADMIN_CMD_FIRMWARE_IMAGE     0x11

/* Maximum admin command timeout in milliseconds */
#define MAX_ADMIN_TIMEOUT            30000

/* Admin command flags */
#define ADMIN_FLAG_URGENT            0x01
#define ADMIN_FLAG_NON_BLOCKING      0x02
#define ADMIN_FLAG_PRIVILEGED        0x04

/* Registers used for admin commands */
#define PCI_ADMIN_COMMAND_REG        0x40    /* Admin command register (vendor-specific) */
#define PCI_ADMIN_STATUS_REG         0x44    /* Admin status register (vendor-specific) */
#define PCI_ADMIN_DATA_REG           0x48    /* Admin data register (vendor-specific) */


/* Get system time in milliseconds - implementation depends on your OS timer */
uint32_t get_system_time_ms(void) {
    /* This is just a placeholder - implement based on your OS timer */
    /* Example using the PIT (Programmable Interval Timer) counter */
    uint32_t ms_counter = 0;
    
    /* In a real implementation, you would read from your system timer */
    /* This is just a stub that increments by 1 each call */
    return (uint32_t)ms_counter++;
}


/* 
 * Send an admin command to a PCI device
 * Returns: status code from the command execution
 */
uint8_t pci_send_admin_command(uint8_t bus, uint8_t device, uint8_t function, 
                             struct pci_admin_cmd* cmd) {
    uint32_t cmd_word, status;
    uint32_t start_time, current_time;
    uint8_t result = PCI_ADMIN_SUCCESS;
    uint32_t command_reg = PCI_ADMIN_COMMAND_REG;
    uint32_t status_reg = PCI_ADMIN_STATUS_REG;
    uint32_t data_reg = PCI_ADMIN_DATA_REG;
    
    /* Check if the device exists */
    uint16_t vendor_id = pci_config_read_word(bus, device, function, 0x00);
    if (vendor_id == 0xFFFF) {
        printf("PCI Admin: No device at %02X:%02X.%X\n", bus, device, function);
        return PCI_ADMIN_DEVICE_ERROR;
    }
    
    /* Adjust timeout if needed */
    if (cmd->timeout_ms == 0 || cmd->timeout_ms > MAX_ADMIN_TIMEOUT) {
        cmd->timeout_ms = MAX_ADMIN_TIMEOUT;
    }
    
    /* Prepare command word: 
     * bits 0-7: opcode
     * bits 8-15: flags
     * bits 16-31: reserved (set to 0)
     */
    cmd_word = ((uint32_t)cmd->cmd_opcode) | ((uint32_t)cmd->flags << 8);
    
    /* If there's data to send, write it to the data register */
    if (cmd->data_ptr != NULL && cmd->data_len > 0) {
        uint32_t data_value = 0;
        uint8_t* data_bytes = (uint8_t*)cmd->data_ptr;
        
        /* Write data length first */
        pci_config_write_dword(bus, device, function, data_reg, cmd->data_len);
        
        /* Write data in 4-byte chunks */
        for (uint32_t i = 0; i < cmd->data_len; i += 4) {
            /* Prepare a dword from data bytes */
            data_value = 0;
            for (uint8_t j = 0; j < 4 && (i + j) < cmd->data_len; j++) {
                data_value |= ((uint32_t)data_bytes[i + j]) << (j * 8);
            }
            
            /* Write to the data register */
            pci_config_write_dword(bus, device, function, data_reg + 4 + i, data_value);
        }
    }
    
    /* Write metadata if present */
    if (cmd->metadata_ptr != NULL && cmd->metadata_len > 0) {
        uint32_t meta_value = 0;
        uint8_t* meta_bytes = (uint8_t*)cmd->metadata_ptr;
        
        /* Write metadata length */
        pci_config_write_dword(bus, device, function, data_reg + 0x100, cmd->metadata_len);
        
        /* Write metadata in 4-byte chunks */
        for (uint32_t i = 0; i < cmd->metadata_len; i += 4) {
            /* Prepare a dword from metadata bytes */
            meta_value = 0;
            for (uint8_t j = 0; j < 4 && (i + j) < cmd->metadata_len; j++) {
                meta_value |= ((uint32_t)meta_bytes[i + j]) << (j * 8);
            }
            
            /* Write to the metadata section of data register */
            pci_config_write_dword(bus, device, function, data_reg + 0x104 + i, meta_value);
        }
    }
    
    /* Send the command by writing to the command register */
    pci_config_write_dword(bus, device, function, command_reg, cmd_word);
    
    /* For non-blocking commands, return immediately */
    if (cmd->flags & ADMIN_FLAG_NON_BLOCKING) {
        return PCI_ADMIN_SUCCESS;
    }
    
    /* Wait for command completion or timeout */
    start_time = (uint32_t)get_system_time_ms(); /* Assume this function exists in your OS */
    
    while (1) {
        /* Read status register */
        status = pci_config_read_dword(bus, device, function, status_reg);
        
        /* Check if command completed (bit 0 set) */
        if (status & 0x1) {
            /* Extract result code from bits 8-15 */
            result = (status >> 8) & 0xFF;
            break;
        }
        
        /* Check for timeout */
        current_time = (uint32_t)get_system_time_ms();
        if (current_time - start_time >= cmd->timeout_ms) {
            result = PCI_ADMIN_TIMEOUT;
            break;
        }
        
        /* Small delay to avoid hammering the bus */
        cpu_pause(); /* Assume this is defined as an appropriate delay for your architecture */
    }
    
    /* If successful and there's data to read back, read from data register */
    if (result == PCI_ADMIN_SUCCESS && cmd->data_ptr != NULL && cmd->data_len > 0) {
        uint32_t data_value;
        uint8_t* data_bytes = (uint8_t*)cmd->data_ptr;
        uint32_t response_len = pci_config_read_dword(bus, device, function, data_reg);
        
        /* Limit to requested size */
        if (response_len > cmd->data_len) {
            response_len = cmd->data_len;
        }
        
        /* Read response data */
        for (uint32_t i = 0; i < response_len; i += 4) {
            data_value = pci_config_read_dword(bus, device, function, data_reg + 4 + i);
            
            /* Extract individual bytes */
            for (uint8_t j = 0; j < 4 && (i + j) < response_len; j++) {
                data_bytes[i + j] = (data_value >> (j * 8)) & 0xFF;
            }
        }
    }
    
    return result;
}



/* CPU pause/delay function */
inline void cpu_pause(void) {
    /* Use architecture-specific pause instruction */
    #if defined(__i386__) || defined(__x86_64__)
        __asm__ volatile("pause" ::: "memory");
    #else
        /* For other architectures, implement an appropriate small delay */
        for (volatile int i = 0; i < 1000; i++) { }
    #endif
}

/*
 * Reset a PCI device using admin command
 * Returns true if successful, false if failed
 */
bool pci_admin_reset_device(uint8_t bus, uint8_t device, uint8_t function) {
    struct pci_admin_cmd cmd;
    uint8_t result;
    
    /* Setup command structure */
    cmd.cmd_opcode = ADMIN_CMD_RESET;
    cmd.flags = ADMIN_FLAG_PRIVILEGED;
    cmd.data_len = 0;
    cmd.data_ptr = NULL;
    cmd.metadata_len = 0;
    cmd.metadata_ptr = NULL;
    cmd.timeout_ms = 5000; /* 5 seconds timeout */
    
    printf("Sending reset command to PCI device %02X:%02X.%X...\n", 
           bus, device, function);
    
    /* Send command */
    result = pci_send_admin_command(bus, device, function, &cmd);
    
    if (result == PCI_ADMIN_SUCCESS) {
        printf("Device reset successful.\n");
        return true;
    } else {
        printf("Device reset failed with status: 0x%02X\n", result);
        return false;
    }
}

/*
 * Get device features using admin command
 * Returns true if successful, false if failed
 */
bool pci_admin_get_features(uint8_t bus, uint8_t device, uint8_t function, 
                          uint32_t* features_buffer, uint32_t buffer_size) {
    struct pci_admin_cmd cmd;
    uint8_t result;
    
    if (features_buffer == NULL || buffer_size == 0) {
        return false;
    }
    
    /* Setup command structure */
    cmd.cmd_opcode = ADMIN_CMD_GET_FEATURES;
    cmd.flags = 0;
    cmd.data_len = buffer_size;
    cmd.data_ptr = features_buffer;
    cmd.metadata_len = 0;
    cmd.metadata_ptr = NULL;
    cmd.timeout_ms = 1000; /* 1 second timeout */
    
    /* Send command */
    result = pci_send_admin_command(bus, device, function, &cmd);
    
    return (result == PCI_ADMIN_SUCCESS);
}

/*
 * Set device features using admin command
 * Returns true if successful, false if failed
 */
bool pci_admin_set_features(uint8_t bus, uint8_t device, uint8_t function, 
                          uint32_t feature_id, uint32_t feature_value) {
    struct pci_admin_cmd cmd;
    uint8_t result;
    uint32_t data[2];
    
    /* Setup data for the command */
    data[0] = feature_id;
    data[1] = feature_value;
    
    /* Setup command structure */
    cmd.cmd_opcode = ADMIN_CMD_SET_FEATURES;
    cmd.flags = ADMIN_FLAG_PRIVILEGED;
    cmd.data_len = sizeof(data);
    cmd.data_ptr = data;
    cmd.metadata_len = 0;
    cmd.metadata_ptr = NULL;
    cmd.timeout_ms = 1000; /* 1 second timeout */
    
    /* Send command */
    result = pci_send_admin_command(bus, device, function, &cmd);
    
    return (result == PCI_ADMIN_SUCCESS);
}

/*
 * Get device identification information
 * Returns true if successful, false if failed
 */
bool pci_admin_identify_device(uint8_t bus, uint8_t device, uint8_t function, 
                             void* id_buffer, uint32_t buffer_size) {
    struct pci_admin_cmd cmd;
    uint8_t result;
    
    if (id_buffer == NULL || buffer_size < 256) {
        return false;
    }
    
    /* Setup command structure */
    cmd.cmd_opcode = ADMIN_CMD_IDENTIFY;
    cmd.flags = 0;
    cmd.data_len = buffer_size;
    cmd.data_ptr = id_buffer;
    cmd.metadata_len = 0;
    cmd.metadata_ptr = NULL;
    cmd.timeout_ms = 2000; /* 2 second timeout */
    
    /* Send command */
    result = pci_send_admin_command(bus, device, function, &cmd);
    
    return (result == PCI_ADMIN_SUCCESS);
}

/*
 * Get device log page
 * Returns true if successful, false if failed
 */
bool pci_admin_get_log_page(uint8_t bus, uint8_t device, uint8_t function, 
                          uint8_t log_id, void* log_buffer, uint32_t buffer_size) {
    struct pci_admin_cmd cmd;
    uint8_t result;
    uint32_t metadata = log_id;
    
    if (log_buffer == NULL || buffer_size == 0) {
        return false;
    }
    
    /* Setup command structure */
    cmd.cmd_opcode = ADMIN_CMD_GET_LOG_PAGE;
    cmd.flags = 0;
    cmd.data_len = buffer_size;
    cmd.data_ptr = log_buffer;
    cmd.metadata_len = sizeof(metadata);
    cmd.metadata_ptr = &metadata;
    cmd.timeout_ms = 3000; /* 3 second timeout */
    
    /* Send command */
    result = pci_send_admin_command(bus, device, function, &cmd);
    
    return (result == PCI_ADMIN_SUCCESS);
}

/* 
 * Send firmware image to device
 * Returns true if successful, false if failed
 */
bool pci_admin_send_firmware(uint8_t bus, uint8_t device, uint8_t function,
                           void* firmware_data, uint32_t data_size, uint8_t slot) {
    struct pci_admin_cmd cmd;
    uint8_t result;
    uint32_t metadata = slot; /* Firmware slot number */
    
    if (firmware_data == NULL || data_size == 0) {
        return false;
    }
    
    printf("Uploading firmware to device %02X:%02X.%X (slot %d, %d bytes)...\n",
           bus, device, function, slot, data_size);
    
    /* Setup command structure */
    cmd.cmd_opcode = ADMIN_CMD_FIRMWARE_IMAGE;
    cmd.flags = ADMIN_FLAG_PRIVILEGED;
    cmd.data_len = data_size;
    cmd.data_ptr = firmware_data;
    cmd.metadata_len = sizeof(metadata);
    cmd.metadata_ptr = &metadata;
    cmd.timeout_ms = 30000; /* 30 second timeout for firmware upload */
    
    /* Send command */
    result = pci_send_admin_command(bus, device, function, &cmd);
    
    if (result == PCI_ADMIN_SUCCESS) {
        printf("Firmware upload successful.\n");
        return true;
    } else {
        printf("Firmware upload failed with status: 0x%02X\n", result);
        return false;
    }
}

/*
 * Commit firmware
 * Returns true if successful, false if failed
 */
bool pci_admin_commit_firmware(uint8_t bus, uint8_t device, uint8_t function, 
                             uint8_t slot, bool activate) {
    struct pci_admin_cmd cmd;
    uint8_t result;
    uint32_t data[2];
    
    /* Setup data for the command */
    data[0] = slot;
    data[1] = activate ? 1 : 0;
    
    printf("Committing firmware on device %02X:%02X.%X (slot %d, activate %d)...\n",
           bus, device, function, slot, activate);
    
    /* Setup command structure */
    cmd.cmd_opcode = ADMIN_CMD_FIRMWARE_COMMIT;
    cmd.flags = ADMIN_FLAG_PRIVILEGED;
    cmd.data_len = sizeof(data);
    cmd.data_ptr = data;
    cmd.metadata_len = 0;
    cmd.metadata_ptr = NULL;
    cmd.timeout_ms = 5000; /* 5 second timeout */
    
    /* Send command */
    result = pci_send_admin_command(bus, device, function, &cmd);
    
    if (result == PCI_ADMIN_SUCCESS) {
        printf("Firmware commit successful.\n");
        return true;
    } else {
        printf("Firmware commit failed with status: 0x%02X\n", result);
        return false;
    }
}

/* 
 * Print admin command status code as string
 */
const char* pci_admin_status_string(uint8_t status) {
    switch (status) {
        case PCI_ADMIN_SUCCESS:
            return "Success";
        case PCI_ADMIN_INVALID_OPCODE:
            return "Invalid Opcode";
        case PCI_ADMIN_INVALID_PARAM:
            return "Invalid Parameter";
        case PCI_ADMIN_TIMEOUT:
            return "Timeout";
        case PCI_ADMIN_DEVICE_ERROR:
            return "Device Error";
        case PCI_ADMIN_ACCESS_DENIED:
            return "Access Denied";
        case PCI_ADMIN_RESOURCE_ERROR:
            return "Resource Error";
        default:
            return "Unknown Error";
    }
}











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
char* get_device_name(uint16_t vendor_id, uint16_t device_id, 
                           uint8_t class_code, uint8_t subclass, uint8_t prog_if);
char* get_class_name(uint8_t class_code, uint8_t subclass, uint8_t prog_if);

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
char* get_device_name(uint16_t vendor_id, uint16_t device_id, 
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
    return (char)get_class_name(class_code, subclass, prog_if);
}

/* Get generic class name based on class/subclass codes */
char* get_class_name(uint8_t class_code, uint8_t subclass, uint8_t prog_if) {
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








void test_pci_admin_commands(void) {
    uint8_t bus, device, function;
    bool result;
    uint32_t features[16] = {0}; // Buffer for device features
    uint8_t id_data[512] = {0};  // Buffer for device identification data
    uint8_t log_data[1024] = {0}; // Buffer for log data
    
    printf("Testing PCI Admin Command Module\n");
    printf("--------------------------------\n");
    
    // For this example, let's look for a SATA AHCI controller (class 01h, subclass 06h, prog_if 01h)
    if (pci_find_device(0x01, 0x06, 0x01, &bus, &device, &function)) {
        printf("Found AHCI controller at %02X:%02X.%X\n", bus, device, function);
        
        // 1. Get device identification
        printf("\n1. Getting device identification...\n");
        result = pci_admin_identify_device(bus, device, function, id_data, sizeof(id_data));
        if (result) {
            printf("Device identification successful\n");
            // In a real implementation, you would parse and display the id_data
            printf("Device Model: %.*s\n", 40, (char*)id_data + 24);  // Assuming model name is at offset 24
            printf("Firmware: %.*s\n", 8, (char*)id_data + 64);      // Assuming firmware version is at offset 64
        } else {
            printf("Failed to get device identification\n");
        }
        
        // 2. Get device features
        printf("\n2. Getting device features...\n");
        result = pci_admin_get_features(bus, device, function, features, sizeof(features));
        if (result) {
            printf("Device features retrieved successfully\n");
            printf("Feature 0: 0x%08X\n", features[0]);
            printf("Feature 1: 0x%08X\n", features[1]);
            printf("Feature 2: 0x%08X\n", features[2]);
            printf("Feature 3: 0x%08X\n", features[3]);
        } else {
            printf("Failed to get device features\n");
        }
        
        // 3. Set a device feature
        printf("\n3. Setting device feature...\n");
        uint32_t feature_id = 0x01;   // Example feature ID (power management)
        uint32_t feature_value = 0x03; // Example value (e.g., enable power saving mode)
        result = pci_admin_set_features(bus, device, function, feature_id, feature_value);
        if (result) {
            printf("Successfully set feature 0x%02X to value 0x%02X\n", feature_id, feature_value);
        } else {
            printf("Failed to set device feature\n");
        }
        
        // 4. Get device log page
        printf("\n4. Getting device log page...\n");
        uint8_t log_id = 0x01;  // Example log ID (error log)
        result = pci_admin_get_log_page(bus, device, function, log_id, log_data, sizeof(log_data));
        if (result) {
            printf("Successfully retrieved log page 0x%02X\n", log_id);
            // In a real implementation, you would parse and display the log_data
            printf("Log entries: %d\n", *(uint32_t*)log_data);
            printf("First log entry timestamp: 0x%08X\n", *(uint32_t*)(log_data + 4));
        } else {
            printf("Failed to get device log page\n");
        }
        
        // 5. Reset the device (commented out for safety)
        /*
        printf("\n5. Resetting the device...\n");
        result = pci_admin_reset_device(bus, device, function);
        if (result) {
            printf("Device reset successful\n");
        } else {
            printf("Device reset failed\n");
        }
        */
        
        printf("\nPCI Admin Command test completed\n");
    } else {
        printf("No AHCI controller found for testing\n");
        
        // Try to find any PCI device for testing
        for (bus = 0; bus < 256; bus++) {
            for (device = 0; device < 32; device++) {
                for (function = 0; function < 8; function++) {
                    uint16_t vendor_id = pci_config_read_word(bus, device, function, 0x00);
                    if (vendor_id != 0xFFFF) {
                        printf("Found PCI device at %02X:%02X.%X (vendor 0x%04X)\n", 
                               bus, device, function, vendor_id);
                        
                        // Try to get device features
                        printf("Trying to get device features...\n");
                        result = pci_admin_get_features(bus, device, function, features, sizeof(features));
                        if (result) {
                            printf("Device features retrieved successfully\n");
                            return;
                        } else {
                            printf("Failed to get device features\n");
                        }
                        
                        return;
                    }
                }
            }
        }
        
        printf("No suitable PCI devices found for testing\n");
    }
}