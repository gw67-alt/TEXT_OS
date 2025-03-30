/**
 * nvme_config.h - NVMe controller configuration and management
 * 
 * Header file with functions for configuring and interacting with NVMe controllers.
 */

 #ifndef NVME_CONFIG_H
 #define NVME_CONFIG_H
 
 #include <stdint.h>
 
 /**
  * Add and initialize a new NVMe controller
  * 
  * @param base_addr Memory-mapped base address of the controller registers
  * @param bar_size Size of the BAR memory region
  * @return Controller ID (>=0) if successful, negative value on error
  */
 int nvme_add_controller(volatile uint8_t* base_addr, uint32_t bar_size);
 
 /**
  * Get the number of initialized NVMe controllers
  * 
  * @return Number of controllers
  */
 uint8_t nvme_get_controller_count();
 
 /**
  * Display information about all initialized NVMe controllers
  */
 void nvme_display_controllers();
 
 /**
  * Scan the PCI bus for NVMe controllers and initialize them
  * 
  * @return Number of controllers found and initialized
  */
 int nvme_scan_and_initialize();
 
 #endif /* NVME_CONFIG_H */