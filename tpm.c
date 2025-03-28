/**
 * TPM Data Storage Implementation for Bare Metal x86
 * 
 * This implementation provides low-level functionality for storing data
 * in a TPM 2.0 device using direct hardware access without relying on
 * an operating system's abstractions.
 */

 #include <stdint.h>
 #include <stdbool.h>
 #include <string.h>
 
 // TPM 2.0 register addresses (assuming memory-mapped I/O)
 // These addresses may vary depending on the hardware implementation
 #define TPM_BASE_ADDR          0xFED40000  // Standard memory-mapped base address for TPM
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
 
 // Debug macros
 #if defined(DEBUG_TPM)
 extern void printf(const char* format, ...);
 #define DEBUG_PRINT(fmt, ...) printf(fmt, ##__VA_ARGS__)
 #else
 #define DEBUG_PRINT(fmt, ...)
 #endif
 
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
 
 // I/O port functions for x86
 static inline void outb(uint16_t port, uint8_t val) {
     __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
 }
 
 static inline uint8_t inb(uint16_t port) {
     uint8_t ret;
     __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
     return ret;
 }
 
 static inline void outl(uint16_t port, uint32_t val) {
     __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
 }
 
 static inline uint32_t inl(uint16_t port) {
     uint32_t ret;
     __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
     return ret;
 }
 
 // Memory-mapped I/O functions
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
     
     DEBUG_PRINT("TPM status timeout. Expected: 0x%x, Last status: 0x%x\n", expected, status);
     return false;
 }
 
 // Set the TPM locality
 bool tpm_set_locality(uint8_t locality) {
     if (locality > TPM_LOCALITY_4) {
         DEBUG_PRINT("Invalid locality: %d\n", locality);
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
     
     DEBUG_PRINT("Failed to set locality %d\n", locality);
     return false;
 }
 
 // Initialize the TPM
 bool tpm_init(void) {
     uint8_t access;
     
     // Set to locality 0
     if (!tpm_set_locality(TPM_LOCALITY_0)) {
         DEBUG_PRINT("Failed to set locality\n");
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
                 DEBUG_PRINT("TPM not ready for commands\n");
                 return false;
             }
             
             return true;
         }
         timeout--;
         tpm_delay(100);
     }
     
     DEBUG_PRINT("TPM initialization timeout\n");
     return false;
 }
 
 // Send a TPM command
 bool tpm_send_command(uint8_t* cmd, uint32_t size) {
     uint32_t status;
     uint32_t i;
     uint32_t burstCount;
     
     // Wait for TPM to be ready to receive command
     if (!tpm_wait_for_status(TPM_STS_COMMAND_READY, TPM_STS_COMMAND_READY)) {
         DEBUG_PRINT("TPM not ready for command\n");
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
             DEBUG_PRINT("TPM did not accept data\n");
             return false;
         }
     }
     
     // Tell TPM we're done sending
     status = mmio_read32(TPM_STS_REG);
     mmio_write32(TPM_STS_REG, status | TPM_STS_GO);
     
     // Wait for TPM to process command
     if (!tpm_wait_for_status(TPM_STS_DATA_AVAIL, TPM_STS_DATA_AVAIL)) {
         DEBUG_PRINT("TPM command processing timeout\n");
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
         DEBUG_PRINT("TPM no data available\n");
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
         DEBUG_PRINT("Failed to send TPM_Startup command\n");
         return false;
     }
     
     // Read response
     if (!tpm_read_response(response, &resp_size)) {
         DEBUG_PRINT("Failed to read TPM_Startup response\n");
         return false;
     }
     
     // Check response code (some TPMs might return an error if already started)
     if (resp_size < 10) {
         DEBUG_PRINT("Invalid TPM_Startup response size\n");
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
         DEBUG_PRINT("Failed to send TPM_SelfTest command\n");
         return false;
     }
     
     // Read response
     if (!tpm_read_response(response, &resp_size)) {
         DEBUG_PRINT("Failed to read TPM_SelfTest response\n");
         return false;
     }
     
     // Check response code
     if (resp_size < 10) {
         DEBUG_PRINT("Invalid TPM_SelfTest response size\n");
         return false;
     }
     
     uint32_t responseCode = (response[6] << 24) | (response[7] << 16) | (response[8] << 8) | response[9];
     if (responseCode != TPM_RC_SUCCESS) {
         DEBUG_PRINT("TPM_SelfTest failed with code: 0x%08X\n", responseCode);
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
         DEBUG_PRINT("Failed to send NV_DefineSpace command\n");
         return false;
     }
     
     // Read response
     if (!tpm_read_response(response, &resp_size)) {
         DEBUG_PRINT("Failed to read NV_DefineSpace response\n");
         return false;
     }
     
     // Check response code
     if (resp_size < 10) {
         DEBUG_PRINT("Invalid NV_DefineSpace response size\n");
         return false;
     }
     
     uint32_t responseCode = (response[6] << 24) | (response[7] << 16) | (response[8] << 8) | response[9];
     if (responseCode != TPM_RC_SUCCESS) {
         DEBUG_PRINT("NV_DefineSpace failed with code: 0x%08X\n", responseCode);
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
         DEBUG_PRINT("Failed to send NV_UndefineSpace command\n");
         return false;
     }
     
     // Read response
     if (!tpm_read_response(response, &resp_size)) {
         DEBUG_PRINT("Failed to read NV_UndefineSpace response\n");
         return false;
     }
     
     // Check response code
     if (resp_size < 10) {
         DEBUG_PRINT("Invalid NV_UndefineSpace response size\n");
         return false;
     }
     
     uint32_t responseCode = (response[6] << 24) | (response[7] << 16) | (response[8] << 8) | response[9];
     if (responseCode != TPM_RC_SUCCESS) {
         DEBUG_PRINT("NV_UndefineSpace failed with code: 0x%08X\n", responseCode);
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
        DEBUG_PRINT("Failed to send NV_Read command\n");
        return false;
    }
    
    // Read response
    if (!tpm_read_response(response, &resp_size)) {
        DEBUG_PRINT("Failed to read NV_Read response\n");
        return false;
    }
    
    // Check response code
    if (resp_size < 12) {
        DEBUG_PRINT("Invalid NV_Read response size\n");
        return false;
    }
    
    uint32_t responseCode = (response[6] << 24) | (response[7] << 16) | (response[8] << 8) | response[9];
    if (responseCode != TPM_RC_SUCCESS) {
        DEBUG_PRINT("NV_Read failed with code: 0x%08X\n", responseCode);
        return false;
    }
    
    // Extract data size from the response
    uint16_t actualSize = (response[10] << 8) | response[11];
    if (actualSize > *dataSize) {
        DEBUG_PRINT("Buffer too small for NV data\n");
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
        DEBUG_PRINT("Data too large for NV write\n");
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
        DEBUG_PRINT("Failed to send NV_Write command\n");
        return false;
    }
    
    // Read response
    if (!tpm_read_response(response, &resp_size)) {
        DEBUG_PRINT("Failed to read NV_Write response\n");
        return false;
    }
    
    // Check response code
    if (resp_size < 10) {
        DEBUG_PRINT("Invalid NV_Write response size\n");
        return false;
    }
    
    uint32_t responseCode = (response[6] << 24) | (response[7] << 16) | (response[8] << 8) | response[9];
    if (responseCode != TPM_RC_SUCCESS) {
        DEBUG_PRINT("NV_Write failed with code: 0x%08X\n", responseCode);
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
        DEBUG_PRINT("Text too long to store in TPM\n");
        return false;
    }
    
    buffer[0] = (textLength >> 8) & 0xFF;
    buffer[1] = textLength & 0xFF;
    memcpy(&buffer[2], text, textLength);
    
    // Define NV space if it doesn't exist
    if (!tpm_nv_index_exists(nvIndex)) {
        if (!tpm_nv_define_space(nvIndex, textLength + 2)) {
            DEBUG_PRINT("Failed to define NV space for label: %s\n", label);
            return false;
        }
    } else {
        // Space exists - check size
        uint16_t existingSize = 0;
        if (!tpm_nv_get_size(nvIndex, &existingSize)) {
            DEBUG_PRINT("Failed to get size of existing NV space\n");
            return false;
        }
        
        if (existingSize < textLength + 2) {
            // Undefine and redefine with larger size
            tpm_nv_undefine_space(nvIndex);
            if (!tpm_nv_define_space(nvIndex, textLength + 2)) {
                DEBUG_PRINT("Failed to redefine NV space for label: %s\n", label);
                return false;
            }
        }
    }
    
    // Write data to NV space
    if (!tpm_nv_write(nvIndex, buffer, textLength + 2)) {
        DEBUG_PRINT("Failed to write text data to TPM\n");
        return false;
    }
    
    DEBUG_PRINT("Successfully stored text for label: %s\n", label);
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
        DEBUG_PRINT("No data found for label: %s\n", label);
        return false;
    }
    
    // Read data from TPM
    if (!tpm_nv_read(nvIndex, buffer, &bufferSize)) {
        DEBUG_PRINT("Failed to read text data from TPM\n");
        return false;
    }
    
    // Extract length
    if (bufferSize < 2) {
        DEBUG_PRINT("Invalid data format in TPM\n");
        return false;
    }
    
    textLength = (buffer[0] << 8) | buffer[1];
    if (textLength + 2 > bufferSize) {
        DEBUG_PRINT("Corrupted data in TPM\n");
        return false;
    }
    
    // Check if we have enough space in the output buffer
    if (textLength >= maxLength) {
        DEBUG_PRINT("Buffer too small for text data\n");
        return false;
    }
    
    // Copy text and null-terminate
    memcpy(text, &buffer[2], textLength);
    text[textLength] = '\0';
    
    DEBUG_PRINT("Successfully retrieved text for label: %s\n", label);
    return true;
}

// Example function demonstrating secure text storage and retrieval
void tpm_secure_text_example(const char* textToStore) {
    char retrievedText[256] = {0};
    
    DEBUG_PRINT("TPM Secure Text Storage Example\n");
    
    // Initialize TPM
    if (!tpm_init()) {
        DEBUG_PRINT("Failed to initialize TPM\n");
        return;
    }
    
    // Send startup command
    if (!tpm_startup(0x0000)) {  // TPM_SU_CLEAR
        DEBUG_PRINT("TPM startup failed (may be already started)\n");
        // Continue anyway
    }
    
    // Store text with a label
    if (tpm_store_secure_text("secret_message", textToStore)) {
        DEBUG_PRINT("Successfully stored secret message\n");
    } else {
        DEBUG_PRINT("Failed to store secret message\n");
        return;
    }
    
    // Retrieve the text
    if (tpm_retrieve_secure_text("secret_message", retrievedText, sizeof(retrievedText))) {
        DEBUG_PRINT("Retrieved secret: %s\n", retrievedText);
    } else {
        DEBUG_PRINT("Failed to retrieve secret message\n");
    }
    
    // Store another text with a different label
    if (tpm_store_secure_text("config_data", "system.autoboot=true\nsystem.timeout=30")) {
        DEBUG_PRINT("Successfully stored configuration data\n");
        
        // Retrieve it
        if (tpm_retrieve_secure_text("config_data", retrievedText, sizeof(retrievedText))) {
            DEBUG_PRINT("Retrieved config: %s\n", retrievedText);
        }
    }
    
    DEBUG_PRINT("TPM Text Storage Example Completed\n");
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
        DEBUG_PRINT("Failed to send NV_ReadPublic command\n");
        return false;
    }
    
    // Read response
    if (!tpm_read_response(response, &resp_size)) {
        DEBUG_PRINT("Failed to read NV_ReadPublic response\n");
        return false;
    }
    
    // Check response code
    if (resp_size < 26) { // Minimum size for a valid response
        DEBUG_PRINT("Invalid NV_ReadPublic response size\n");
        return false;
    }
    
    uint32_t responseCode = (response[6] << 24) | (response[7] << 16) | (response[8] << 8) | response[9];
    if (responseCode != TPM_RC_SUCCESS) {
        DEBUG_PRINT("NV_ReadPublic failed with code: 0x%08X\n", responseCode);
        return false;
    }
    
    // Extract dataSize from the response (position may vary based on TPM implementation)
    // For most TPMs, it should be near the end of the NV public area
    // The data size is typically stored at offset 26 in the response for a TPM 2.0 NV_ReadPublic command
    *size = (response[26] << 8) | response[27];
    
    return true;
}