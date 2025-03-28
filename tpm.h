/**
 * TPM Data Storage Implementation for Bare Metal x86
 * Header File
 */

 #ifndef TPM_H
 #define TPM_H
 
 #include <stdint.h>
 #include <stdbool.h>
 
 // Maximum data size for TPM operations
 #define TPM_MAX_DATA_SIZE      1024
 
 // TPM 2.0 Return Codes
 #define TPM_RC_SUCCESS         0x00000000
 #define TPM_RC_FAILURE         0x00000001
 #define TPM_RC_BAD_TAG         0x0000001E
 #define TPM_RC_INSUFFICIENT    0x0000009A
 
 // Function declarations
 bool tpm_init(void);
 bool tpm_startup(uint16_t startupType);
 bool tpm_self_test(bool full_test);
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
 
 #endif /* TPM_H */