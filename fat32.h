/*
 * FAT32 Implementation Header for Bare Metal AMD64
 * Builds on SATA Controller code from identify.h
 */

#ifndef FAT32_H
#define FAT32_H


// Define FAT32 constants
#define FAT32_SECTOR_SIZE        512
#define FAT32_CLUSTER_SIZE       4096  // 8 sectors per cluster
#define FAT32_SECTORS_PER_CLUSTER (FAT32_CLUSTER_SIZE / FAT32_SECTOR_SIZE)
#define FAT32_RESERVED_SECTORS   32    // Including boot sector
#define FAT32_NUM_FATS           2     // Number of FATs
#define FAT32_ROOT_ENTRIES       0     // FAT32 uses clusters for root directory
#define FAT32_SIGNATURE          0xAA55
#define FAT32_MEDIA_DESCRIPTOR   0xF8   // Fixed disk
#define FAT32_FAT_SIZE_SECTORS   8192   // Size of each FAT in sectors (adjust based on volume size)
#define FAT32_ROOT_CLUSTER       2      // First cluster of root directory

// FAT32 Entry Values
#define FAT32_EOC                0x0FFFFFF8 // End of cluster chain
#define FAT32_BAD_CLUSTER        0x0FFFFFF7 // Bad cluster
#define FAT32_FREE_CLUSTER       0x00000000 // Free cluster

// File attributes
#define FAT32_ATTR_READ_ONLY    0x01
#define FAT32_ATTR_HIDDEN       0x02
#define FAT32_ATTR_SYSTEM       0x04
#define FAT32_ATTR_VOLUME_ID    0x08
#define FAT32_ATTR_DIRECTORY    0x10
#define FAT32_ATTR_ARCHIVE      0x20

// FAT32 Boot Sector structure
typedef struct {
    // Jump instruction and OEM name (11 bytes)
    uint8_t  jump_boot[3];      // Jump instruction to boot code
    char     oem_name[8];       // OEM name/version
    
    // BIOS Parameter Block (BPB) (25 bytes)
    uint16_t bytes_per_sector;  // Bytes per sector (usually 512)
    uint8_t  sectors_per_cluster; // Sectors per cluster
    uint16_t reserved_sectors;  // Reserved sectors (including boot sector)
    uint8_t  num_fats;          // Number of FATs (usually 2)
    uint16_t root_entries;      // Number of root directory entries (0 for FAT32)
    uint16_t total_sectors_16;  // Total sectors (0 for FAT32, use total_sectors_32)
    uint8_t  media_type;        // Media descriptor (0xF8 for fixed disk)
    uint16_t fat_size_16;       // Sectors per FAT (0 for FAT32, use fat_size_32)
    uint16_t sectors_per_track; // Sectors per track
    uint16_t num_heads;         // Number of heads
    uint32_t hidden_sectors;    // Hidden sectors before partition
    uint32_t total_sectors_32;  // Total sectors (if > 65535)
    
    // FAT32 Extended BIOS Parameter Block (EBPB) (54 bytes)
    uint32_t fat_size_32;       // Sectors per FAT
    uint16_t extended_flags;    // Flags (mirroring, active FAT)
    uint16_t fs_version;        // File system version (0.0)
    uint32_t root_cluster;      // First cluster of root directory
    uint16_t fs_info;           // Sector number of FS Info structure
    uint16_t backup_boot_sector; // Sector number of backup boot sector
    uint8_t  reserved[12];      // Reserved (should be zero)
    uint8_t  drive_number;      // Drive number (0x80 for hard disk)
    uint8_t  reserved1;         // Reserved (used by Windows NT)
    uint32_t volume_id;         // Volume serial number
    char     volume_label[11];  // Volume label
    char     fs_type[8];        // File system type (FAT32)
    
    // Boot code and signature
    uint8_t  boot_code[420];    // Boot code
    uint16_t boot_signature;    // Boot signature (0xAA55)

    uint16_t extended_boot_signature;    // Boot signature (0xAA55)
} __attribute__((packed)) fat32_boot_sector_t;

// FAT32 Directory Entry structure
typedef struct {
    char     name[8];           // File name (padded with spaces)
    char     ext[3];            // Extension (padded with spaces)
    uint8_t  attributes;        // File attributes
    uint8_t  reserved;          // Reserved for Windows NT
    uint8_t  creation_time_ms;  // Creation time (milliseconds)
    uint16_t creation_time;     // Creation time (hours, minutes, seconds)
    uint16_t creation_date;     // Creation date
    uint16_t last_access_date;  // Last access date
    uint16_t first_cluster_high; // High 16 bits of first cluster
    uint16_t last_mod_time;     // Last modification time
    uint16_t last_mod_date;     // Last modification date
    uint16_t first_cluster_low; // Low 16 bits of first cluster
    uint32_t file_size;         // File size in bytes
} __attribute__((packed)) fat32_dir_entry_t;

// FAT32 FSInfo structure
typedef struct {
    uint32_t lead_signature;     // Lead signature (0x41615252)
    uint8_t  reserved1[480];     // Reserved
    uint32_t structure_signature; // Structure signature (0x61417272)
    uint32_t free_cluster_count; // Last known free cluster count
    uint32_t next_free_cluster;  // Hint for next free cluster
    uint8_t  reserved2[12];      // Reserved
    uint32_t trail_signature;    // Trail signature (0xAA550000)
} __attribute__((packed)) fat32_fsinfo_t;
// Public functions

int fat32_init(uint64_t ahci_base, int port);
int fat32_format_volume(uint64_t ahci_base, int port, uint64_t total_sectors, const char* volume_label);
int fat32_read_file(uint64_t ahci_base, int port, uint32_t dir_cluster, 
                    const char* filename, void* buffer, uint32_t buffer_size, uint32_t* bytes_read);
int fat32_write_file(uint64_t ahci_base, int port, uint32_t dir_cluster, 
                     const char* filename, const void* buffer, uint32_t buffer_size);
int fat32_delete_file(uint64_t ahci_base, int port, uint32_t dir_cluster, const char* filename);
int fat32_list_directory(uint64_t ahci_base, int port, uint32_t dir_cluster);
int fat32_read_cluster(uint64_t ahci_base, int port, uint32_t cluster_num, void* buffer);
int fat32_write_cluster(uint64_t ahci_base, int port, uint32_t cluster_num, const void* buffer);
uint32_t fat32_get_next_cluster(uint64_t ahci_base, int port, uint32_t current_cluster);
int fat32_allocate_cluster(uint64_t ahci_base, int port, uint32_t* new_cluster);
int fat32_free_cluster_chain(uint64_t ahci_base, int port, uint32_t start_cluster);
int fat32_create_entry(uint64_t ahci_base, int port, uint32_t dir_cluster, 
                       const char* name, uint8_t attributes, uint32_t* new_entry_cluster);

// Test function
void filesystem_test(uint64_t AHCI_BASE);

// String utility function for legacy C code

#endif // FAT32_H