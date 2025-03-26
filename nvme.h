#ifndef NVME_H
#define NVME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* NVMe Register Offsets */
#define NVME_REG_CAP      0x0000  // Controller Capabilities
#define NVME_REG_VS       0x0008  // Version
#define NVME_REG_INTMS    0x000C  // Interrupt Mask Set
#define NVME_REG_INTMC    0x0010  // Interrupt Mask Clear
#define NVME_REG_CC       0x0014  // Controller Configuration
#define NVME_REG_CSTS     0x001C  // Controller Status
#define NVME_REG_NSSR     0x0020  // NVM Subsystem Reset
#define NVME_REG_AQA      0x0024  // Admin Queue Attributes
#define NVME_REG_ASQ      0x0028  // Admin Submission Queue Base Address
#define NVME_REG_ACQ      0x0030  // Admin Completion Queue Base Address
#define NVME_REG_CMBLOC   0x0038  // Controller Memory Buffer Location
#define NVME_REG_CMBSZ    0x003C  // Controller Memory Buffer Size
#define NVME_REG_SQ0TDBL  0x1000  // Submission Queue 0 Tail Doorbell
#define NVME_REG_CQ0HDBL  0x1004  // Completion Queue 0 Head Doorbell

/* NVMe Controller Register Bit Masks */
#define NVME_CC_EN        (1 << 0)   // Enable
#define NVME_CC_CSS_NVM   (0 << 4)   // NVM Command Set
#define NVME_CC_MPS_SHIFT 7          // Memory Page Size
#define NVME_CC_AMS_RR    (0 << 11)  // Round Robin arbitration
#define NVME_CC_SHN_NONE  (0 << 14)  // No shutdown
#define NVME_CC_IOSQES    (6 << 16)  // I/O Submission Queue Entry Size (64 bytes)
#define NVME_CC_IOCQES    (4 << 20)  // I/O Completion Queue Entry Size (16 bytes)

#define NVME_CSTS_RDY     (1 << 0)   // Ready
#define NVME_CSTS_CFS     (1 << 1)   // Controller Fatal Status
#define NVME_CSTS_SHST    (3 << 2)   // Shutdown Status

/* NVMe Command Opcodes */
#define NVME_CMD_IDENTIFY         0x06
#define NVME_CMD_GET_LOG_PAGE     0x02
#define NVME_CMD_CREATE_SQ        0x01
#define NVME_CMD_CREATE_CQ        0x05
#define NVME_CMD_DELETE_SQ        0x00
#define NVME_CMD_DELETE_CQ        0x04
#define NVME_CMD_READ             0x02
#define NVME_CMD_WRITE            0x01

/* NVMe Identify CNS values */
#define NVME_ID_CNS_NS            0x00
#define NVME_ID_CNS_CTRL          0x01

/* NVMe Completion Queue Entry Status Codes */
#define NVME_CQE_SC_SUCCESS       0x00
#define NVME_CQE_SC_INV_CMD       0x01
#define NVME_CQE_SC_INV_FIELD     0x02

/* NVMe Constants */
#define NVME_ADMIN_QUEUE_SIZE     16    // Size of admin queues in entries
#define NVME_IO_QUEUE_SIZE        128   // Size of I/O queues in entries
#define NVME_PAGE_SIZE            4096  // Memory page size
#define NVME_MAX_NAMESPACES       16    // Maximum number of namespaces to support
#define NVME_MAX_QUEUES           16    // Maximum number of queue pairs to support

/* NVMe Submission Queue Entry (64 bytes) */
struct nvme_sqe {
    uint8_t  opcode;       // Command Opcode
    uint8_t  flags;        // Command Flags
    uint16_t command_id;   // Command Identifier
    uint32_t nsid;         // Namespace Identifier
    uint64_t reserved1;    // Reserved
    uint64_t metadata;     // Metadata Pointer
    uint64_t prp1;         // PRP Entry 1
    uint64_t prp2;         // PRP Entry 2 or PRP List
    uint32_t cdw10;        // Command-specific
    uint32_t cdw11;        // Command-specific
    uint32_t cdw12;        // Command-specific
    uint32_t cdw13;        // Command-specific
    uint32_t cdw14;        // Command-specific
    uint32_t cdw15;        // Command-specific
};

/* NVMe Completion Queue Entry (16 bytes) */
struct nvme_cqe {
    uint32_t cdw0;         // Command-specific
    uint32_t reserved;     // Reserved
    uint16_t sq_head;      // SQ Head Pointer
    uint16_t sq_id;        // SQ Identifier
    uint16_t command_id;   // Command Identifier
    uint16_t status;       // Status Field
};

/* NVMe Queue */
struct nvme_queue {
    uint16_t id;           // Queue ID
    uint16_t size;         // Queue Size (number of entries)
    uint16_t head;         // Head pointer
    uint16_t tail;         // Tail pointer
    uint32_t phase;        // Phase tag
    void *entries;         // Pointer to queue entries
    uint64_t phys_addr;    // Physical address of entries
};

/* NVMe Namespace */
struct nvme_namespace {
    uint32_t id;              // Namespace ID
    uint64_t size;            // Size in logical blocks
    uint32_t lba_size;        // Logical block size
    uint8_t  lba_shift;       // Log2 of LBA size
    bool     active;          // Whether this namespace is active
};

/* NVMe Controller */
struct nvme_controller {
    uint32_t base_addr;        // Base memory-mapped address
    uint32_t doorbell_stride;  // Doorbell stride (from CAP)
    
    // Admin queues
    struct nvme_queue admin_sq;
    struct nvme_queue admin_cq;
    
    // I/O queues
    struct nvme_queue io_sq[NVME_MAX_QUEUES];
    struct nvme_queue io_cq[NVME_MAX_QUEUES];
    uint16_t num_queues;       // Number of queue pairs created
    
    // Namespaces
    struct nvme_namespace namespaces[NVME_MAX_NAMESPACES];
    uint32_t num_namespaces;   // Number of namespaces
    
    // Controller information
    char model[41];            // Model number
    char serial[21];           // Serial number
    uint32_t max_transfer;     // Maximum data transfer size
    bool initialized;          // Whether the controller is initialized
};

/* NVMe Identify Controller Data Structure */
struct nvme_identify_controller {
    // Controller Capabilities and Features
    uint16_t vid;          // PCI Vendor ID
    uint16_t ssvid;        // PCI Subsystem Vendor ID
    char     sn[20];       // Serial Number
    char     mn[40];       // Model Number
    char     fr[8];        // Firmware Revision
    uint8_t  rab;          // Recommended Arbitration Burst
    uint8_t  ieee[3];      // IEEE OUI Identifier
    uint8_t  cmic;         // Controller Multi-Path I/O and Namespace Sharing Capabilities
    uint8_t  mdts;         // Maximum Data Transfer Size
    uint16_t cntlid;       // Controller ID
    uint32_t ver;          // Version
    uint32_t rtd3r;        // RTD3 Resume Latency
    uint32_t rtd3e;        // RTD3 Entry Latency
    uint32_t oaes;         // Optional Asynchronous Events Supported
    uint32_t ctratt;       // Controller Attributes
    uint16_t rrls;         // Read Recovery Levels Supported
    uint8_t  reserved1[9];
    
    // Admin Command Set Attributes
    uint8_t  sqes;         // Submission Queue Entry Size
    uint8_t  cqes;         // Completion Queue Entry Size
    uint16_t maxcmd;       // Maximum Command Size
    uint32_t nn;           // Number of Namespaces
    uint16_t oncs;         // Optional NVM Command Support
    uint16_t fuses;        // Fused Operation Support
    uint8_t  fna;          // Format NVM Attributes
    uint8_t  vwc;          // Volatile Write Cache
    uint16_t awun;         // Atomic Write Unit Normal
    uint16_t awupf;        // Atomic Write Unit Power Fail
    uint8_t  icsvscc;      // NVM Vendor Specific Command Configuration
    uint8_t  nwpc;         // Namespace Write Protection Capabilities
    uint16_t acwu;         // Atomic Compare & Write Unit
    uint16_t reserved2;
    uint32_t sgls;         // SGL Support
    
    // NVM Command Set Attributes
    uint32_t reserved3[32];
    
    // Power State Descriptors
    uint32_t psd[32][8];   // Power State Descriptors
    
    // Vendor Specific
    uint32_t vs[1024];     // Vendor Specific
};

/* NVMe Identify Namespace Data Structure */
struct nvme_identify_namespace {
    uint64_t nsze;           // Namespace Size (total size in logical blocks)
    uint64_t ncap;           // Namespace Capacity (size exposed to host)
    uint64_t nuse;           // Namespace Utilization
    uint8_t  nsfeat;         // Namespace Features
    uint8_t  nlbaf;          // Number of LBA Formats
    uint8_t  flbas;          // Formatted LBA Size
    uint8_t  mc;             // Metadata Capabilities
    uint8_t  dpc;            // End-to-end Data Protection Capabilities
    uint8_t  dps;            // End-to-end Data Protection Type Settings
    uint8_t  nmic;           // Namespace Multi-path I/O and Namespace Sharing Capabilities
    uint8_t  rescap;         // Reservation Capabilities
    uint8_t  fpi;            // Format Progress Indicator
    uint8_t  dlfeat;         // Deallocate Logical Block Features
    uint16_t nawun;          // Namespace Atomic Write Unit Normal
    uint16_t nawupf;         // Namespace Atomic Write Unit Power Fail
    uint16_t nacwu;          // Namespace Atomic Compare & Write Unit
    uint16_t nabsn;          // Namespace Atomic Boundary Size Normal
    uint16_t nabo;           // Namespace Atomic Boundary Offset
    uint16_t nabspf;         // Namespace Atomic Boundary Size Power Fail
    uint16_t noiob;          // Namespace Optimal IO Boundary
    uint64_t nvmcap[2];      // NVM Capacity
    uint8_t  reserved1[40];
    uint8_t  nguid[16];      // Namespace Globally Unique Identifier
    uint8_t  eui64[8];       // IEEE Extended Unique Identifier
    
    // LBA Format Support
    struct {
        uint16_t ms;         // Metadata Size
        uint8_t  lbads;      // LBA Data Size (2^n bytes)
        uint8_t  rp;         // Relative Performance
    } lbaf[16];              // LBA Formats
    
    uint8_t reserved2[192];
};

/* Function Prototypes */
// Core NVMe functions
uint32_t find_nvme_controller(void);
void nvme_init(uint32_t base_addr);
int nvme_reset_controller(void);
int nvme_identify_controller(void);
int nvme_identify_namespace(uint32_t nsid);
int nvme_create_io_queues(void);
int nvme_initialize(void);

// Command submission and completion
int nvme_submit_admin_command(uint8_t opcode, uint32_t nsid, uint64_t prp1, uint64_t prp2, 
                             uint32_t cdw10, uint32_t cdw11, uint32_t cdw12, uint32_t cdw13, 
                             uint32_t cdw14, uint32_t cdw15);
int nvme_wait_for_admin_completion(uint16_t command_id);
int nvme_submit_io_command(uint16_t queue_id, uint8_t opcode, uint32_t nsid, uint64_t prp1, 
                          uint64_t prp2, uint32_t cdw10, uint32_t cdw11, uint32_t cdw12, 
                          uint32_t cdw13, uint32_t cdw14, uint32_t cdw15);
int nvme_wait_for_io_completion(uint16_t queue_id, uint16_t command_id);

// Data transfer functions
int nvme_read_sectors(uint32_t nsid, uint64_t start_lba, uint32_t count, void *buffer);
int nvme_write_sectors(uint32_t nsid, uint64_t start_lba, uint32_t count, const void *buffer);

// Utility functions
void nvme_demo(void);
void cmd_nvme_list(void);
void cmd_nvme_read(uint32_t nsid, uint64_t lba, uint32_t count);
void register_nvme_commands(void);
void init_nvme_subsystem(void);

#endif /* NVME_H */