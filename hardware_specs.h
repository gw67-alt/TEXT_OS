#ifndef HARDWARE_SPECS_H
#define HARDWARE_SPECS_H

#include <stdint.h>
#include <stdbool.h>

// CPU information structure
typedef struct {
    const char* vendor;
    const char* model;
    uint32_t family;
    uint32_t model_id;
    uint32_t cores;
    uint32_t threads;
    uint32_t frequency_mhz;
    bool has_invariant_tsc;
} cpu_info_t;

// Memory information structure
typedef struct {
    uint32_t total_mb;
    const char* type;
    uint32_t speed_mhz;
    uint32_t channels;
} memory_info_t;

// Motherboard information structure
typedef struct {
    const char* manufacturer;
    const char* model;
    const char* chipset;
} motherboard_info_t;

// Overall hardware information structure
typedef struct {
    cpu_info_t cpu;
    memory_info_t memory;
    motherboard_info_t motherboard;
} hardware_info_t;

// External declaration of the hardware info structure
extern hardware_info_t system_hardware;

// Function prototypes
void hardware_specs_initialize(void);
void detect_cpu_info(void);
void detect_memory_info(void);
void detect_motherboard_info(void);
void display_hardware_info(void);
bool str_contains(const char* str, const char* substr);
bool str_equals(const char* str1, const char* str2);

#endif /* HARDWARE_SPECS_H */