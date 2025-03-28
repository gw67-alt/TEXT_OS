/**
 * TPM Data Storage Implementation for Bare Metal x86
 * 
 * This implementation provides low-level functionality for storing data
 * in a TPM 2.0 device using direct hardware access without relying on
 * an operating system's abstractions.
 */

 #include <stdint.h>
 #include <stdbool.h>
 #include "stdio.h"
 #include "io.h"
 
 // TPM 2.0 register addresses (assuming memory-mapped I/O)
 // These addresses may vary depending on the hardware implementation










/**
 * TPM Base Address Detection for Bare Metal x86
 * 
 * This implementation provides functionality to automatically detect
 * the memory-mapped I/O base address of a TPM 2.0 device on x86 hardware.
 */
 #include <stdint.h>
 #include <stdbool.h>
 #include "stdio.h"
 
 // PCI Configuration Space registers
 #define PCI_CONFIG_ADDRESS     0xCF8
 #define PCI_CONFIG_DATA        0xCFC
 
 // PCI registers
 #define PCI_VENDOR_ID          0x00
 #define PCI_DEVICE_ID          0x02
 #define PCI_COMMAND            0x04
 #define PCI_CLASS_REV          0x08
 #define PCI_BAR0               0x10
 #define PCI_BAR1               0x14
 
 // TPM PCI Device IDs
 #define TPM_PCI_VENDOR_ID_IFX         0x1279  // Infineon
 #define TPM_PCI_VENDOR_ID_INTEL       0x8086  // Intel
 #define TPM_PCI_VENDOR_ID_NUVOTON     0x1050  // Nuvoton
 #define TPM_PCI_VENDOR_ID_IBM         0x1014  // IBM
 #define TPM_PCI_VENDOR_ID_WINBOND     0x1050  // Winbond (same as Nuvoton)
 #define TPM_PCI_VENDOR_ID_STM         0x104A  // STMicroelectronics
 #define TPM_PCI_VENDOR_ID_NATIONTECH  0x1B4E  // Nationtech
 
 // TPM Interface types
 #define TPM_INTERFACE_TIS      0
 #define TPM_INTERFACE_FIFO     1
 #define TPM_INTERFACE_CRB      2
 
 // ACPI related information
 #define ACPI_RSDP_SIGNATURE    "RSD PTR "
 #define ACPI_RSDP_SIG_SIZE     8
 #define ACPI_RSDT_SIGNATURE    "RSDT"
 #define ACPI_XSDT_SIGNATURE    "XSDT"
 #define ACPI_TCPA_SIGNATURE    "TCPA"  // For TPM 1.2
 #define ACPI_TPM2_SIGNATURE    "TPM2"  // For TPM 2.0
 
 // Default TPM Base Addresses to try if detection fails
 #define TPM_DEFAULT_BASE_ADDR  0xFED40000  // Most common base address
 #define TPM_ALT_BASE_ADDR_1    0xFED45000  // Alternative address sometimes used
 #define TPM_ALT_BASE_ADDR_2    0xFED4A000  // Another alternative address
 
 // Function declarations
 uint32_t detect_tpm_base_address(void);
 uint32_t find_tpm_pci_device(void);
 uint32_t find_tpm_acpi_device(void);
 uint32_t test_tpm_address(uint32_t addr);
 uint32_t read_pci_config(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
 void write_pci_config(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value);
 
 // Memory-mapped I/O functions (same as in the original code)
 static inline void mmio_write32(uint32_t addr, uint32_t val) {
     *((volatile uint32_t*)addr) = val;
 }
 
 static inline uint32_t mmio_read32(uint32_t addr) {
     return *((volatile uint32_t*)addr);
 }
 
 static inline void mmio_write8(uint32_t addr, uint8_t val) {
     *((volatile uint8_t*)addr) = val;
 }
 
 static inline uint8_t mmio_read8(uint32_t addr) {
     return *((volatile uint8_t*)addr);
 }
 
 // Access PCI Configuration Space
 uint32_t read_pci_config(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
     uint32_t address = (uint32_t)((bus << 16) | (device << 11) | (function << 8) | 
                                   (offset & 0xFC) | 0x80000000);
     outl(PCI_CONFIG_ADDRESS, address);
     return inl(PCI_CONFIG_DATA);
 }
 
 void write_pci_config(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value) {
     uint32_t address = (uint32_t)((bus << 16) | (device << 11) | (function << 8) | 
                                   (offset & 0xFC) | 0x80000000);
     outl(PCI_CONFIG_ADDRESS, address);
     outl(PCI_CONFIG_DATA, value);
 }
 
 // Check if a potential address responds like a TPM device
 uint32_t test_tpm_address(uint32_t addr) {
     // Test if address is accessible by trying to read from it
     uint8_t access;
     
     // First check: Try to read TPM_ACCESS register
     // If not accessible, this will likely cause a hardware exception which should be caught
     // by the calling code (implement protection as appropriate for your environment)
     
     // Adding simple validation check
     if (addr < 0xF0000000 || addr > 0xFFFFFFFF) {
         return 0; // Invalid address range for memory-mapped TPM
     }
     
     // First safely check if we can read from the address
     // This is a simple check, but a real implementation would need more robust protection

    access = mmio_read8(addr);
         
    // If we can read, let's check if it looks like a TPM device
    // Most TPM devices have TPM_ACCESS_VALID bit (0x80) set in the access register
    if (access & 0x80) {
        // Additional validation: try to read the TIS interface ID register
        uint32_t interfaceId = mmio_read32(addr + 0x30);
             
        // Check if interface ID has reasonable values
        // TPM 2.0 devices typically have interface ID with specific bits set
        if ((interfaceId & 0xFFFF0000) != 0) {
            return addr; // Looks like a valid TPM device
            }
             
        // If interface ID check failed, we might still have a TPM
        // but let's mark it as lower confidence
        return addr;
        }
        return 0; // Not a valid TPM

     } 
 
 // Scan PCI bus for TPM devices
 uint32_t find_tpm_pci_device(void) {
     uint16_t vendor_id, device_id;
     uint32_t class_rev, bar0;
     uint32_t address = 0;
     
     // Scan PCI bus for devices
     for (uint8_t bus = 0; bus < 256; bus++) {
         for (uint8_t device = 0; device < 32; device++) {
             for (uint8_t function = 0; function < 8; function++) {
                 // Read Vendor ID
                 uint32_t vendor = read_pci_config(bus, device, function, PCI_VENDOR_ID);
                 vendor_id = vendor & 0xFFFF;
                 device_id = (vendor >> 16) & 0xFFFF;
                 
                 // Check if valid device exists at this location
                 if (vendor_id == 0xFFFF) {
                     continue; // No device here
                 }
                 
                 // Check for known TPM vendors
                 bool is_tpm_vendor = false;
                 switch (vendor_id) {
                     case TPM_PCI_VENDOR_ID_IFX:
                     case TPM_PCI_VENDOR_ID_INTEL:
                     case TPM_PCI_VENDOR_ID_NUVOTON:
                     case TPM_PCI_VENDOR_ID_IBM:
                     case TPM_PCI_VENDOR_ID_STM:
                     case TPM_PCI_VENDOR_ID_NATIONTECH:
                         is_tpm_vendor = true;
                         break;
                     default:
                         // Read class code to check if it's a TPM device
                         class_rev = read_pci_config(bus, device, function, PCI_CLASS_REV);
                         uint8_t class_code = (class_rev >> 24) & 0xFF;
                         uint8_t subclass = (class_rev >> 16) & 0xFF;
                         
                         // TPM devices often have class code 0x0C (Serial bus controller)
                         // and subclass 0x05 (TPM)
                         if (class_code == 0x0C && subclass == 0x05) {
                             is_tpm_vendor = true;
                         }
                         break;
                 }
                 
                 if (is_tpm_vendor) {
                     // We found a potential TPM device, get its base address
                     printf("Found potential TPM device: Vendor=0x%04X, Device=0x%04X at bus %d, device %d, function %d\n",
                            vendor_id, device_id, bus, device, function);
                     
                     // Check if memory-mapped I/O is enabled
                     uint16_t command = read_pci_config(bus, device, function, PCI_COMMAND) & 0xFFFF;
                     if (!(command & 0x02)) {
                         // Memory space not enabled, try to enable it
                         write_pci_config(bus, device, function, PCI_COMMAND, command | 0x02);
                     }
                     
                     // Read BAR0 which usually contains the TPM base address
                     bar0 = read_pci_config(bus, device, function, PCI_BAR0);
                     
                     // Check if BAR0 is memory-mapped (bit 0 clear)
                     if (!(bar0 & 0x01)) {
                         // Memory-mapped BAR, get the address (mask off the low bits)
                         address = bar0 & 0xFFFFFFF0;
                         
                         // Test if this address actually responds like a TPM
                         if (test_tpm_address(address)) {
                             return address;
                         }
                     }
                     
                     // Also check BAR1 as some TPMs use it
                     uint32_t bar1 = read_pci_config(bus, device, function, PCI_BAR1);
                     if (!(bar1 & 0x01)) {
                         address = bar1 & 0xFFFFFFF0;
                         if (test_tpm_address(address)) {
                             return address;
                         }
                     }
                 }
             }
         }
     }
     
     return 0; // No TPM PCI device found
 }
 
 // Find ACPI RSDP (Root System Description Pointer)
 uint64_t find_acpi_rsdp(void) {
     uint64_t rsdp_addr = 0;
     uint8_t* search_addr;
     
     // First search the EBDA (Extended BIOS Data Area)
     uint16_t* ebda_ptr = (uint16_t*)0x40E;
     uint32_t ebda_addr = ((uint32_t)*ebda_ptr) << 4;
     
     // Search first 1KB of EBDA
     if (ebda_addr > 0x80000 && ebda_addr < 0xA0000) {
         for (search_addr = (uint8_t*)ebda_addr; search_addr < (uint8_t*)(ebda_addr + 1024); search_addr += 16) {
             if (memcmp(search_addr, ACPI_RSDP_SIGNATURE, ACPI_RSDP_SIG_SIZE) == 0) {
                 return (uint64_t)((uintptr_t)search_addr);
             }
         }
     }
     
     // Then search the BIOS memory area between 0xE0000 and 0xFFFFF
     for (search_addr = (uint8_t*)0xE0000; search_addr < (uint8_t*)0xFFFFF; search_addr += 16) {
         if (memcmp(search_addr, ACPI_RSDP_SIGNATURE, ACPI_RSDP_SIG_SIZE) == 0) {
             return (uint64_t)((uintptr_t)search_addr);
         }
     }
     
     return 0; // RSDP not found
 }
 
 // Validate checksum of an ACPI table
 bool validate_acpi_checksum(uint8_t* table, uint32_t length) {
     uint8_t sum = 0;
     for (uint32_t i = 0; i < length; i++) {
         sum += table[i];
     }
     return (sum == 0);
 }
 
 // Find TPM through ACPI tables
 uint32_t find_tpm_acpi_device(void) {
     uint64_t rsdp_addr = find_acpi_rsdp();
     if (!rsdp_addr) {
         printf("ACPI RSDP not found\n");
         return 0;
     }
     
     printf("Found ACPI RSDP at 0x%llX\n", rsdp_addr);
     
     // Check ACPI version (RSDP structure differs based on version)
     uint8_t revision = *((uint8_t*)(rsdp_addr + 15));
     uint32_t rsdt_addr;
     uint64_t xsdt_addr;
     
     if (revision == 0) {
         // ACPI 1.0 - use RSDT
         rsdt_addr = *((uint32_t*)(rsdp_addr + 16));
         if (!rsdt_addr) {
             return 0;
         }
         
         // Validate RSDT signature
         if (memcmp((void*)rsdt_addr, ACPI_RSDT_SIGNATURE, 4) != 0) {
             printf("Invalid RSDT signature\n");
             return 0;
         }
         
         // Get table length and entries
         uint32_t rsdt_length = *((uint32_t*)(rsdt_addr + 4));
         uint32_t entries = (rsdt_length - 36) / 4; // 36 is the header size
         
         // Search for TPM table
         for (uint32_t i = 0; i < entries; i++) {
             uint32_t table_addr = *((uint32_t*)(rsdt_addr + 36 + i * 4));
             
             // Check if this is a TPM table
             if (memcmp((void*)table_addr, ACPI_TCPA_SIGNATURE, 4) == 0 ||
                 memcmp((void*)table_addr, ACPI_TPM2_SIGNATURE, 4) == 0) {
                 
                 // Found a TPM table, extract base address
                 // For TPM 2.0 (TPM2 table), the base address is typically at offset 48
                 if (memcmp((void*)table_addr, ACPI_TPM2_SIGNATURE, 4) == 0) {
                     // TPM 2.0 table - extract control area address
                     uint32_t tpm_base = *((uint32_t*)(table_addr + 48));
                     if (test_tpm_address(tpm_base)) {
                         return tpm_base;
                     }
                 } 
                 // For TPM 1.2 (TCPA table), the base address is at a different offset
                 else if (memcmp((void*)table_addr, ACPI_TCPA_SIGNATURE, 4) == 0) {
                     // TPM 1.2 table - extract address
                     uint32_t tpm_base = *((uint32_t*)(table_addr + 40));
                     if (test_tpm_address(tpm_base)) {
                         return tpm_base;
                     }
                 }
             }
         }
     } else {
         // ACPI 2.0+ - use XSDT
         xsdt_addr = *((uint64_t*)(rsdp_addr + 24));
         if (!xsdt_addr) {
             return 0;
         }
         
         // Validate XSDT signature
         if (memcmp((void*)xsdt_addr, ACPI_XSDT_SIGNATURE, 4) != 0) {
             printf("Invalid XSDT signature\n");
             return 0;
         }
         
         // Get table length and entries
         uint32_t xsdt_length = *((uint32_t*)(xsdt_addr + 4));
         uint32_t entries = (xsdt_length - 36) / 8; // 36 is the header size
         
         // Search for TPM table
         for (uint32_t i = 0; i < entries; i++) {
             uint64_t table_addr = *((uint64_t*)(xsdt_addr + 36 + i * 8));
             
             // Check if this is a TPM table
             if (memcmp((void*)table_addr, ACPI_TCPA_SIGNATURE, 4) == 0 ||
                 memcmp((void*)table_addr, ACPI_TPM2_SIGNATURE, 4) == 0) {
                 
                 // Found a TPM table, extract base address
                 if (memcmp((void*)table_addr, ACPI_TPM2_SIGNATURE, 4) == 0) {
                     // TPM 2.0 table - extract control area address
                     uint32_t tpm_base = *((uint32_t*)(table_addr + 48));
                     if (test_tpm_address(tpm_base)) {
                         return tpm_base;
                     }
                 } else if (memcmp((void*)table_addr, ACPI_TCPA_SIGNATURE, 4) == 0) {
                     // TPM 1.2 table - extract address
                     uint32_t tpm_base = *((uint32_t*)(table_addr + 40));
                     if (test_tpm_address(tpm_base)) {
                         return tpm_base;
                     }
                 }
             }
         }
     }
     
     return 0; // TPM not found in ACPI tables
 }
 
 // Main function to detect TPM base address
 uint32_t detect_tpm_base_address(void) {
     uint32_t tpm_base = 0;
     
     printf("Detecting TPM base address...\n");
     
     // First try PCI detection
     tpm_base = find_tpm_pci_device();
     if (tpm_base) {
         printf("TPM found via PCI at address 0x%08X\n", tpm_base);
         return tpm_base;
     }
     
     // If not found via PCI, try ACPI tables
     tpm_base = find_tpm_acpi_device();
     if (tpm_base) {
         printf("TPM found via ACPI at address 0x%08X\n", tpm_base);
         return tpm_base;
     }
     
     // If still not found, try known common addresses
     printf("TPM not found via PCI or ACPI, trying known addresses...\n");
     
     // Try the most common TPM address first
     tpm_base = test_tpm_address(TPM_DEFAULT_BASE_ADDR);
     if (tpm_base) {
         printf("TPM found at default address 0x%08X\n", tpm_base);
         return tpm_base;
     }
     
     // Try alternative addresses
     tpm_base = test_tpm_address(TPM_ALT_BASE_ADDR_1);
     if (tpm_base) {
         printf("TPM found at alternative address 0x%08X\n", tpm_base);
         return tpm_base;
     }
     
     tpm_base = test_tpm_address(TPM_ALT_BASE_ADDR_2);
     if (tpm_base) {
         printf("TPM found at alternative address 0x%08X\n", tpm_base);
         return tpm_base;
     }
     
     // If we get here, no TPM was found
     printf("No TPM device detected. Using default address 0x%08X\n", TPM_DEFAULT_BASE_ADDR);
     return TPM_DEFAULT_BASE_ADDR;
 }


 #define TPM_BASE_ADDR detect_tpm_base_address()  // Standard memory-mapped base address for TPM
 #define TPM_ACCESS_REG         (TPM_BASE_ADDR + 0x00)
 #define TPM_INT_ENABLE_REG     (TPM_BASE_ADDR + 0x08)
 #define TPM_INT_VECTOR_REG     (TPM_BASE_ADDR + 0x0C)
 #define TPM_INT_STATUS_REG     (TPM_BASE_ADDR + 0x10)
 #define TPM_INTF_CAPABILITY_REG (TPM_BASE_ADDR + 0x14)
 #define TPM_STS_REG            (TPM_BASE_ADDR + 0x18)
 #define TPM_DATA_FIFO_REG      (TPM_BASE_ADDR + 0x24)
 
 // TPM 2.0 command codes
 #define TPM_CC_STARTUP         0x00000144
 #define TPM_CC_SELF_TEST       0x00000143
 #define TPM_CC_NV_DEFINE_SPACE 0x0000012A
 #define TPM_CC_NV_WRITE        0x00000137
 #define TPM_CC_NV_READ         0x0000014E
 
 // TPM 2.0 handle types
 #define TPM_HANDLE_OWNER       0x40000001
 #define TPM_RH_OWNER           0x40000001
 #define TPM_RH_PLATFORM        0x4000000C
 
 // TPM 2.0 structure tags
 #define TPM_ST_NO_SESSIONS     0x8001
 #define TPM_ST_SESSIONS        0x8002
 
 // NV attributes
 #define TPMA_NV_OWNERWRITE     0x00000002
 #define TPMA_NV_OWNERREAD      0x00000001
 #define TPMA_NV_PLATFORMCREATE 0x00000400
 #define TPMA_NV_AUTHREAD       0x00040000
 #define TPMA_NV_AUTHWRITE      0x00080000
 #define TPMA_NV_NO_DA          0x00400000
 
 // TPM 2.0 Locality Settings
 #define TPM_LOCALITY_0         0
 #define TPM_LOCALITY_1         1
 #define TPM_LOCALITY_2         2
 #define TPM_LOCALITY_3         3
 #define TPM_LOCALITY_4         4
 
 // TPM 2.0 Status Register Bits
 #define TPM_STS_VALID          0x80
 #define TPM_STS_COMMAND_READY  0x40
 #define TPM_STS_GO             0x20
 #define TPM_STS_DATA_AVAIL     0x10
 #define TPM_STS_DATA_EXPECT    0x08
 
 // TPM 2.0 Access Register Bits
 #define TPM_ACCESS_VALID       0x80
 #define TPM_ACCESS_ACTIVE_LOCALITY 0x20
 #define TPM_ACCESS_REQUEST_USE 0x02
 #define TPM_ACCESS_REQUEST_PENDING 0x04
 
 // TPM 2.0 Return Codes
 #define TPM_RC_SUCCESS         0x00000000
 #define TPM_RC_FAILURE         0x00000001
 #define TPM_RC_BAD_TAG         0x0000001E
 #define TPM_RC_INSUFFICIENT    0x0000009A
 
 // Maximum data size for TPM operations
 #define TPM_MAX_DATA_SIZE      1024
 
 // Function declarations
 bool tpm_wait_for_status(uint32_t mask, uint32_t expected);
 bool tpm_set_locality(uint8_t locality);
 bool tpm_init(void);
 bool tpm_startup(uint16_t startupType);
 bool tpm_self_test(bool full_test);
 bool tpm_send_command(uint8_t* cmd, uint32_t size);
 bool tpm_read_response(uint8_t* response, uint32_t* size);
 bool tpm_nv_define_space(uint32_t nvIndex, uint16_t dataSize);
 bool tpm_nv_undefine_space(uint32_t nvIndex);
 bool tpm_nv_write(uint32_t nvIndex, uint8_t* data, uint16_t dataSize);
 bool tpm_nv_read(uint32_t nvIndex, uint8_t* data, uint16_t* dataSize);
 bool tpm_nv_index_exists(uint32_t nvIndex);
 bool tpm_nv_get_size(uint32_t nvIndex, uint16_t* size);
 bool tpm_encrypt_data(uint8_t* data, uint16_t dataSize, uint8_t* encryptedData, uint16_t* encryptedSize);
 bool tpm_decrypt_data(uint8_t* encryptedData, uint16_t encryptedSize, uint8_t* data, uint16_t* dataSize);
 bool tpm_get_random(uint8_t* buffer, uint16_t size);
 bool tpm_store_secure_data(const char* label, uint8_t* data, uint16_t dataSize);
 bool tpm_retrieve_secure_data(const char* label, uint8_t* data, uint16_t* dataSize);
 void tpm_secure_storage_example(void);
 
 // Basic delay function - used to give TPM time to process
 static void tpm_delay(uint32_t cycles) {
     for (uint32_t i = 0; i < cycles; i++) {
         // This is a simple busy wait
         __asm__ volatile("nop");
     }
 }
 
 // Wait for TPM to be ready
 bool tpm_wait_for_status(uint32_t mask, uint32_t expected) {
     uint32_t status;
     int timeout = 1000000; // Arbitrary timeout value
     
     while (timeout > 0) {
         status = mmio_read32(TPM_STS_REG);
         if ((status & mask) == expected) {
             return true;
         }
         timeout--;
         tpm_delay(100); // Short delay between checks
     }
     
     printf("TPM status timeout. Expected: 0x%x, Last status: 0x%x\n", expected, status);
     return false;
 }
 
 // Set the TPM locality
 bool tpm_set_locality(uint8_t locality) {
     if (locality > TPM_LOCALITY_4) {
         printf("Invalid locality: %d\n", locality);
         return false;
     }
     
     uint32_t localityAddr = TPM_BASE_ADDR + (locality << 12);
     uint8_t access = mmio_read8(localityAddr + 0x00);
     
     // Check if we already have this locality
     if ((access & TPM_ACCESS_ACTIVE_LOCALITY) != 0) {
         return true;
     }
     
     // Request the locality
     mmio_write8(localityAddr + 0x00, TPM_ACCESS_REQUEST_USE);
     
     // Wait for the locality to be granted
     int timeout = 100000;
     while (timeout > 0) {
         access = mmio_read8(localityAddr + 0x00);
         if ((access & TPM_ACCESS_ACTIVE_LOCALITY) != 0) {
             return true;
         }
         timeout--;
         tpm_delay(100);
     }
     
     printf("Failed to set locality %d\n", locality);
     return false;
 }
 
 // Initialize the TPM
 bool tpm_init(void) {
     uint8_t access;
     
     // Set to locality 0
     if (!tpm_set_locality(TPM_LOCALITY_0)) {
         printf("Failed to set locality\n");
         return false;
     }
     
     // Request access to TPM
     access = mmio_read8(TPM_ACCESS_REG);
     
     // If already requested, good to go
     if (access & TPM_ACCESS_REQUEST_USE) {
         return true;
     }
     
     // Request access
     mmio_write8(TPM_ACCESS_REG, TPM_ACCESS_REQUEST_USE);
     
     // Wait for TPM to grant access
     int timeout = 1000000;
     while (timeout > 0) {
         access = mmio_read8(TPM_ACCESS_REG);
         if (access & TPM_ACCESS_ACTIVE_LOCALITY) {
             // Clear command ready bit
             mmio_write8(TPM_STS_REG, TPM_STS_COMMAND_READY);
             
             // Wait for command ready
             if (!tpm_wait_for_status(TPM_STS_COMMAND_READY, TPM_STS_COMMAND_READY)) {
                 printf("TPM not ready for commands\n");
                 return false;
             }
             
             return true;
         }
         timeout--;
         tpm_delay(100);
     }
     
     printf("TPM initialization timeout\n");
     return false;
 }
 
 // Send a TPM command
 bool tpm_send_command(uint8_t* cmd, uint32_t size) {
     uint32_t status;
     uint32_t i;
     uint32_t burstCount;
     
     // Wait for TPM to be ready to receive command
     if (!tpm_wait_for_status(TPM_STS_COMMAND_READY, TPM_STS_COMMAND_READY)) {
         printf("TPM not ready for command\n");
         return false;
     }
     
     // Write command to FIFO in burst-sized chunks
     i = 0;
     while (i < size) {
         // Read burst count
         status = mmio_read32(TPM_STS_REG);
         burstCount = (status >> 8) & 0xFFFF;
         if (burstCount == 0) {
             burstCount = 1; // Minimum
         }
         
         // Write up to burstCount bytes
         for (uint32_t j = 0; j < burstCount && i < size; j++, i++) {
             mmio_write8(TPM_DATA_FIFO_REG, cmd[i]);
         }
         
         // Wait for data to be accepted
         if (!tpm_wait_for_status(TPM_STS_VALID, TPM_STS_VALID)) {
             printf("TPM did not accept data\n");
             return false;
         }
     }
     
     // Tell TPM we're done sending
     status = mmio_read32(TPM_STS_REG);
     mmio_write32(TPM_STS_REG, status | TPM_STS_GO);
     
     // Wait for TPM to process command
     if (!tpm_wait_for_status(TPM_STS_DATA_AVAIL, TPM_STS_DATA_AVAIL)) {
         printf("TPM command processing timeout\n");
         return false;
     }
     
     return true;
 }
 
 // Read response from TPM
 bool tpm_read_response(uint8_t* response, uint32_t* size) {
     uint32_t status;
     uint32_t count = 0;
     uint32_t burstCount;
     
     // Wait for data to be available
     if (!tpm_wait_for_status(TPM_STS_DATA_AVAIL, TPM_STS_DATA_AVAIL)) {
         printf("TPM no data available\n");
         return false;
     }
     
     // Read response
     while (count < *size) {
         // Get burst count (how many bytes we can read)
         status = mmio_read32(TPM_STS_REG);
         burstCount = (status >> 8) & 0xFFFF;
         if (burstCount == 0) {
             burstCount = 1; // Minimum
         }
         
         // Read up to burstCount bytes
         for (uint32_t i = 0; i < burstCount && count < *size; i++) {
             if (!(mmio_read8(TPM_STS_REG) & TPM_STS_DATA_AVAIL)) {
                 // No more data
                 break;
             }
             response[count++] = mmio_read8(TPM_DATA_FIFO_REG);
         }
         
         // Check if we're done
         if (!(mmio_read8(TPM_STS_REG) & TPM_STS_DATA_AVAIL)) {
             break;
         }
     }
     
     *size = count;
     
     // Tell TPM we're done reading
     status = mmio_read32(TPM_STS_REG);
     mmio_write32(TPM_STS_REG, status | TPM_STS_COMMAND_READY);
     
     return true;
 }
 
 // Send TPM2_Startup command (required after reset)
 bool tpm_startup(uint16_t startupType) {
     uint8_t cmd[12] = {0};
     uint32_t cmd_size = 12;
     uint8_t response[TPM_MAX_DATA_SIZE] = {0};
     uint32_t resp_size = sizeof(response);
     
     // Construct the command header
     cmd[0] = 0x80;  // TPM_ST_NO_SESSIONS >> 8
     cmd[1] = 0x01;  // TPM_ST_NO_SESSIONS & 0xFF
     cmd[2] = 0x00;  // Command size >> 8
     cmd[3] = 0x0C;  // Command size & 0xFF
     cmd[4] = 0x00;  // TPM_CC_STARTUP >> 24
     cmd[5] = 0x00;  // TPM_CC_STARTUP >> 16
     cmd[6] = 0x01;  // TPM_CC_STARTUP >> 8
     cmd[7] = 0x44;  // TPM_CC_STARTUP & 0xFF
     
     // Startup type (TPM_SU_CLEAR = 0x0000, TPM_SU_STATE = 0x0001)
     cmd[8] = (startupType >> 8) & 0xFF;
     cmd[9] = startupType & 0xFF;
     
     // Send the command
     if (!tpm_send_command(cmd, cmd_size)) {
         printf("Failed to send TPM_Startup command\n");
         return false;
     }
     
     // Read response
     if (!tpm_read_response(response, &resp_size)) {
         printf("Failed to read TPM_Startup response\n");
         return false;
     }
     
     // Check response code (some TPMs might return an error if already started)
     if (resp_size < 10) {
         printf("Invalid TPM_Startup response size\n");
         return false;
     }
     
     uint32_t responseCode = (response[6] << 24) | (response[7] << 16) | (response[8] << 8) | response[9];
     return (responseCode == TPM_RC_SUCCESS);
 }
 
 
 // Execute TPM self-test
 bool tpm_self_test(bool full_test) {
     uint8_t cmd[12] = {0};
     uint32_t cmd_size = 12;
     uint8_t response[TPM_MAX_DATA_SIZE] = {0};
     uint32_t resp_size = sizeof(response);
     
     // Construct the command header
     cmd[0] = 0x80;  // TPM_ST_NO_SESSIONS >> 8
     cmd[1] = 0x01;  // TPM_ST_NO_SESSIONS & 0xFF
     cmd[2] = 0x00;  // Command size >> 8
     cmd[3] = 0x0C;  // Command size & 0xFF
     cmd[4] = 0x00;  // TPM_CC_SELF_TEST >> 24
     cmd[5] = 0x00;  // TPM_CC_SELF_TEST >> 16
     cmd[6] = 0x01;  // TPM_CC_SELF_TEST >> 8
     cmd[7] = 0x43;  // TPM_CC_SELF_TEST & 0xFF
     
     // Full test flag
     cmd[8] = 0x00;
     cmd[9] = 0x00;
     cmd[10] = 0x00;
     cmd[11] = full_test ? 0x01 : 0x00;
     
     // Send the command
     if (!tpm_send_command(cmd, cmd_size)) {
         printf("Failed to send TPM_SelfTest command\n");
         return false;
     }
     
     // Read response
     if (!tpm_read_response(response, &resp_size)) {
         printf("Failed to read TPM_SelfTest response\n");
         return false;
     }
     
     // Check response code
     if (resp_size < 10) {
         printf("Invalid TPM_SelfTest response size\n");
         return false;
     }
     
     uint32_t responseCode = (response[6] << 24) | (response[7] << 16) | (response[8] << 8) | response[9];
     if (responseCode != TPM_RC_SUCCESS) {
         printf("TPM_SelfTest failed with code: 0x%08X\n", responseCode);
         return false;
     }
     
     return true;
 }
 
 // Define an NV space in TPM memory
 bool tpm_nv_define_space(uint32_t nvIndex, uint16_t dataSize) {
     uint8_t cmd[64] = {0};
     uint32_t cmd_size = 0;
     uint8_t response[TPM_MAX_DATA_SIZE] = {0};
     uint32_t resp_size = sizeof(response);
     
     // Construct the command header
     cmd[0] = 0x80;                          // TPM_ST_NO_SESSIONS >> 8
     cmd[1] = 0x01;                          // TPM_ST_NO_SESSIONS & 0xFF
     cmd[2] = 0x00;                          // Command size (filled in later)
     cmd[3] = 0x00;
     cmd[4] = 0x00;
     cmd[5] = 0x00;
     cmd[6] = 0x00;                          // TPM_CC_NV_DEFINE_SPACE >> 24
     cmd[7] = 0x00;                          // TPM_CC_NV_DEFINE_SPACE >> 16
     cmd[8] = 0x12;                          // TPM_CC_NV_DEFINE_SPACE >> 8
     cmd[9] = 0xA;                           // TPM_CC_NV_DEFINE_SPACE & 0xFF
     
     // Auth handle (TPM_RH_OWNER)
     cmd[10] = 0x40;                         // TPM_RH_OWNER >> 24
     cmd[11] = 0x00;                         // TPM_RH_OWNER >> 16
     cmd[12] = 0x00;                         // TPM_RH_OWNER >> 8
     cmd[13] = 0x01;                         // TPM_RH_OWNER & 0xFF
     
     // Auth size (0 for simple command)
     cmd[14] = 0x00;
     cmd[15] = 0x00;
     
     // NV public info
     cmd[16] = 0x00;                         // Size of public info (filled in later)
     cmd[17] = 0x00;
     
     // NV handle
     cmd[18] = 0x01;                         // TPM_HT_NV_INDEX >> 24
     cmd[19] = 0x00;                         // 0 >> 16
     cmd[20] = (nvIndex >> 8) & 0xFF;        // nvIndex >> 8
     cmd[21] = nvIndex & 0xFF;               // nvIndex & 0xFF
     
     // NV name algorithm (SHA256)
     cmd[22] = 0x00;
     cmd[23] = 0x0B;
     
     // NV attributes
     uint32_t attributes = TPMA_NV_OWNERWRITE | TPMA_NV_OWNERREAD | TPMA_NV_NO_DA;
     cmd[24] = (attributes >> 24) & 0xFF;
     cmd[25] = (attributes >> 16) & 0xFF;
     cmd[26] = (attributes >> 8) & 0xFF;
     cmd[27] = attributes & 0xFF;
     
     // NV auth policy (empty)
     cmd[28] = 0x00;
     cmd[29] = 0x00;
     
     // NV data size
     cmd[30] = (dataSize >> 8) & 0xFF;
     cmd[31] = dataSize & 0xFF;
     
     // Fill in sizes
     cmd[16] = 0x00;
     cmd[17] = 16;  // Size of NV public info
     
     cmd_size = 32;
     cmd[2] = (cmd_size >> 8) & 0xFF;
     cmd[3] = cmd_size & 0xFF;
     
     // Send command
     if (!tpm_send_command(cmd, cmd_size)) {
         printf("Failed to send NV_DefineSpace command\n");
         return false;
     }
     
     // Read response
     if (!tpm_read_response(response, &resp_size)) {
         printf("Failed to read NV_DefineSpace response\n");
         return false;
     }
     
     // Check response code
     if (resp_size < 10) {
         printf("Invalid NV_DefineSpace response size\n");
         return false;
     }
     
     uint32_t responseCode = (response[6] << 24) | (response[7] << 16) | (response[8] << 8) | response[9];
     if (responseCode != TPM_RC_SUCCESS) {
         printf("NV_DefineSpace failed with code: 0x%08X\n", responseCode);
         // The space might already exist, which is fine for our purposes
         if (responseCode == 0x14C) {  // TPM_RC_NV_DEFINED
             return true;
         }
         return false;
     }
     
     return true;
 }
 
 // Delete an NV space
 bool tpm_nv_undefine_space(uint32_t nvIndex) {
     uint8_t cmd[24] = {0};
     uint32_t cmd_size = 0;
     uint8_t response[TPM_MAX_DATA_SIZE] = {0};
     uint32_t resp_size = sizeof(response);
     
     // Construct the command header
     cmd[0] = 0x80;                          // TPM_ST_NO_SESSIONS >> 8
     cmd[1] = 0x01;                          // TPM_ST_NO_SESSIONS & 0xFF
     cmd[2] = 0x00;                          // Command size (filled in later)
     cmd[3] = 0x00;
     cmd[4] = 0x00;                          // TPM_CC_NV_UNDEFINE_SPACE >> 24
     cmd[5] = 0x00;                          // TPM_CC_NV_UNDEFINE_SPACE >> 16
     cmd[6] = 0x01;                          // TPM_CC_NV_UNDEFINE_SPACE >> 8
     cmd[7] = 0x22;                          // TPM_CC_NV_UNDEFINE_SPACE & 0xFF
     
     // Auth handle (TPM_RH_OWNER)
     cmd[8] = 0x40;                          // TPM_RH_OWNER >> 24
     cmd[9] = 0x00;                          // TPM_RH_OWNER >> 16
     cmd[10] = 0x00;                         // TPM_RH_OWNER >> 8
     cmd[11] = 0x01;                         // TPM_RH_OWNER & 0xFF
     
     // NV index handle
     cmd[12] = 0x01;                         // TPM_HT_NV_INDEX >> 24
     cmd[13] = 0x00;                         // 0 >> 16
     cmd[14] = (nvIndex >> 8) & 0xFF;        // nvIndex >> 8
     cmd[15] = nvIndex & 0xFF;               // nvIndex & 0xFF
     
     // Auth size (0 for simple command)
     cmd[16] = 0x00;
     cmd[17] = 0x00;
     
     // Fill in command size
     cmd_size = 18;
     cmd[2] = (cmd_size >> 8) & 0xFF;
     cmd[3] = cmd_size & 0xFF;
     
     // Send command
     if (!tpm_send_command(cmd, cmd_size)) {
         printf("Failed to send NV_UndefineSpace command\n");
         return false;
     }
     
     // Read response
     if (!tpm_read_response(response, &resp_size)) {
         printf("Failed to read NV_UndefineSpace response\n");
         return false;
     }
     
     // Check response code
     if (resp_size < 10) {
         printf("Invalid NV_UndefineSpace response size\n");
         return false;
     }
     
     uint32_t responseCode = (response[6] << 24) | (response[7] << 16) | (response[8] << 8) | response[9];
     if (responseCode != TPM_RC_SUCCESS) {
         printf("NV_UndefineSpace failed with code: 0x%08X\n", responseCode);
         return false;
     }
     
     return true;
 }
 


    // TPM NV Read function to read data from an NV index
bool tpm_nv_read(uint32_t nvIndex, uint8_t* data, uint16_t* dataSize) {
    uint8_t cmd[32] = {0};
    uint32_t cmd_size = 0;
    uint8_t response[TPM_MAX_DATA_SIZE] = {0};
    uint32_t resp_size = sizeof(response);
    
    // Construct the command header
    cmd[0] = 0x80;                          // TPM_ST_NO_SESSIONS >> 8
    cmd[1] = 0x01;                          // TPM_ST_NO_SESSIONS & 0xFF
    cmd[2] = 0x00;                          // Command size (filled in later)
    cmd[3] = 0x00;
    cmd[4] = 0x00;                          // TPM_CC_NV_READ >> 24
    cmd[5] = 0x00;                          // TPM_CC_NV_READ >> 16
    cmd[6] = 0x01;                          // TPM_CC_NV_READ >> 8
    cmd[7] = 0x4E;                          // TPM_CC_NV_READ & 0xFF
    
    // Auth handle (TPM_RH_OWNER)
    cmd[8] = 0x40;                          // TPM_RH_OWNER >> 24
    cmd[9] = 0x00;                          // TPM_RH_OWNER >> 16
    cmd[10] = 0x00;                         // TPM_RH_OWNER >> 8
    cmd[11] = 0x01;                         // TPM_RH_OWNER & 0xFF
    
    // NV index handle
    cmd[12] = 0x01;                         // TPM_HT_NV_INDEX >> 24
    cmd[13] = 0x00;                         // 0 >> 16
    cmd[14] = (nvIndex >> 8) & 0xFF;        // nvIndex >> 8
    cmd[15] = nvIndex & 0xFF;               // nvIndex & 0xFF
    
    // Auth size (0 for simple command)
    cmd[16] = 0x00;
    cmd[17] = 0x00;
    
    // Offset (0 to read from beginning)
    cmd[18] = 0x00;
    cmd[19] = 0x00;
    cmd[20] = 0x00;
    cmd[21] = 0x00;
    
    // Size to read
    cmd[22] = (*dataSize >> 8) & 0xFF;
    cmd[23] = *dataSize & 0xFF;
    
    // Fill in command size
    cmd_size = 24;
    cmd[2] = (cmd_size >> 8) & 0xFF;
    cmd[3] = cmd_size & 0xFF;
    
    // Send command
    if (!tpm_send_command(cmd, cmd_size)) {
        printf("Failed to send NV_Read command\n");
        return false;
    }
    
    // Read response
    if (!tpm_read_response(response, &resp_size)) {
        printf("Failed to read NV_Read response\n");
        return false;
    }
    
    // Check response code
    if (resp_size < 12) {
        printf("Invalid NV_Read response size\n");
        return false;
    }
    
    uint32_t responseCode = (response[6] << 24) | (response[7] << 16) | (response[8] << 8) | response[9];
    if (responseCode != TPM_RC_SUCCESS) {
        printf("NV_Read failed with code: 0x%08X\n", responseCode);
        return false;
    }
    
    // Extract data size from the response
    uint16_t actualSize = (response[10] << 8) | response[11];
    if (actualSize > *dataSize) {
        printf("Buffer too small for NV data\n");
        return false;
    }
    
    // Copy data from response to output buffer
    memcpy(data, &response[12], actualSize);
    *dataSize = actualSize;
    
    return true;
}

// TPM NV Write function to write data to an NV index
bool tpm_nv_write(uint32_t nvIndex, uint8_t* data, uint16_t dataSize) {
    uint8_t cmd[TPM_MAX_DATA_SIZE] = {0};
    uint32_t cmd_size = 0;
    uint8_t response[TPM_MAX_DATA_SIZE] = {0};
    uint32_t resp_size = sizeof(response);
    
    // Check if data size is within limits
    if (dataSize > TPM_MAX_DATA_SIZE - 32) {
        printf("Data too large for NV write\n");
        return false;
    }
    
    // Construct the command header
    cmd[0] = 0x80;                          // TPM_ST_NO_SESSIONS >> 8
    cmd[1] = 0x01;                          // TPM_ST_NO_SESSIONS & 0xFF
    cmd[2] = 0x00;                          // Command size (filled in later)
    cmd[3] = 0x00;
    cmd[4] = 0x00;                          // TPM_CC_NV_WRITE >> 24
    cmd[5] = 0x00;                          // TPM_CC_NV_WRITE >> 16
    cmd[6] = 0x01;                          // TPM_CC_NV_WRITE >> 8
    cmd[7] = 0x37;                          // TPM_CC_NV_WRITE & 0xFF
    
    // Auth handle (TPM_RH_OWNER)
    cmd[8] = 0x40;                          // TPM_RH_OWNER >> 24
    cmd[9] = 0x00;                          // TPM_RH_OWNER >> 16
    cmd[10] = 0x00;                         // TPM_RH_OWNER >> 8
    cmd[11] = 0x01;                         // TPM_RH_OWNER & 0xFF
    
    // NV index handle
    cmd[12] = 0x01;                         // TPM_HT_NV_INDEX >> 24
    cmd[13] = 0x00;                         // 0 >> 16
    cmd[14] = (nvIndex >> 8) & 0xFF;        // nvIndex >> 8
    cmd[15] = nvIndex & 0xFF;               // nvIndex & 0xFF
    
    // Auth size (0 for simple command)
    cmd[16] = 0x00;
    cmd[17] = 0x00;
    
    // Offset (0 to write from beginning)
    cmd[18] = 0x00;
    cmd[19] = 0x00;
    cmd[20] = 0x00;
    cmd[21] = 0x00;
    
    // Size of data to write
    cmd[22] = (dataSize >> 8) & 0xFF;
    cmd[23] = dataSize & 0xFF;
    
    // Copy data to command buffer
    memcpy(&cmd[24], data, dataSize);
    
    // Fill in command size
    cmd_size = 24 + dataSize;
    cmd[2] = (cmd_size >> 8) & 0xFF;
    cmd[3] = cmd_size & 0xFF;
    
    // Send command
    if (!tpm_send_command(cmd, cmd_size)) {
        printf("Failed to send NV_Write command\n");
        return false;
    }
    
    // Read response
    if (!tpm_read_response(response, &resp_size)) {
        printf("Failed to read NV_Write response\n");
        return false;
    }
    
    // Check response code
    if (resp_size < 10) {
        printf("Invalid NV_Write response size\n");
        return false;
    }
    
    uint32_t responseCode = (response[6] << 24) | (response[7] << 16) | (response[8] << 8) | response[9];
    if (responseCode != TPM_RC_SUCCESS) {
        printf("NV_Write failed with code: 0x%08X\n", responseCode);
        return false;
    }
    
    return true;
}

// Store text data in TPM with a label
bool tpm_store_secure_text(const char* label, const char* text) {
    uint32_t nvIndex;
    uint16_t textLength;
    uint8_t buffer[TPM_MAX_DATA_SIZE];
    
    // Calculate the label's hash to use as NV index
    // Simple hash for demonstration - in production use a cryptographic hash
    nvIndex = 0;
    for (int i = 0; label[i] != '\0'; i++) {
        nvIndex = (nvIndex << 5) + nvIndex + label[i];
    }
    
    // Ensure index is in a valid range for user-defined space
    nvIndex = (nvIndex & 0xFFFF) | 0x01000000;
    
    // Prepare the data (prefix with length)
    textLength = strlen(text);
    if (textLength > TPM_MAX_DATA_SIZE - 2) {
        printf("Text too long to store in TPM\n");
        return false;
    }
    
    buffer[0] = (textLength >> 8) & 0xFF;
    buffer[1] = textLength & 0xFF;
    memcpy(&buffer[2], text, textLength);
    
    // Define NV space if it doesn't exist
    if (!tpm_nv_index_exists(nvIndex)) {
        if (!tpm_nv_define_space(nvIndex, textLength + 2)) {
            printf("Failed to define NV space for label: %s\n", label);
            return false;
        }
    } else {
        // Space exists - check size
        uint16_t existingSize = 0;
        if (!tpm_nv_get_size(nvIndex, &existingSize)) {
            printf("Failed to get size of existing NV space\n");
            return false;
        }
        
        if (existingSize < textLength + 2) {
            // Undefine and redefine with larger size
            tpm_nv_undefine_space(nvIndex);
            if (!tpm_nv_define_space(nvIndex, textLength + 2)) {
                printf("Failed to redefine NV space for label: %s\n", label);
                return false;
            }
        }
    }
    
    // Write data to NV space
    if (!tpm_nv_write(nvIndex, buffer, textLength + 2)) {
        printf("Failed to write text data to TPM\n");
        return false;
    }
    
    printf("Successfully stored text for label: %s\n", label);
    return true;
}

// Retrieve text data from TPM using a label
bool tpm_retrieve_secure_text(const char* label, char* text, uint16_t maxLength) {
    uint32_t nvIndex;
    uint8_t buffer[TPM_MAX_DATA_SIZE];
    uint16_t bufferSize = TPM_MAX_DATA_SIZE;
    uint16_t textLength;
    
    // Calculate the label's hash to get NV index (same as in store function)
    nvIndex = 0;
    for (int i = 0; label[i] != '\0'; i++) {
        nvIndex = (nvIndex << 5) + nvIndex + label[i];
    }
    
    // Ensure index is in a valid range for user-defined space
    nvIndex = (nvIndex & 0xFFFF) | 0x01000000;
    
    // Check if index exists
    if (!tpm_nv_index_exists(nvIndex)) {
        printf("No data found for label: %s\n", label);
        return false;
    }
    
    // Read data from TPM
    if (!tpm_nv_read(nvIndex, buffer, &bufferSize)) {
        printf("Failed to read text data from TPM\n");
        return false;
    }
    
    // Extract length
    if (bufferSize < 2) {
        printf("Invalid data format in TPM\n");
        return false;
    }
    
    textLength = (buffer[0] << 8) | buffer[1];
    if (textLength + 2 > bufferSize) {
        printf("Corrupted data in TPM\n");
        return false;
    }
    
    // Check if we have enough space in the output buffer
    if (textLength >= maxLength) {
        printf("Buffer too small for text data\n");
        return false;
    }
    
    // Copy text and null-terminate
    memcpy(text, &buffer[2], textLength);
    text[textLength] = '\0';
    
    printf("Successfully retrieved text for label: %s\n", label);
    return true;
}

// Example function demonstrating secure text storage and retrieval
void tpm_secure_text_example(const char* textToStore) {
    char retrievedText[256] = {0};
    
    printf("TPM Secure Text Storage Example\n");
    
    // Initialize TPM
    if (!tpm_init()) {
        printf("Failed to initialize TPM\n");
        return;
    }
    
    // Send startup command
    if (!tpm_startup(0x0000)) {  // TPM_SU_CLEAR
        printf("TPM startup failed (may be already started)\n");
        // Continue anyway
    }
    
    // Store text with a label
    if (tpm_store_secure_text("secret_message", textToStore)) {
        printf("Successfully stored secret message\n");
    } else {
        printf("Failed to store secret message\n");
        return;
    }
    
    // Retrieve the text
    if (tpm_retrieve_secure_text("secret_message", retrievedText, sizeof(retrievedText))) {
        printf("Retrieved secret: %s\n", retrievedText);
    } else {
        printf("Failed to retrieve secret message\n");
    }
    
    // Store another text with a different label
    if (tpm_store_secure_text("config_data", "system.autoboot=true\nsystem.timeout=30")) {
        printf("Successfully stored configuration data\n");
        
        // Retrieve it
        if (tpm_retrieve_secure_text("config_data", retrievedText, sizeof(retrievedText))) {
            printf("Retrieved config: %s\n", retrievedText);
        }
    }
    
    printf("TPM Text Storage Example Completed\n");
}

// Complete implementation of tpm_nv_index_exists function from the original code
bool tpm_nv_index_exists(uint32_t nvIndex) {
    uint8_t cmd[24] = {0};
    uint32_t cmd_size = 0;
    uint8_t response[TPM_MAX_DATA_SIZE] = {0};
    uint32_t resp_size = sizeof(response);
    
    // Construct the command header for NV_ReadPublic
    cmd[0] = 0x80;                          // TPM_ST_NO_SESSIONS >> 8
    cmd[1] = 0x01;                          // TPM_ST_NO_SESSIONS & 0xFF
    cmd[2] = 0x00;                          // Command size (filled in later)
    cmd[3] = 0x00;
    cmd[4] = 0x00;                          // TPM_CC_NV_READPUBLIC >> 24
    cmd[5] = 0x00;                          // TPM_CC_NV_READPUBLIC >> 16
    cmd[6] = 0x01;                          // TPM_CC_NV_READPUBLIC >> 8
    cmd[7] = 0x69;                          // TPM_CC_NV_READPUBLIC & 0xFF
    
    // NV index handle
    cmd[8] = 0x01;                         // TPM_HT_NV_INDEX >> 24
    cmd[9] = 0x00;                         // 0 >> 16
    cmd[10] = (nvIndex >> 8) & 0xFF;       // nvIndex >> 8
    cmd[11] = nvIndex & 0xFF;              // nvIndex & 0xFF
    
    // Fill in command size
    cmd_size = 12;
    cmd[2] = (cmd_size >> 8) & 0xFF;
    cmd[3] = cmd_size & 0xFF;
    
    // Send command
    if (!tpm_send_command(cmd, cmd_size)) {
        return false;
    }
    
    // Read response
    if (!tpm_read_response(response, &resp_size)) {
        return false;
    }
    
    // Check response code
    if (resp_size < 10) {
        return false;
    }
    
    uint32_t responseCode = (response[6] << 24) | (response[7] << 16) | (response[8] << 8) | response[9];
    return (responseCode == TPM_RC_SUCCESS);
}

// Complete implementation of tpm_nv_get_size function
bool tpm_nv_get_size(uint32_t nvIndex, uint16_t* size) {
    uint8_t cmd[24] = {0};
    uint32_t cmd_size = 0;
    uint8_t response[TPM_MAX_DATA_SIZE] = {0};
    uint32_t resp_size = sizeof(response);
    
    // Construct the command header for NV_ReadPublic
    cmd[0] = 0x80;                          // TPM_ST_NO_SESSIONS >> 8
    cmd[1] = 0x01;                          // TPM_ST_NO_SESSIONS & 0xFF
    cmd[2] = 0x00;                          // Command size (filled in later)
    cmd[3] = 0x00;
    cmd[4] = 0x00;                          // TPM_CC_NV_READPUBLIC >> 24
    cmd[5] = 0x00;                          // TPM_CC_NV_READPUBLIC >> 16
    cmd[6] = 0x01;                          // TPM_CC_NV_READPUBLIC >> 8
    cmd[7] = 0x69;                          // TPM_CC_NV_READPUBLIC & 0xFF
    
    // NV index handle
    cmd[8] = 0x01;                         // TPM_HT_NV_INDEX >> 24
    cmd[9] = 0x00;                         // 0 >> 16
    cmd[10] = (nvIndex >> 8) & 0xFF;       // nvIndex >> 8
    cmd[11] = nvIndex & 0xFF;              // nvIndex & 0xFF
    
    // Fill in command size
    cmd_size = 12;
    cmd[2] = (cmd_size >> 8) & 0xFF;
    cmd[3] = cmd_size & 0xFF;
    
    // Send command
    if (!tpm_send_command(cmd, cmd_size)) {
        printf("Failed to send NV_ReadPublic command\n");
        return false;
    }
    
    // Read response
    if (!tpm_read_response(response, &resp_size)) {
        printf("Failed to read NV_ReadPublic response\n");
        return false;
    }
    
    // Check response code
    if (resp_size < 26) { // Minimum size for a valid response
        printf("Invalid NV_ReadPublic response size\n");
        return false;
    }
    
    uint32_t responseCode = (response[6] << 24) | (response[7] << 16) | (response[8] << 8) | response[9];
    if (responseCode != TPM_RC_SUCCESS) {
        printf("NV_ReadPublic failed with code: 0x%08X\n", responseCode);
        return false;
    }
    
    // Extract dataSize from the response (position may vary based on TPM implementation)
    // For most TPMs, it should be near the end of the NV public area
    // The data size is typically stored at offset 26 in the response for a TPM 2.0 NV_ReadPublic command
    *size = (response[26] << 8) | response[27];
    
    return true;
}