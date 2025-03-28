#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>


/* NVMe Bare Metal Driver with Static Variables */

/* Static PCIe configuration */
static const uint64_t PCIE_CONFIG_BASE_ADDR = 0xE0000000;
static const uint8_t NVME_CLASS_CODE = 0x01;
static const uint8_t NVME_SUBCLASS = 0x08;
static const uint8_t NVME_PROG_IF = 0x02;

/* Static NVMe register offsets */
static const uint32_t NVME_REG_CAP_LO = 0x00;
static const uint32_t NVME_REG_CAP_HI = 0x04;
static const uint32_t NVME_REG_VS = 0x08;
static const uint32_t NVME_REG_CC = 0x14;
static const uint32_t NVME_REG_CSTS = 0x1C;
static const uint32_t NVME_REG_AQA = 0x24;
static const uint32_t NVME_REG_ASQ = 0x28;
static const uint32_t NVME_REG_ACQ = 0x30;
static const uint32_t NVME_REG_SQ0TDBL = 0x1000;
static const uint32_t NVME_REG_CQ0HDBL = 0x1004;

/* Static NVMe command opcodes */
static const uint8_t NVME_CMD_WRITE = 0x01;
static const uint8_t NVME_CMD_READ = 0x02;
static const uint8_t NVME_ADMIN_CMD_IDENTIFY = 0x06;
static const uint8_t NVME_ADMIN_CMD_CREATE_CQ = 0x05;
static const uint8_t NVME_ADMIN_CMD_CREATE_SQ = 0x01;

/* Static NVMe completion status */
static const uint16_t NVME_SC_SUCCESS = 0x0;

/* Static submission queue entry offsets */
static const uint32_t SQE_OPCODE_OFFSET = 0;
static const uint32_t SQE_FLAGS_OFFSET = 1;
static const uint32_t SQE_COMMAND_ID_OFFSET = 2;
static const uint32_t SQE_NSID_OFFSET = 4;
static const uint32_t SQE_PRP1_OFFSET = 24;
static const uint32_t SQE_PRP2_OFFSET = 32;
static const uint32_t SQE_CDW10_OFFSET = 40;
static const uint32_t SQE_CDW11_OFFSET = 44;
static const uint32_t SQE_SLBA_OFFSET = 40;
static const uint32_t SQE_NLB_OFFSET = 48;
static const uint32_t SQE_SIZE = 64;

/* Static completion queue entry offsets */
static const uint32_t CQE_RESULT_OFFSET = 0;
static const uint32_t CQE_SQ_HEAD_OFFSET = 8;
static const uint32_t CQE_SQ_ID_OFFSET = 10;
static const uint32_t CQE_COMMAND_ID_OFFSET = 12;
static const uint32_t CQE_STATUS_OFFSET = 14;
static const uint32_t CQE_SIZE = 16;

/* Static configuration parameters */
static const uint32_t ADMIN_QUEUE_SIZE = 64;
static const uint32_t IO_QUEUE_SIZE = 1024;
static const uint32_t PAGE_SIZE = 4096;
static const uint32_t NUM_IO_QUEUES = 4;
static const uint32_t SECTOR_SIZE = 512;
static const uint32_t COMMAND_TIMEOUT_MS = 5000;

/* Static memory for NVMe controller mapping */
static volatile uint32_t* nvme_regs = NULL;
static uint32_t db_stride = 0;

/* Instead of using variables for these sizes, use #define constants */
#define ADMIN_SQ_BUFFER_SIZE (64 * 64)  /* 64 entries, 64 bytes each */
#define ADMIN_CQ_BUFFER_SIZE (64 * 16)  /* 64 entries, 16 bytes each */

/* Now declare the buffers with fixed sizes */
__attribute__((aligned(4096)))
static uint8_t admin_sq_buffer[ADMIN_SQ_BUFFER_SIZE];

__attribute__((aligned(4096)))
static uint8_t admin_cq_buffer[ADMIN_CQ_BUFFER_SIZE];

/* Static queue state variables */
static uint32_t admin_sq_tail = 0;
static uint32_t admin_sq_head = 0;
static uint32_t admin_cq_head = 0;
static uint32_t admin_cq_phase = 1;
static uint16_t next_cmd_id = 0;

#define IO_SQ_ENTRY_SIZE 64  // 64 bytes per submission queue entry
#define IO_CQ_ENTRY_SIZE 16  // 16 bytes per completion queue entry
#define IO_SQ_BUFFER_SIZE (IO_QUEUE_SIZE * IO_SQ_ENTRY_SIZE)
#define IO_CQ_BUFFER_SIZE (IO_QUEUE_SIZE * IO_CQ_ENTRY_SIZE)

/* Static memory for I/O queues (simplified example with just one I/O queue pair) */
/* Make sure IO_QUEUE_SIZE is defined with #define, not as a variable */
#define IO_QUEUE_SIZE 1024

/* Define entry sizes */
#define SQ_ENTRY_SIZE 64
#define CQ_ENTRY_SIZE 16

/* Define total buffer sizes using preprocessor calculations */
#define IO_SQ_BUFFER_SIZE (IO_QUEUE_SIZE * SQ_ENTRY_SIZE)
#define IO_CQ_BUFFER_SIZE (IO_QUEUE_SIZE * CQ_ENTRY_SIZE)

/* Static memory for I/O queues */
__attribute__((aligned(4096)))
static uint8_t io_sq_buffer[IO_SQ_BUFFER_SIZE];

__attribute__((aligned(4096)))
static uint8_t io_cq_buffer[IO_CQ_BUFFER_SIZE];

/* Static I/O queue state variables */
static uint32_t io_sq_tail = 0;
static uint32_t io_sq_head = 0;
static uint32_t io_cq_head = 0;
static uint32_t io_cq_phase = 1;

/* Static data buffer for read/write operations */
__attribute__((aligned(4096)))
static uint8_t data_buffer[4096];

/* Static identify controller data buffer */
__attribute__((aligned(4096)))
static uint8_t identify_data[4096];

/* Get PCIe config address for a given device, function, and offset */
static void* pcie_get_config_address(uint8_t bus, uint8_t device, 
                                    uint8_t function, uint16_t offset) {
    return (void*)(PCIE_CONFIG_BASE_ADDR | 
                  ((uint32_t)bus << 20) | 
                  ((uint32_t)device << 15) | 
                  ((uint32_t)function << 12) | 
                  (offset & 0xFFF));
}

/* Read a 32-bit value from PCIe configuration space */
static uint32_t pcie_config_read_dword(uint8_t bus, uint8_t device, 
                                      uint8_t function, uint16_t offset) {
    uint32_t* addr = (uint32_t*)pcie_get_config_address(bus, device, function, offset);
    return *addr;
}

/* Write a 32-bit value to PCIe configuration space */
static void pcie_config_write_dword(uint8_t bus, uint8_t device, 
                                   uint8_t function, uint16_t offset, uint32_t value) {
    uint32_t* addr = (uint32_t*)pcie_get_config_address(bus, device, function, offset);
    *addr = value;
}

/* Enable bus mastering for the PCIe device */
static void pcie_enable_device(uint8_t bus, uint8_t device, uint8_t function) {
    uint32_t command = pcie_config_read_dword(bus, device, function, 0x04);
    command |= (1 << 2) | (1 << 1); // Bus Master and Memory Space Enable
    pcie_config_write_dword(bus, device, function, 0x04, command);
}

/* Get the base address of BAR0 for the NVMe device */
static uint64_t pcie_get_nvme_bar_address(uint8_t bus, uint8_t device, uint8_t function) {
    uint32_t bar0_low = pcie_config_read_dword(bus, device, function, 0x10);
    uint32_t bar0_high = pcie_config_read_dword(bus, device, function, 0x14);
    return ((uint64_t)bar0_high << 32) | (bar0_low & ~0xF);
}

/* Reset and enable the NVMe controller */
static int nvme_reset_and_enable_controller(void) {
    uint64_t cap;
    uint32_t cc, csts;
    uint32_t timeout_ms = COMMAND_TIMEOUT_MS;
    
    /* Read controller capabilities */
    cap = ((uint64_t)nvme_regs[NVME_REG_CAP_HI/4] << 32) | nvme_regs[NVME_REG_CAP_LO/4];
    db_stride = (cap >> 32) & 0xF;
    
    /* Disable the controller */
    cc = nvme_regs[NVME_REG_CC/4];
    cc &= ~0x1;  // Clear Enable bit
    nvme_regs[NVME_REG_CC/4] = cc;
    
    /* Wait for controller to become disabled */
    do {
        csts = nvme_regs[NVME_REG_CSTS/4];
        if ((csts & 0x1) == 0) break;
        // In a real implementation, add a delay here
    } while (timeout_ms-- > 0);
    
    if (timeout_ms == 0) {
        // Controller failed to disable
        return -1;
    }
    
    /* Configure controller parameters */
    cc = 0;
    cc |= (0 << 4);  // I/O Command Set: NVM Command Set
    cc |= (4 << 7);  // CQE Size: 16 bytes (2^4)
    cc |= (6 << 11); // SQE Size: 64 bytes (2^6)
    cc |= (0 << 14); // Memory Page Size: 4KB (2^12)
    cc |= (0 << 16); // Arbitration Mechanism: Round Robin
    
    /* Write the configuration */
    nvme_regs[NVME_REG_CC/4] = cc;
    
    /* Set up Admin Queue Attributes */
    uint32_t aqa = ((ADMIN_QUEUE_SIZE - 1) << 16) | (ADMIN_QUEUE_SIZE - 1);
    nvme_regs[NVME_REG_AQA/4] = aqa;
    
    /* Set Admin Submission Queue Base Address */
    uint64_t admin_sq_addr = (uint64_t)admin_sq_buffer;
    nvme_regs[NVME_REG_ASQ/4] = (uint32_t)admin_sq_addr;
    nvme_regs[(NVME_REG_ASQ/4) + 1] = (uint32_t)(admin_sq_addr >> 32);
    
    /* Set Admin Completion Queue Base Address */
    uint64_t admin_cq_addr = (uint64_t)admin_cq_buffer;
    nvme_regs[NVME_REG_ACQ/4] = (uint32_t)admin_cq_addr;
    nvme_regs[(NVME_REG_ACQ/4) + 1] = (uint32_t)(admin_cq_addr >> 32);
    
    /* Enable the controller */
    cc |= 0x1;  // Set Enable bit
    nvme_regs[NVME_REG_CC/4] = cc;
    
    /* Wait for controller to become ready */
    timeout_ms = COMMAND_TIMEOUT_MS;
    do {
        csts = nvme_regs[NVME_REG_CSTS/4];
        if (csts & 0x1) break;
        // In a real implementation, add a delay here
    } while (timeout_ms-- > 0);
    
    if (timeout_ms == 0) {
        // Controller failed to enable
        return -2;
    }
    
    return 0;
}

/* Get doorbell register address for a queue */
static volatile uint32_t* nvme_get_doorbell(uint16_t queue_id, bool is_sq) {
    uint32_t offset = NVME_REG_SQ0TDBL;
    
    if (!is_sq) {
        offset = NVME_REG_CQ0HDBL;
    }
    
    if (queue_id > 0) {
        offset += (2 * queue_id) * (4 << db_stride);
    }
    
    return &nvme_regs[offset/4];
}

/* Ring submission queue doorbell */
static void nvme_ring_sq_doorbell(uint16_t sq_id, uint32_t tail) {
    volatile uint32_t* doorbell = nvme_get_doorbell(sq_id, true);
    *doorbell = tail;
}

/* Ring completion queue doorbell */
static void nvme_ring_cq_doorbell(uint16_t cq_id, uint32_t head) {
    volatile uint32_t* doorbell = nvme_get_doorbell(cq_id, false);
    *doorbell = head;
}

/* Submit an identify controller command to the admin queue */
static void nvme_submit_identify_controller(void) {
    uint8_t* sq_entry = admin_sq_buffer + (admin_sq_tail * SQE_SIZE);
    
    /* Clear the entry */
    for (int i = 0; i < SQE_SIZE; i++) {
        sq_entry[i] = 0;
    }
    
    /* Set up identify command */
    sq_entry[SQE_OPCODE_OFFSET] = NVME_ADMIN_CMD_IDENTIFY;
    sq_entry[SQE_FLAGS_OFFSET] = 0;
    
    /* Assign command ID */
    uint16_t cmd_id = next_cmd_id++;
    sq_entry[SQE_COMMAND_ID_OFFSET] = cmd_id & 0xFF;
    sq_entry[SQE_COMMAND_ID_OFFSET + 1] = (cmd_id >> 8) & 0xFF;
    
    /* Set namespace ID to 0 (not targeting a specific namespace) */
    sq_entry[SQE_NSID_OFFSET] = 0;
    sq_entry[SQE_NSID_OFFSET + 1] = 0;
    sq_entry[SQE_NSID_OFFSET + 2] = 0;
    sq_entry[SQE_NSID_OFFSET + 3] = 0;
    
    /* Set PRP1 to identify data buffer */
    uint64_t identify_addr = (uint64_t)identify_data;
    for (int i = 0; i < 8; i++) {
        sq_entry[SQE_PRP1_OFFSET + i] = (identify_addr >> (i * 8)) & 0xFF;
    }
    
    /* Command Dword 10: Identify Controller structure (CNS=1) */
    sq_entry[SQE_CDW10_OFFSET] = 1;
    sq_entry[SQE_CDW10_OFFSET + 1] = 0;
    sq_entry[SQE_CDW10_OFFSET + 2] = 0;
    sq_entry[SQE_CDW10_OFFSET + 3] = 0;
    
    /* Update submission queue tail */
    admin_sq_tail = (admin_sq_tail + 1) % ADMIN_QUEUE_SIZE;
    
    /* Ring doorbell */
    nvme_ring_sq_doorbell(0, admin_sq_tail);
}

/* Wait for completion of a command with given command ID */
static int nvme_wait_for_completion(uint16_t cq_id, uint16_t cmd_id) {
    uint8_t* cq_buffer = (cq_id == 0) ? admin_cq_buffer : io_cq_buffer;
    uint32_t* cq_head_ptr = (cq_id == 0) ? &admin_cq_head : &io_cq_head;
    uint32_t* cq_phase_ptr = (cq_id == 0) ? &admin_cq_phase : &io_cq_phase;
    uint32_t cq_size = (cq_id == 0) ? ADMIN_QUEUE_SIZE : IO_QUEUE_SIZE;
    
    uint32_t timeout = COMMAND_TIMEOUT_MS;
    
    while (timeout-- > 0) {
        uint8_t* cqe = cq_buffer + (*cq_head_ptr * CQE_SIZE);
        
        /* Check phase bit to see if entry is valid */
        uint16_t status = *((uint16_t*)(cqe + CQE_STATUS_OFFSET));
        
        if ((status & 0x1) == *cq_phase_ptr) {
            /* Extract command ID */
            uint16_t entry_cmd_id = *((uint16_t*)(cqe + CQE_COMMAND_ID_OFFSET));
            
            if (entry_cmd_id == cmd_id) {
                /* Extract status code */
                uint16_t status_code = (status >> 1) & 0xFF;
                
                /* Update completion queue head */
                *cq_head_ptr = (*cq_head_ptr + 1) % cq_size;
                
                /* Update phase bit if we wrapped around */
                if (*cq_head_ptr == 0) {
                    *cq_phase_ptr = !(*cq_phase_ptr);
                }
                
                /* Ring the doorbell */
                nvme_ring_cq_doorbell(cq_id, *cq_head_ptr);
                
                /* Return status */
                return (status_code == NVME_SC_SUCCESS) ? 0 : -status_code;
            }
        }
        
        /* In a real implementation, add a delay here */
    }
    
    /* Timeout occurred */
    return -1;
}

/* Create I/O completion queue */
static int nvme_create_io_completion_queue(void) {
    uint8_t* sq_entry = admin_sq_buffer + (admin_sq_tail * SQE_SIZE);
    
    /* Clear the entry */
    for (int i = 0; i < SQE_SIZE; i++) {
        sq_entry[i] = 0;
    }
    
    /* Set up command */
    sq_entry[SQE_OPCODE_OFFSET] = NVME_ADMIN_CMD_CREATE_CQ;
    sq_entry[SQE_FLAGS_OFFSET] = 0;
    
    /* Assign command ID */
    uint16_t cmd_id = next_cmd_id++;
    sq_entry[SQE_COMMAND_ID_OFFSET] = cmd_id & 0xFF;
    sq_entry[SQE_COMMAND_ID_OFFSET + 1] = (cmd_id >> 8) & 0xFF;
    
    /* Set PRP1 to completion queue buffer */
    uint64_t cq_addr = (uint64_t)io_cq_buffer;
    for (int i = 0; i < 8; i++) {
        sq_entry[SQE_PRP1_OFFSET + i] = (cq_addr >> (i * 8)) & 0xFF;
    }
    
    /* Command Dword 10: Queue Size and Queue ID */
    uint32_t cdw10 = ((IO_QUEUE_SIZE - 1) & 0xFFFF) | (1 << 16);  /* Queue ID 1 */
    sq_entry[SQE_CDW10_OFFSET] = cdw10 & 0xFF;
    sq_entry[SQE_CDW10_OFFSET + 1] = (cdw10 >> 8) & 0xFF;
    sq_entry[SQE_CDW10_OFFSET + 2] = (cdw10 >> 16) & 0xFF;
    sq_entry[SQE_CDW10_OFFSET + 3] = (cdw10 >> 24) & 0xFF;
    
    /* Command Dword 11: Physically Contiguous, Interrupts Enabled */
    uint32_t cdw11 = (1 << 0) | (1 << 1);
    sq_entry[SQE_CDW11_OFFSET] = cdw11 & 0xFF;
    sq_entry[SQE_CDW11_OFFSET + 1] = (cdw11 >> 8) & 0xFF;
    sq_entry[SQE_CDW11_OFFSET + 2] = (cdw11 >> 16) & 0xFF;
    sq_entry[SQE_CDW11_OFFSET + 3] = (cdw11 >> 24) & 0xFF;
    
    /* Update submission queue tail */
    admin_sq_tail = (admin_sq_tail + 1) % ADMIN_QUEUE_SIZE;
    
    /* Ring doorbell */
    nvme_ring_sq_doorbell(0, admin_sq_tail);
    
    /* Wait for completion */
    return nvme_wait_for_completion(0, cmd_id);
}

/* Create I/O submission queue */
static int nvme_create_io_submission_queue(void) {
    uint8_t* sq_entry = admin_sq_buffer + (admin_sq_tail * SQE_SIZE);
    
    /* Clear the entry */
    for (int i = 0; i < SQE_SIZE; i++) {
        sq_entry[i] = 0;
    }
    
    /* Set up command */
    sq_entry[SQE_OPCODE_OFFSET] = NVME_ADMIN_CMD_CREATE_SQ;
    sq_entry[SQE_FLAGS_OFFSET] = 0;
    
    /* Assign command ID */
    uint16_t cmd_id = next_cmd_id++;
    sq_entry[SQE_COMMAND_ID_OFFSET] = cmd_id & 0xFF;
    sq_entry[SQE_COMMAND_ID_OFFSET + 1] = (cmd_id >> 8) & 0xFF;
    
    /* Set PRP1 to submission queue buffer */
    uint64_t sq_addr = (uint64_t)io_sq_buffer;
    for (int i = 0; i < 8; i++) {
        sq_entry[SQE_PRP1_OFFSET + i] = (sq_addr >> (i * 8)) & 0xFF;
    }
    
    /* Command Dword 10: Queue Size and Queue ID */
    uint32_t cdw10 = ((IO_QUEUE_SIZE - 1) & 0xFFFF) | (1 << 16);  /* Queue ID 1 */
    sq_entry[SQE_CDW10_OFFSET] = cdw10 & 0xFF;
    sq_entry[SQE_CDW10_OFFSET + 1] = (cdw10 >> 8) & 0xFF;
    sq_entry[SQE_CDW10_OFFSET + 2] = (cdw10 >> 16) & 0xFF;
    sq_entry[SQE_CDW10_OFFSET + 3] = (cdw10 >> 24) & 0xFF;
    
    /* Command Dword 11: Physically Contiguous, Medium Priority, and CQ ID */
    uint32_t cdw11 = (1 << 0) | (1 << 1) | (1 << 16);  /* CQ ID 1 */
    sq_entry[SQE_CDW11_OFFSET] = cdw11 & 0xFF;
    sq_entry[SQE_CDW11_OFFSET + 1] = (cdw11 >> 8) & 0xFF;
    sq_entry[SQE_CDW11_OFFSET + 2] = (cdw11 >> 16) & 0xFF;
    sq_entry[SQE_CDW11_OFFSET + 3] = (cdw11 >> 24) & 0xFF;
    
    /* Update submission queue tail */
    admin_sq_tail = (admin_sq_tail + 1) % ADMIN_QUEUE_SIZE;
    
    /* Ring doorbell */
    nvme_ring_sq_doorbell(0, admin_sq_tail);
    
    /* Wait for completion */
    return nvme_wait_for_completion(0, cmd_id);
}

/* Submit a write command to the I/O queue */
static int nvme_write_data(uint64_t lba, uint32_t num_blocks, const void* data) {
    uint8_t* sq_entry = io_sq_buffer + (io_sq_tail * SQE_SIZE);
    
    /* Clear the entry */
    for (int i = 0; i < SQE_SIZE; i++) {
        sq_entry[i] = 0;
    }
    
    /* Set up command */
    sq_entry[SQE_OPCODE_OFFSET] = NVME_CMD_WRITE;
    sq_entry[SQE_FLAGS_OFFSET] = 0;
    
    /* Assign command ID */
    uint16_t cmd_id = next_cmd_id++;
    sq_entry[SQE_COMMAND_ID_OFFSET] = cmd_id & 0xFF;
    sq_entry[SQE_COMMAND_ID_OFFSET + 1] = (cmd_id >> 8) & 0xFF;
    
    /* Set namespace ID to 1 */
    sq_entry[SQE_NSID_OFFSET] = 1;
    
    /* Copy data to data buffer */
    for (uint32_t i = 0; i < num_blocks * SECTOR_SIZE; i++) {
        data_buffer[i] = ((const uint8_t*)data)[i];
    }
    
    /* Set PRP1 to data buffer */
    uint64_t data_addr = (uint64_t)data_buffer;
    for (int i = 0; i < 8; i++) {
        sq_entry[SQE_PRP1_OFFSET + i] = (data_addr >> (i * 8)) & 0xFF;
    }
    
    /* Set starting LBA */
    for (int i = 0; i < 8; i++) {
        sq_entry[SQE_SLBA_OFFSET + i] = (lba >> (i * 8)) & 0xFF;
    }
    
    /* Set number of logical blocks (0-based) */
    sq_entry[SQE_NLB_OFFSET] = (num_blocks - 1) & 0xFF;
    sq_entry[SQE_NLB_OFFSET + 1] = ((num_blocks - 1) >> 8) & 0xFF;
    
    /* Update submission queue tail */
    io_sq_tail = (io_sq_tail + 1) % IO_QUEUE_SIZE;
    
    /* Ring doorbell */
    nvme_ring_sq_doorbell(1, io_sq_tail);
    
    /* Wait for completion */
    return nvme_wait_for_completion(1, cmd_id);
}

/* Submit a read command to the I/O queue */
static int nvme_read_data(uint64_t lba, uint32_t num_blocks, void* data) {
    uint8_t* sq_entry = io_sq_buffer + (io_sq_tail * SQE_SIZE);
    
    /* Clear the entry */
    for (int i = 0; i < SQE_SIZE; i++) {
        sq_entry[i] = 0;
    }
    
    /* Set up command */
    sq_entry[SQE_OPCODE_OFFSET] = NVME_CMD_READ;
    sq_entry[SQE_FLAGS_OFFSET] = 0;
    
    /* Assign command ID */
    uint16_t cmd_id = next_cmd_id++;
    sq_entry[SQE_COMMAND_ID_OFFSET] = cmd_id & 0xFF;
    sq_entry[SQE_COMMAND_ID_OFFSET + 1] = (cmd_id >> 8) & 0xFF;
    
    /* Set namespace ID to 1 */
    sq_entry[SQE_NSID_OFFSET] = 1;
    
    /* Set PRP1 to data buffer */
    uint64_t data_addr = (uint64_t)data_buffer;
    for (int i = 0; i < 8; i++) {
        sq_entry[SQE_PRP1_OFFSET + i] = (data_addr >> (i * 8)) & 0xFF;
    }
    
    /* Set starting LBA */
    for (int i = 0; i < 8; i++) {
        sq_entry[SQE_SLBA_OFFSET + i] = (lba >> (i * 8)) & 0xFF;
    }
    
    /* Set number of logical blocks (0-based) */
    sq_entry[SQE_NLB_OFFSET] = (num_blocks - 1) & 0xFF;
    sq_entry[SQE_NLB_OFFSET + 1] = ((num_blocks - 1) >> 8) & 0xFF;
    
    /* Update submission queue tail */
    io_sq_tail = (io_sq_tail + 1) % IO_QUEUE_SIZE;
    
    /* Ring doorbell */
    nvme_ring_sq_doorbell(1, io_sq_tail);
    
    /* Wait for completion */
    int ret = nvme_wait_for_completion(1, cmd_id);
    
    /* Copy data from data buffer to output buffer */
    if (ret == 0) {
        for (uint32_t i = 0; i < num_blocks * SECTOR_SIZE; i++) {
            ((uint8_t*)data)[i] = data_buffer[i];
        }
    }
    
    return ret;
}

/* Find and initialize an NVMe device */
static int nvme_init(uint8_t* bus, uint8_t* device, uint8_t* function) {
    /* Search for NVMe device */
    for (uint8_t b = 0; b < 255; b++) {
        for (uint8_t d = 0; d < 32; d++) {
            for (uint8_t f = 0; f < 8; f++) {
                uint32_t* addr = (uint32_t*)pcie_get_config_address(b, d, f, 0);
                uint32_t device_vendor = *addr;
                
                if (device_vendor == 0xFFFFFFFF || device_vendor == 0) {
                    continue;
                }
                
                /* Check class code, subclass, and prog_if */
                uint32_t class_rev = pcie_config_read_dword(b, d, f, 0x08);
                uint8_t class_code = (class_rev >> 24) & 0xFF;
                uint8_t subclass = (class_rev >> 16) & 0xFF;
                uint8_t prog_if = (class_rev >> 8) & 0xFF;
                
                if (class_code == NVME_CLASS_CODE && 
                    subclass == NVME_SUBCLASS && 
                    prog_if == NVME_PROG_IF) {
                    *bus = b;
                    *device = d;
                    *function = f;
                    return 0;
                }
            }
        }
    }
    
    return -1;
}

/* Initialize NVMe controller and queues */
static int nvme_init_controller(uint8_t bus, uint8_t device, uint8_t function) {
    /* Enable PCIe device */
    pcie_enable_device(bus, device, function);
    
    /* Map BAR0 */
    uint64_t bar0_addr = pcie_get_nvme_bar_address(bus, device, function);
    if (bar0_addr == 0) {
        return -1;
    }
    
    /* Set register base pointer */
    nvme_regs = (volatile uint32_t*)bar0_addr;
    
    /* Clear queue buffers */
    for (uint32_t i = 0; i < ADMIN_QUEUE_SIZE * 64; i++) {
        admin_sq_buffer[i] = 0;
    }
    
    for (uint32_t i = 0; i < ADMIN_QUEUE_SIZE * 16; i++) {
        admin_cq_buffer[i] = 0;
    }
    
    for (uint32_t i = 0; i < IO_QUEUE_SIZE * 64; i++) {
        io_sq_buffer[i] = 0;
    }
    
    for (uint32_t i = 0; i < IO_QUEUE_SIZE * 16; i++) {
        io_cq_buffer[i] = 0;
    }
    
    /* Reset queue variables */
    admin_sq_head = 0;
    admin_sq_tail = 0;
    admin_cq_head = 0;
    admin_cq_phase = 1;
    io_sq_head = 0;
    io_sq_tail = 0;
    io_cq_head = 0;
    io_cq_phase = 1;
    next_cmd_id = 0;
    
    /* Reset and enable controller */
    if (nvme_reset_and_enable_controller() != 0) {
        return -2;
    }
    
    /* Submit identify controller command */
    nvme_submit_identify_controller();
    
    /* Wait for completion */
    if (nvme_wait_for_completion(0, 0) != 0) {
        return -3;
    }
    
    /* Create I/O completion queue */
    if (nvme_create_io_completion_queue() != 0) {
        return -4;
    }
    
    /* Create I/O submission queue */
    if (nvme_create_io_submission_queue() != 0) {
        return -5;
    }
    
    return 0;
}

/* Write a block of data to the NVMe device */
static int nvme_write_block(uint64_t lba, const void* data, uint32_t num_blocks) {
    /* Validate parameters */
    if (num_blocks == 0 || num_blocks > (PAGE_SIZE / SECTOR_SIZE)) {
        return -1;
    }
    
    return nvme_write_data(lba, num_blocks, data);
}

/* Read a block of data from the NVMe device */
static int nvme_read_block(uint64_t lba, void* data, uint32_t num_blocks) {
    /* Validate parameters */
    if (num_blocks == 0 || num_blocks > (PAGE_SIZE / SECTOR_SIZE)) {
        return -1;
    }
    
    return nvme_read_data(lba, num_blocks, data);
}
/* Main entry point for NVMe driver */
int nvme_test() {
    static uint8_t bus, device, function;
    static uint8_t test_pattern[512];
    static uint8_t read_buffer[512];
    int ret;
    
    printf("Starting NVMe test...\n");
    
    /* Initialize test pattern */
    for (int i = 0; i < 512; i++) {
        test_pattern[i] = i & 0xFF;
    }
    printf("Test pattern initialized\n");
    
    /* Find NVMe device */
    printf("Searching for NVMe device...\n");
    ret = nvme_init(&bus, &device, &function);
    if (ret != 0) {
        printf("Failed to find NVMe device, error code: %d\n", ret);
        return -1;
    }
    printf("NVMe device found at %02X:%02X.%X\n", bus, device, function);
    
    /* Initialize NVMe controller */
    printf("Initializing NVMe controller...\n");
    ret = nvme_init_controller(bus, device, function);
    if (ret != 0) {
        printf("Failed to initialize NVMe controller, error code: %d\n", ret);
        return -2;
    }
    printf("NVMe controller initialized successfully\n");
    
    /* Write a block of data */
    printf("Writing test data to LBA 0...\n");
    ret = nvme_write_block(0, test_pattern, 1);
    if (ret != 0) {
        printf("Write operation failed, error code: %d\n", ret);
        return -3;
    }
    printf("Write operation successful\n");
    
    /* Read back the data */
    printf("Reading data from LBA 0...\n");
    ret = nvme_read_block(0, read_buffer, 1);
    if (ret != 0) {
        printf("Read operation failed, error code: %d\n", ret);
        return -4;
    }
    printf("Read operation successful\n");
    
    /* Verify data */
    printf("Verifying data integrity...\n");
    for (int i = 0; i < 512; i++) {
        if (read_buffer[i] != test_pattern[i]) {
            printf("Data verification failed at offset %d: expected 0x%02X, got 0x%02X\n", 
                   i, test_pattern[i], read_buffer[i]);
            return -5;
        }
    }
    printf("Data verification successful - all bytes match\n");
    
    printf("NVMe test completed successfully\n");
    return 0;
}

/* Alternative simple NVMe write using static inline implementation */
static inline int nvme_write_inline(uint64_t lba, const void* data, uint32_t size) {
    printf("nvme_write_inline: Writing %u bytes to LBA 0x%lx\n", size, lba);
    
    static const uint32_t CMD_SIZE = 64;                   // NVMe command size
    static const uint8_t OPCODE_WRITE = 0x01;              // NVMe write opcode
    static const uint32_t NVME_REG_SQ0TDBL = 0x1000;      // SQ0 Tail Doorbell offset
    
    static volatile uint32_t* nvme_base = (volatile uint32_t*)0xF0000000; // Example BAR0 address
    static volatile uint32_t* sq_doorbell = (volatile uint32_t*)0xF0001000; // SQ doorbell
    
    /* Static arrays for the command and data */
    __attribute__((aligned(4096))) uint8_t command[CMD_SIZE];
    static __attribute__((aligned(4096))) uint8_t data_buffer[4096];
    
    /* Copy data to aligned buffer */
    printf("Copying data to aligned buffer...\n");
    for (uint32_t i = 0; i < size; i++) {
        data_buffer[i] = ((const uint8_t*)data)[i];
    }
    
    /* Clear command buffer */
    printf("Preparing command buffer...\n");
    for (uint32_t i = 0; i < CMD_SIZE; i++) {
        command[i] = 0;
    }
    
    /* Set command fields */
    command[0] = OPCODE_WRITE;                       // Opcode
    command[1] = 0;                                  // Flags
    command[2] = 1;                                  // Command ID
    command[3] = 0;
    command[4] = 1;                                  // Namespace ID (1)
    command[5] = 0;
    command[6] = 0;
    command[7] = 0;
    
    /* Set PRP1 (data buffer physical address) */
    printf("Setting PRP1 to data buffer address: 0x%p\n", data_buffer);
    uint64_t data_addr = (uint64_t)data_buffer;
    for (int i = 0; i < 8; i++) {
        command[24 + i] = (data_addr >> (i * 8)) & 0xFF;
    }
    
    /* Set LBA */
    printf("Setting LBA: 0x%lx\n", lba);
    for (int i = 0; i < 8; i++) {
        command[40 + i] = (lba >> (i * 8)) & 0xFF;
    }
    
    /* Set block count (0-based, so 0 means 1 block) */
    uint8_t num_blocks = (size / 512) - 1;
    printf("Setting block count: %u (value %u)\n", num_blocks + 1, num_blocks);
    command[48] = num_blocks;
    
    /* Submit command by writing to doorbell */
    printf("Ringing doorbell at address 0x%p\n", sq_doorbell);
    *sq_doorbell = 1;
    
    /* Simple polling loop for completion */
    printf("Waiting for command completion...\n");
    volatile uint32_t timeout = 1000000;
    while (timeout--) {
        /* In real code, would check completion queue entry */
        /* For this example, we just assume success after a delay */
    }
    
    printf("nvme_write_inline: Write operation completed\n");
    return 0;
}

/* NVMe Write - Streamlined minimal implementation with static variables */
static int nvme_minimal_write(uint64_t lba, const uint8_t* data, uint32_t count) {
    /* Static NVMe controller registers at fixed memory address */
    static volatile uint32_t* const NVME_REG_BASE = (volatile uint32_t*)0xF0000000;
    static volatile uint32_t* const NVME_SQ_DOORBELL = (volatile uint32_t*)0xF0001000;
    static volatile uint32_t* const NVME_CQ_DOORBELL = (volatile uint32_t*)0xF0001004;
    
    /* Static DMA-aligned buffers for command and data */
    static __attribute__((aligned(4096))) uint8_t cmd_buffer[64];
    static __attribute__((aligned(4096))) uint8_t data_buffer[4096];
    
    /* Static completion queue entry */
    static __attribute__((aligned(4096))) uint8_t cq_entry[16];
    
    /* Static command values */
    static const uint8_t NVME_CMD_WRITE = 0x01;
    static const uint16_t CMD_ID = 0x1234;
    static const uint32_t NAMESPACE_ID = 1;
    
    /* Copy data to DMA buffer */
    for (uint32_t i = 0; i < count * 512; i++) {
        data_buffer[i] = data[i];
    }
    
    /* Construct write command - clear buffer first */
    for (int i = 0; i < 64; i++) {
        cmd_buffer[i] = 0;
    }
    
    /* Set command header fields */
    cmd_buffer[0] = NVME_CMD_WRITE;                 // Opcode
    cmd_buffer[2] = CMD_ID & 0xFF;                  // Command ID (low byte)
    cmd_buffer[3] = (CMD_ID >> 8) & 0xFF;           // Command ID (high byte)
    cmd_buffer[4] = NAMESPACE_ID & 0xFF;            // Namespace ID (byte 0)
    cmd_buffer[5] = (NAMESPACE_ID >> 8) & 0xFF;     // Namespace ID (byte 1)
    cmd_buffer[6] = (NAMESPACE_ID >> 16) & 0xFF;    // Namespace ID (byte 2)
    cmd_buffer[7] = (NAMESPACE_ID >> 24) & 0xFF;    // Namespace ID (byte 3)
    
    /* Set PRP1 (data buffer address) */
    uint64_t data_addr = (uint64_t)data_buffer;
    for (int i = 0; i < 8; i++) {
        cmd_buffer[24 + i] = (data_addr >> (i * 8)) & 0xFF;
    }
    
    /* Set LBA field */
    for (int i = 0; i < 8; i++) {
        cmd_buffer[40 + i] = (lba >> (i * 8)) & 0xFF;
    }
    
    /* Set number of blocks (0-based count) */
    cmd_buffer[48] = (count - 1) & 0xFF;            // NLB (byte 0)
    cmd_buffer[49] = ((count - 1) >> 8) & 0xFF;     // NLB (byte 1)
    
    /* Submit command to submission queue */
    *NVME_SQ_DOORBELL = 1;  // Increment tail pointer
    
    /* Poll for completion */
    static uint32_t timeout = 1000000;
    static uint8_t phase_bit = 1;
    
    while (timeout--) {
        /* Check completion queue phase bit */
        if ((cq_entry[15] & 0x01) == phase_bit) {
            /* Check if successful (status code 0) */
            uint16_t status = (cq_entry[14] << 8) | cq_entry[15];
            uint16_t status_code = (status >> 1) & 0xFF;
            
            /* Update completion queue head and phase bit */
            *NVME_CQ_DOORBELL = 1;  // Increment head pointer
            
            /* Return status */
            return (status_code == 0) ? 0 : -status_code;
        }
        
        /* Minimal delay in polling loop */
        for (volatile int i = 0; i < 100; i++);
    }
    
    /* Timeout occurred */
    return -1;
}
