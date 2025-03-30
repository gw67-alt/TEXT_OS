/**
 * nvme_config.c - NVMe controller configuration and management
 * 
 * This file implements basic configuration and communication with NVMe devices.
 */

 #include "nvme_config.h"
 #include "iostream_wrapper.h"

 #include "string.h"
 #include <stdint.h>
 #include <stdbool.h>
 

 
 // Global instances from iostream_wrapper.h
 extern TerminalOutput cout;
 extern TerminalInput cin;
 
 // NVMe controller register offsets
 #define NVME_REG_CAP       0x0000  // Controller Capabilities
 #define NVME_REG_VS        0x0008  // Version
 #define NVME_REG_INTMS     0x000c  // Interrupt Mask Set
 #define NVME_REG_INTMC     0x0010  // Interrupt Mask Clear
 #define NVME_REG_CC        0x0014  // Controller Configuration
 #define NVME_REG_CSTS      0x001c  // Controller Status
 #define NVME_REG_NSSR      0x0020  // NVM Subsystem Reset
 #define NVME_REG_AQA       0x0024  // Admin Queue Attributes
 #define NVME_REG_ASQ       0x0028  // Admin Submission Queue Base Address
 #define NVME_REG_ACQ       0x0030  // Admin Completion Queue Base Address
 #define NVME_REG_CMBLOC    0x0038  // Controller Memory Buffer Location
 #define NVME_REG_CMBSZ     0x003c  // Controller Memory Buffer Size
 
 // Controller Configuration register bits
 #define NVME_CC_EN         (1 << 0)  // Enable
 #define NVME_CC_CSS_NVM    (0 << 4)  // I/O Command Set = NVM
 #define NVME_CC_MPS_SHIFT  7         // Memory Page Size
 #define NVME_CC_AMS_RR     (0 << 11) // Arbitration Mechanism = Round Robin
 #define NVME_CC_SHN_NONE   (0 << 14) // Shutdown Notification = None
 #define NVME_CC_IOSQES     (6 << 16) // I/O Submission Queue Entry Size (2^6 = 64 bytes)
 #define NVME_CC_IOCQES     (4 << 20) // I/O Completion Queue Entry Size (2^4 = 16 bytes)
 
 // Controller Status register bits
 #define NVME_CSTS_RDY      (1 << 0)  // Ready
 #define NVME_CSTS_CFS      (1 << 1)  // Controller Fatal Status
 #define NVME_CSTS_SHST_MASK (3 << 2) // Shutdown Status mask
 
 // Admin commands
 #define NVME_ADMIN_CMD_DELETE_SQ    0x00
 #define NVME_ADMIN_CMD_CREATE_SQ    0x01
 #define NVME_ADMIN_CMD_DELETE_CQ    0x04
 #define NVME_ADMIN_CMD_CREATE_CQ    0x05
 #define NVME_ADMIN_CMD_IDENTIFY     0x06
 #define NVME_ADMIN_CMD_ABORT        0x08
 #define NVME_ADMIN_CMD_SET_FEATURES 0x09
 #define NVME_ADMIN_CMD_GET_FEATURES 0x0A
 #define NVME_ADMIN_CMD_GET_LOG_PAGE 0x02
 
 // Maximum number of NVMe controllers we can handle
 #define MAX_NVME_CONTROLLERS 4
 
 // Admin queue sizes
 #define NVME_ADMIN_QUEUE_SIZE 32
 
 // Structure to hold queue information
 typedef struct {
     void* base_addr;
     uint16_t size;
     uint16_t head;
     uint16_t tail;
 } NVMeQueue;
 
 // Structure to hold controller information
 typedef struct {
     volatile uint8_t* base_addr;   // Memory-mapped registers
     uint32_t bar_size;             // Size of BAR
     bool is_initialized;           // Initialization status
     
     // Admin queues
     NVMeQueue admin_sq;
     NVMeQueue admin_cq;
     
     // Controller capabilities
     uint64_t cap;
     uint32_t max_entries;
     uint32_t doorbell_stride;
     uint8_t cc_en;
     
     // Controller info from identify command
     uint8_t identify_data[4096];
 } NVMeController;
 
 // Global array to store controller instances
 static NVMeController nvme_controllers[MAX_NVME_CONTROLLERS];
 static uint8_t num_controllers = 0;
 
 // Forward declarations
 static int nvme_initialize_controller(NVMeController* controller);
 static int nvme_identify_controller(NVMeController* controller);
 static int nvme_reset_controller(NVMeController* controller);
 static int nvme_configure_admin_queues(NVMeController* controller);
 static uint64_t nvme_read_cap(NVMeController* controller);
 static uint32_t nvme_read_vs(NVMeController* controller);
 static uint32_t nvme_read_csts(NVMeController* controller);
 static void nvme_write_cc(NVMeController* controller, uint32_t value);
 
 // Read a 32-bit value from controller register
 static inline uint32_t nvme_readl(const volatile void* addr) {
     return *((volatile uint32_t*)addr);
 }
 
 // Read a 64-bit value from controller register
 static inline uint64_t nvme_readq(const volatile void* addr) {
     return *((volatile uint64_t*)addr);
 }
 
 // Write a 32-bit value to controller register
 static inline void nvme_writel(volatile void* addr, uint32_t value) {
     *((volatile uint32_t*)addr) = value;
 }
 
 // Write a 64-bit value to controller register
 static inline void nvme_writeq(volatile void* addr, uint64_t value) {
     *((volatile uint64_t*)addr) = value;
 }
 
 // Initialize a new NVMe controller
 int nvme_add_controller(volatile uint8_t* base_addr, uint32_t bar_size) {
     if (num_controllers >= MAX_NVME_CONTROLLERS) {
         cout << "Error: Maximum number of NVMe controllers reached\n";
         return -1;
     }
     
     NVMeController* controller = &nvme_controllers[num_controllers];
     controller->base_addr = base_addr;
     controller->bar_size = bar_size;
     controller->is_initialized = false;
     
     int result = nvme_initialize_controller(controller);
     if (result == 0) {
         num_controllers++;
         cout << "NVMe controller " << (num_controllers - 1) << " initialized successfully\n";
         return num_controllers - 1;  // Return controller index
     } else {
         cout << "Error initializing NVMe controller: " << result << "\n";
         return -1;
     }
 }
 
 // Get the number of initialized controllers
 uint8_t nvme_get_controller_count() {
     return num_controllers;
 }
 
 // Initialize an NVMe controller
 static int nvme_initialize_controller(NVMeController* controller) {
     // Read controller capabilities
     controller->cap = nvme_read_cap(controller);
     controller->max_entries = (controller->cap & 0xFFFF) + 1;
     controller->doorbell_stride = 4 << (((controller->cap >> 32) & 0xF));
     cout.hex();
     uint32_t cap_high = (uint32_t)(controller->cap >> 32);
     uint32_t cap_low = (uint32_t)(controller->cap & 0xFFFFFFFF);
     cout << "NVMe Controller Capabilities: "  << cap_high << ":" << cap_low << "\n";
         cout.dec();
     cout << "Max Queue Entries: " << controller->max_entries << "\n";
     cout << "Doorbell Stride: " << controller->doorbell_stride << " bytes\n";
     
     // Read controller version
     uint32_t version = nvme_read_vs(controller);
     cout.hex();
     cout << "NVMe Version: " << version << "\n";
     
     // Reset the controller
     if (nvme_reset_controller(controller) != 0) {
         cout << "Failed to reset controller\n";
         return -1;
     }
     
     // Configure admin queues
     if (nvme_configure_admin_queues(controller) != 0) {
         cout << "Failed to configure admin queues\n";
         return -2;
     }
     
     // Enable the controller
     uint32_t cc = 0;
     cc |= NVME_CC_EN;              // Enable the controller
     cc |= NVME_CC_CSS_NVM;         // Set command set to NVM
     cc |= (0 << NVME_CC_MPS_SHIFT);// Set memory page size (4KB)
     cc |= NVME_CC_AMS_RR;          // Set arbitration mechanism to round robin
     cc |= NVME_CC_IOSQES;          // Set I/O submission queue entry size
     cc |= NVME_CC_IOCQES;          // Set I/O completion queue entry size
     
     nvme_write_cc(controller, cc);
     
     // Wait for controller to become ready
     uint32_t timeout = 500;  // 500 ms timeout
     while (timeout > 0) {
         uint32_t csts = nvme_read_csts(controller);
         if (csts & NVME_CSTS_RDY) {
             break;
         }
         
         // Wait 1ms
         // sleep(1);  // Implement appropriate sleep function
         timeout--;
     }
     
     if (timeout == 0) {
         cout << "Timeout waiting for controller to become ready\n";
         return -3;
     }
     
     // Identify the controller
     if (nvme_identify_controller(controller) != 0) {
         cout << "Failed to identify controller\n";
         return -4;
     }
     
     controller->is_initialized = true;
     return 0;
 }
 
 // Reset an NVMe controller
 static int nvme_reset_controller(NVMeController* controller) {
     // Read the current controller configuration
     volatile uint8_t* cc_addr = controller->base_addr + NVME_REG_CC;
     uint32_t cc = nvme_readl(cc_addr);
     
     // If the controller is already enabled, disable it first
     if (cc & NVME_CC_EN) {
         // Clear the enable bit
         cc &= ~NVME_CC_EN;
         nvme_writel(cc_addr, cc);
         
         // Wait for controller to transition to disabled state
         uint32_t timeout = 500;  // 500 ms timeout
         while (timeout > 0) {
             uint32_t csts = nvme_read_csts(controller);
             if (!(csts & NVME_CSTS_RDY)) {
                 break;
             }
             
             // Wait 1ms
             // sleep(1);  // Implement appropriate sleep function
             timeout--;
         }
         
         if (timeout == 0) {
             cout << "Timeout waiting for controller to disable\n";
             return -1;
         }
     }
     
     return 0;
 }
 
 // Configure admin queues
 static int nvme_configure_admin_queues(NVMeController* controller) {
     // Allocate memory for admin submission queue (page-aligned)
     controller->admin_sq.base_addr = (void*)0x10000;  // Example address, should be allocated dynamically
     controller->admin_sq.size = NVME_ADMIN_QUEUE_SIZE;
     controller->admin_sq.head = 0;
     controller->admin_sq.tail = 0;
     
     // Allocate memory for admin completion queue (page-aligned)
     controller->admin_cq.base_addr = (void*)0x11000;  // Example address, should be allocated dynamically
     controller->admin_cq.size = NVME_ADMIN_QUEUE_SIZE;
     controller->admin_cq.head = 0;
     controller->admin_cq.tail = 0;
     
     // Set up admin queue attributes
     uint32_t aqa = 0;
     aqa |= (controller->admin_sq.size - 1) << 0;   // Submission queue size
     aqa |= (controller->admin_cq.size - 1) << 16;  // Completion queue size
     
     volatile uint8_t* aqa_addr = controller->base_addr + NVME_REG_AQA;
     nvme_writel(aqa_addr, aqa);
     
     // Set submission queue base address
     volatile uint8_t* asq_addr = controller->base_addr + NVME_REG_ASQ;
     nvme_writeq(asq_addr, (uint64_t)controller->admin_sq.base_addr);
     
     // Set completion queue base address
     volatile uint8_t* acq_addr = controller->base_addr + NVME_REG_ACQ;
     nvme_writeq(acq_addr, (uint64_t)controller->admin_cq.base_addr);
     
     return 0;
 }
 
 // Send identify command to controller
 static int nvme_identify_controller(NVMeController* controller) {
     // This would implement the identify command
     // For simplicity, we're just setting a success value
     // In a real implementation, you would:
     // 1. Create an identify command
     // 2. Submit it to the admin submission queue
     // 3. Ring the doorbell
     // 4. Wait for completion
     // 5. Process the identify data
     
     // Placeholder for identify data
     memset(controller->identify_data, 0, sizeof(controller->identify_data));
     
     // Example implementation would extract model name and other info
     // and display it in the terminal
     
     cout << "NVMe Controller Identified\n";
     cout << "Model: ACME NVMe SSD\n";  // Placeholder
     cout << "Serial: NVME123456789\n"; // Placeholder
     cout << "Firmware: 1.0\n";        // Placeholder
     
     return 0;
 }
 
 // Read controller capabilities register
 static uint64_t nvme_read_cap(NVMeController* controller) {
     volatile uint8_t* cap_addr = controller->base_addr + NVME_REG_CAP;
     return nvme_readq(cap_addr);
 }
 
 // Read controller version register
 static uint32_t nvme_read_vs(NVMeController* controller) {
     volatile uint8_t* vs_addr = controller->base_addr + NVME_REG_VS;
     return nvme_readl(vs_addr);
 }
 
 // Read controller status register
 static uint32_t nvme_read_csts(NVMeController* controller) {
     volatile uint8_t* csts_addr = controller->base_addr + NVME_REG_CSTS;
     return nvme_readl(csts_addr);
 }
 
 // Write to controller configuration register
 static void nvme_write_cc(NVMeController* controller, uint32_t value) {
     volatile uint8_t* cc_addr = controller->base_addr + NVME_REG_CC;
     nvme_writel(cc_addr, value);
 }
 
 // Display information about all initialized controllers
 void nvme_display_controllers() {
    cout.dec();
     cout << "NVMe Controllers (" << (int)num_controllers << " found):\n";
     
     for (int i = 0; i < num_controllers; i++) {
         NVMeController* controller = &nvme_controllers[i];
         
         cout << "Controller " << i << ":\n";
         cout.hex();
         cout << "  Base Address: " << (uint32_t)controller->base_addr  << "\n";
         cout << "  Status: " << (controller->is_initialized ? "Initialized" : "Not initialized") << "\n";
         
         if (controller->is_initialized) {
             // Display additional information extracted from identify data
             cout << "  Model: ACME NVMe SSD\n";  // Placeholder
             cout << "  Capacity: 1 TB\n";         // Placeholder
             cout << "  Max Transfer Size: 256 KB\n"; // Placeholder
         }
         
         cout << "\n";
     }
 }
 
 // Scan PCI bus for NVMe controllers and initialize them
 int nvme_scan_and_initialize() {
     cout << "Scanning for NVMe controllers...\n";
     
     // In a real implementation, this would scan the PCI bus for devices
     // with class code 0x01 (mass storage) and subclass 0x08 (NVM controller)
     
     // For demonstration, we'll pretend we found one controller
     volatile uint8_t* base_addr = (volatile uint8_t*)0xC0000000;  // Example MMIO base address
     uint32_t bar_size = 0x1000;  // 4KB of MMIO space
     
     int controller_id = nvme_add_controller(base_addr, bar_size);
     
     if (controller_id >= 0) {
         cout.hex();
         cout << "Found and initialized NVMe controller at " ;
         cout << (uint32_t)base_addr << "\n";
     } else {
         cout << "Failed to initialize NVMe controller\n";
     }
     
     return num_controllers;
 }
 
 // Create a corresponding header file for this implementation
 // This would go in nvme_config.h