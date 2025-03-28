#ifndef PCI_ADMIN_H
#define PCI_ADMIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

/* Admin command flags */
#define ADMIN_FLAG_URGENT            0x01
#define ADMIN_FLAG_NON_BLOCKING      0x02
#define ADMIN_FLAG_PRIVILEGED        0x04

/* Function prototypes */
uint8_t pci_send_admin_command(uint8_t bus, uint8_t device, uint8_t function, 
                             struct pci_admin_cmd* cmd);

uint32_t get_system_time_ms(void);
void cpu_pause(void);

bool pci_admin_reset_device(uint8_t bus, uint8_t device, uint8_t function);

bool pci_admin_get_features(uint8_t bus, uint8_t device, uint8_t function, 
                          uint32_t* features_buffer, uint32_t buffer_size);

bool pci_admin_set_features(uint8_t bus, uint8_t device, uint8_t function, 
                          uint32_t feature_id, uint32_t feature_value);

bool pci_admin_identify_device(uint8_t bus, uint8_t device, uint8_t function, 
                             void* id_buffer, uint32_t buffer_size);

bool pci_admin_get_log_page(uint8_t bus, uint8_t device, uint8_t function, 
                          uint8_t log_id, void* log_buffer, uint32_t buffer_size);

bool pci_admin_send_firmware(uint8_t bus, uint8_t device, uint8_t function,
                           void* firmware_data, uint32_t data_size, uint8_t slot);

bool pci_admin_commit_firmware(uint8_t bus, uint8_t device, uint8_t function, 
                             uint8_t slot, bool activate);

const char* pci_admin_status_string(uint8_t status);

#endif /* PCI_ADMIN_H */