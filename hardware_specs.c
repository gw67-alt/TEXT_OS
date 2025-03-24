#include <stdint.h>
#include <stdbool.h>
#include "kernel.h"
#include "hardware_specs.h"

// Hardware detection and specification structure
hardware_info_t system_hardware;

// Buffer for string data
static char vendor_buffer[13];
static char brand_buffer[49];

// Initialize and detect hardware specifications
void hardware_specs_initialize(void) {
    terminal_writestring("Detecting hardware specifications...\n");
    
    // CPU information
    detect_cpu_info();
    
    // Memory information
    detect_memory_info();
    
    // Detect motherboard information
    detect_motherboard_info();
    
    // Display hardware information
    display_hardware_info();
}

// Detect CPU information using CPUID instruction
void detect_cpu_info(void) {
    uint32_t eax, ebx, ecx, edx;
    uint32_t max_std_id, max_ext_id;
    
    // Test if CPUID is supported by attempting to flip the ID flag in EFLAGS
    uint32_t flags_before, flags_after;
    
    // Try to toggle the ID bit in EFLAGS (bit 21)
    __asm__ volatile (
        "pushfl\n\t"          // Save EFLAGS to stack
        "pushfl\n\t"          // Push EFLAGS to top of stack
        "pop %0\n\t"          // Pop into flags_before
        "movl %0, %1\n\t"     // Copy to flags_after
        "xorl $0x200000, %1\n\t" // Toggle ID bit
        "push %1\n\t"         // Push modified EFLAGS
        "popfl\n\t"           // Pop into EFLAGS
        "pushfl\n\t"          // Get EFLAGS again
        "pop %1\n\t"          // Pop into flags_after
        "popfl"               // Restore original EFLAGS
        : "=r" (flags_before), "=r" (flags_after)
        :
        : "cc"
    );
    
    // Check if we could toggle the ID bit
    bool cpuid_supported = ((flags_before ^ flags_after) & 0x200000) != 0;
    
    if (cpuid_supported) {
        // Get highest standard function
        __asm__ volatile ("cpuid" 
            : "=a" (max_std_id), "=b" (ebx), "=c" (ecx), "=d" (edx) 
            : "0" (0));
            
        // Store vendor ID string
        *((uint32_t *) vendor_buffer) = ebx;
        *((uint32_t *) (vendor_buffer + 4)) = edx;
        *((uint32_t *) (vendor_buffer + 8)) = ecx;
        vendor_buffer[12] = '\0';
        
        system_hardware.cpu.vendor = vendor_buffer;
        
        // Check if extended functions are available
        __asm__ volatile ("cpuid"
            : "=a" (max_ext_id), "=b" (ebx), "=c" (ecx), "=d" (edx)
            : "0" (0x80000000));
            
        // Get CPU brand string if available (requires extended function 0x80000004)
        if (max_ext_id >= 0x80000004) {
            // Get CPU brand string (requires 3 calls to CPUID)
            __asm__ volatile ("cpuid"
                : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)
                : "0" (0x80000002));
                
            *((uint32_t *) brand_buffer) = eax;
            *((uint32_t *) (brand_buffer + 4)) = ebx;
            *((uint32_t *) (brand_buffer + 8)) = ecx;
            *((uint32_t *) (brand_buffer + 12)) = edx;
            
            __asm__ volatile ("cpuid"
                : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)
                : "0" (0x80000003));
                
            *((uint32_t *) (brand_buffer + 16)) = eax;
            *((uint32_t *) (brand_buffer + 20)) = ebx;
            *((uint32_t *) (brand_buffer + 24)) = ecx;
            *((uint32_t *) (brand_buffer + 28)) = edx;
            
            __asm__ volatile ("cpuid"
                : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)
                : "0" (0x80000004));
                
            *((uint32_t *) (brand_buffer + 32)) = eax;
            *((uint32_t *) (brand_buffer + 36)) = ebx;
            *((uint32_t *) (brand_buffer + 40)) = ecx;
            *((uint32_t *) (brand_buffer + 44)) = edx;
            brand_buffer[48] = '\0';
            
            system_hardware.cpu.model = brand_buffer;
        } else {
            system_hardware.cpu.model = "Unknown Model";
        }
        
        // Get core/thread information if available
        if (max_std_id >= 1) {
            __asm__ volatile ("cpuid"
                : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)
                : "0" (1));
                
            // EAX bits 8-11 contain family, 4-7 contain model
            uint32_t family = ((eax >> 8) & 0xF) + ((eax >> 20) & 0xFF);
            uint32_t model = ((eax >> 4) & 0xF) | (((eax >> 16) & 0xF) << 4);
            
            system_hardware.cpu.family = family;
            system_hardware.cpu.model_id = model;
                
            // Check if hyperthreading is supported (EDX bit 28)
            bool ht_supported = (edx & (1 << 28)) != 0;
            
            // Default to single core if we can't determine
            system_hardware.cpu.cores = 1;
            system_hardware.cpu.threads = ht_supported ? 2 : 1;
            
            // If CPUID leaf 4 is supported, we can get more accurate core count
            if (max_std_id >= 4) {
                uint32_t cores_per_package = 0;
                
                __asm__ volatile ("cpuid"
                    : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)
                    : "0" (4), "2" (0));
                    
                cores_per_package = ((eax >> 26) & 0x3F) + 1;
                system_hardware.cpu.cores = cores_per_package;
                system_hardware.cpu.threads = ht_supported ? cores_per_package * 2 : cores_per_package;
            }
            
            // Get basic frequency information if available
            // Note: Real implementation would read MSRs for frequency
            // For now we'll estimate based on ID information
            system_hardware.cpu.frequency_mhz = 0;
            
            if (max_ext_id >= 0x80000007) {
                // Get invariant TSC info
                __asm__ volatile ("cpuid"
                    : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)
                    : "0" (0x80000007));
                
                // Check if invariant TSC is available (EDX bit 8)
                bool invariant_tsc = (edx & (1 << 8)) != 0;
                system_hardware.cpu.has_invariant_tsc = invariant_tsc;
            }
        }
    } else {
        system_hardware.cpu.vendor = "CPUID Not Supported";
        system_hardware.cpu.model = "Unknown CPU";
        system_hardware.cpu.cores = 1;
        system_hardware.cpu.threads = 1;
        system_hardware.cpu.frequency_mhz = 0;
    }
}

// Detect memory information using BIOS E820 memory map or multiboot info
void detect_memory_info(void) {
    // In a real implementation, you would:
    // 1. Use multiboot info if booted via GRUB
    // 2. Parse the E820 memory map from BIOS
    // 3. Use UEFI memory map if booted via UEFI
    
    // For this demo, we'll use a simple memory detection algorithm
    // that probes memory in 1MB increments
    
    uint32_t total_mb = 0;
    volatile uint32_t* mem_test;
    const uint32_t pattern = 0xAA55AA55;
    const uint32_t inverse = 0x55AA55AA;
    const uint32_t base_addr = 0x1000000; // Start at 16MB
    
    // We'll limit our test to 4GB for safety
    for (uint32_t mb = 16; mb < 4096; mb++) {
        uint32_t addr = base_addr + (mb - 16) * 0x100000; // 1MB increments
        
        // Skip memory-mapped hardware regions
        if (mb >= 768 && mb < 1024) {
            continue; // Skip 768MB-1024MB typically reserved for PCI
        }
        
        // Try to access memory
        mem_test = (volatile uint32_t*)addr;
        
        uint32_t old_value = *mem_test;
        *mem_test = pattern;
        
        // Memory barrier to ensure write completes
        __asm__ volatile ("" ::: "memory");
        
        if (*mem_test != pattern) {
            break; // Memory not accessible
        }
        
        *mem_test = inverse;
        
        // Memory barrier
        __asm__ volatile ("" ::: "memory");
        
        if (*mem_test != inverse) {
            break; // Memory not writable
        }
        
        // Restore original value
        *mem_test = old_value;
        
        total_mb = mb + 1;
    }
    
    system_hardware.memory.total_mb = total_mb;
    
    // Memory type detection would require reading SPD via SMBus
    // For now, make an educated guess based on system age
    
    // Check CPU features to guess memory type
    if (system_hardware.cpu.family >= 0x19) { // Newer AMD CPUs
        system_hardware.memory.type = "DDR5";
        system_hardware.memory.speed_mhz = 4800;
    } else if (system_hardware.cpu.family >= 0x17) {
        system_hardware.memory.type = "DDR4";
        system_hardware.memory.speed_mhz = 3200;
    } else {
        system_hardware.memory.type = "DDR3";
        system_hardware.memory.speed_mhz = 1600;
    }
    
    // Assume dual channel as most common configuration
    system_hardware.memory.channels = 2;
}

// Detect motherboard information
void detect_motherboard_info(void) {
    // In a real implementation, you would:
    // 1. Query SMBIOS tables for detailed motherboard information
    // 2. Access DMI information for manufacturer and model
    
    // For simplicity, we'll check for common signatures
    // in the BIOS region to identify manufacturer
    
    // The BIOS ROM typically starts at 0xF0000
    const char* bios_region = (const char*)0xF0000;
    const uint32_t bios_size = 0x10000; // 64KB
    
    // Check for manufacturer signatures
    bool found = false;
    
    // Common manufacturer strings to search for
    const char* manufacturer_strings[] = {
        "ASUS", "ASUSTeK", "Gigabyte", "MSI", "ASRock", "BIOSTAR",
        "EVGA", "Supermicro", "Intel", "AMD", "Dell", "HP", "Lenovo", "IBM"
    };
    
    for (int i = 0; i < sizeof(manufacturer_strings) / sizeof(const char*); i++) {
        const char* manufacturer = manufacturer_strings[i];
        uint32_t len = 0;
        
        // Calculate string length
        while (manufacturer[len] != '\0') {
            len++;
        }
        
        // Search through BIOS region
        for (uint32_t j = 0; j < bios_size - len; j++) {
            bool match = true;
            
            for (uint32_t k = 0; k < len; k++) {
                if (bios_region[j + k] != manufacturer[k]) {
                    match = false;
                    break;
                }
            }
            
            if (match) {
                system_hardware.motherboard.manufacturer = manufacturer;
                found = true;
                break;
            }
        }
        
        if (found) {
            break;
        }
    }
    
    if (!found) {
        system_hardware.motherboard.manufacturer = "Unknown";
    }
    
    // For B650 chipset detection, check CPU and AMD signatures
    if (system_hardware.cpu.vendor != NULL && 
        str_contains(system_hardware.cpu.vendor, "AMD") &&
        (system_hardware.cpu.family >= 0x19)) {
        
        // Check for B650 specific signatures
        // Real implementation would use PCI configuration space to identify the chipset
        
        // For now, just use AMD CPU to guess B650 for Ryzen 7000 series
        system_hardware.motherboard.chipset = "B650";
        system_hardware.motherboard.model = "B650M";
    } else {
        // Make educated guesses based on CPU
        if (system_hardware.cpu.vendor != NULL) {
            if (str_contains(system_hardware.cpu.vendor, "Intel")) {
                if (system_hardware.cpu.family >= 0x06 && system_hardware.cpu.model_id >= 0x9A) {
                    system_hardware.motherboard.chipset = "Z690";
                } else {
                    system_hardware.motherboard.chipset = "Intel";
                }
            } else if (str_contains(system_hardware.cpu.vendor, "AMD")) {
                system_hardware.motherboard.chipset = "AMD";
            } else {
                system_hardware.motherboard.chipset = "Unknown";
            }
        } else {
            system_hardware.motherboard.chipset = "Unknown";
        }
        
        system_hardware.motherboard.model = "Unknown";
    }
}

// String contains helper function
bool str_contains(const char* str, const char* substr) {
    if (str == NULL || substr == NULL) {
        return false;
    }
    
    while (*str) {
        const char* a = str;
        const char* b = substr;
        
        while (*a && *b && *a == *b) {
            a++;
            b++;
        }
        
        if (!*b) {
            return true;
        }
        
        str++;
    }
    
    return false;
}

// Display detected hardware information
void display_hardware_info(void) {
    terminal_writestring("\n===== HARDWARE SPECIFICATIONS =====\n");
    
    // CPU info
    terminal_writestring("CPU: ");
    if (system_hardware.cpu.model != NULL) {
        terminal_writestring(system_hardware.cpu.model);
    } else {
        terminal_writestring("Unknown");
    }
    terminal_writestring("\n");
    
    terminal_writestring("Vendor: ");
    if (system_hardware.cpu.vendor != NULL) {
        terminal_writestring(system_hardware.cpu.vendor);
    } else {
        terminal_writestring("Unknown");
    }
    terminal_writestring("\n");
    
    terminal_writestring("Family/Model: ");
    char buffer[32];
    int_to_string(system_hardware.cpu.family, buffer);
    terminal_writestring(buffer);
    terminal_writestring("h/");
    int_to_string(system_hardware.cpu.model_id, buffer);
    terminal_writestring(buffer);
    terminal_writestring("h\n");
    
    terminal_writestring("Cores/Threads: ");
    int_to_string(system_hardware.cpu.cores, buffer);
    terminal_writestring(buffer);
    terminal_writestring("/");
    int_to_string(system_hardware.cpu.threads, buffer);
    terminal_writestring(buffer);
    terminal_writestring("\n");
    
    if (system_hardware.cpu.frequency_mhz > 0) {
        terminal_writestring("Frequency: ");
        int_to_string(system_hardware.cpu.frequency_mhz, buffer);
        terminal_writestring(buffer);
        terminal_writestring(" MHz\n");
    }
    
    // Memory info
    terminal_writestring("\nMemory: ");
    int_to_string(system_hardware.memory.total_mb, buffer);
    terminal_writestring(buffer);
    terminal_writestring(" MB ");
    terminal_writestring(system_hardware.memory.type);
    
    if (system_hardware.memory.speed_mhz > 0) {
        terminal_writestring(" @ ");
        int_to_string(system_hardware.memory.speed_mhz, buffer);
        terminal_writestring(buffer);
        terminal_writestring(" MHz");
    }
    
    terminal_writestring(" (");
    int_to_string(system_hardware.memory.channels, buffer);
    terminal_writestring(buffer);
    terminal_writestring(" channels)\n");
    
    // Motherboard info
    terminal_writestring("\nMotherboard: ");
    terminal_writestring(system_hardware.motherboard.manufacturer);
    
    if (system_hardware.motherboard.model != NULL && 
        system_hardware.motherboard.model[0] != '\0' &&
        !str_equals(system_hardware.motherboard.model, "Unknown")) {
        terminal_writestring(" ");
        terminal_writestring(system_hardware.motherboard.model);
    }
    
    terminal_writestring("\nChipset: ");
    terminal_writestring(system_hardware.motherboard.chipset);
    terminal_writestring("\n");
    
    terminal_writestring("\n==================================\n");
}

// String equality helper function
bool str_equals(const char* str1, const char* str2) {
    if (str1 == NULL || str2 == NULL) {
        return str1 == str2;
    }
    
    while (*str1 && *str2 && *str1 == *str2) {
        str1++;
        str2++;
    }
    
    return *str1 == *str2;
}