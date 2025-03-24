#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* External references to kernel functions and variables */
extern void terminal_writestring(const char* data);
extern void terminal_putchar(char c);
extern uint8_t make_color(enum vga_color fg, enum vga_color bg);
extern void terminal_setcolor(uint8_t color);
extern char command_buffer[];
extern int command_length;
extern void cmd_help();
extern void cmd_clear();
extern void cmd_hello();

/* Forward declarations of our functions */
void* malloc(size_t size);
void free(void* ptr);
void* memcpy(void* dest, const void* src, size_t n);
void* memset(void* s, int c, size_t n);
bool strncmp(const char* s1, const char* s2, size_t n);
char* strchr(const char* str, char ch);
void itoa(int value, char* str, int base);

/* ATA I/O ports */
#define ATA_PRIMARY_DATA         0x1F0
#define ATA_PRIMARY_ERROR        0x1F1
#define ATA_PRIMARY_SECTOR_COUNT 0x1F2
#define ATA_PRIMARY_LBA_LOW      0x1F3
#define ATA_PRIMARY_LBA_MID      0x1F4
#define ATA_PRIMARY_LBA_HIGH     0x1F5
#define ATA_PRIMARY_DRIVE_SELECT 0x1F6
#define ATA_PRIMARY_STATUS       0x1F7
#define ATA_PRIMARY_COMMAND      0x1F7

/* ATA commands */
#define ATA_CMD_READ_SECTORS     0x20
#define ATA_CMD_WRITE_SECTORS    0x30
#define ATA_CMD_IDENTIFY         0xEC
#define ATA_CMD_FLUSH_CACHE      0xE7

/* ATA status bits */
#define ATA_STATUS_ERR  (1 << 0)
#define ATA_STATUS_DRQ  (1 << 3)
#define ATA_STATUS_DF   (1 << 5)
#define ATA_STATUS_BUSY (1 << 7)

/* ATA drive select bits */
#define ATA_DRIVE_MASTER 0xA0
#define ATA_DRIVE_SLAVE  0xB0

/* Buffer for disk I/O */
#define SECTOR_SIZE 512
uint8_t disk_buffer[SECTOR_SIZE];

/* Function prototypes for I/O operations */
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

/* Wait for ATA drive to be ready */
void ata_wait_ready() {
    while (inb(ATA_PRIMARY_STATUS) & ATA_STATUS_BUSY);
}

/* Wait for ATA data to be ready */
bool ata_wait_data() {
    uint8_t status;
    do {
        status = inb(ATA_PRIMARY_STATUS);
        if (status & ATA_STATUS_ERR)
            return false;
        if (status & ATA_STATUS_DF)
            return false;
    } while (!(status & ATA_STATUS_DRQ));
    
    return true;
}

/* Initialize ATA drive */
bool ata_init() {
    terminal_writestring("Initializing ATA drive...\n");
    
    // Select master drive
    outb(ATA_PRIMARY_DRIVE_SELECT, ATA_DRIVE_MASTER);
    
    // Wait for drive to be ready
    ata_wait_ready();
    
    // Send IDENTIFY command
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_IDENTIFY);
    
    // Wait for drive to be ready again
    ata_wait_ready();
    
    // Check if drive exists by reading status
    if (inb(ATA_PRIMARY_STATUS) == 0) {
        terminal_writestring("No ATA drive detected\n");
        return false;
    }
    
    // Wait for data to be ready
    if (!ata_wait_data()) {
        terminal_writestring("ATA drive error\n");
        return false;
    }
    
    // Read the IDENTIFY data (not used for now, just clear the buffer)
    for (int i = 0; i < 256; i++) {
        inw(ATA_PRIMARY_DATA);
    }
    
    terminal_writestring("ATA drive detected and initialized\n");
    return true;
}

/* Read a sector from disk */
bool ata_read_sector(uint32_t lba, uint8_t* buffer) {
    // Select drive and set highest 4 bits of LBA
    outb(ATA_PRIMARY_DRIVE_SELECT, ATA_DRIVE_MASTER | ((lba >> 24) & 0x0F));
    
    // Set sector count, LBA low/mid/high
    outb(ATA_PRIMARY_SECTOR_COUNT, 1);
    outb(ATA_PRIMARY_LBA_LOW, lba & 0xFF);
    outb(ATA_PRIMARY_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_PRIMARY_LBA_HIGH, (lba >> 16) & 0xFF);
    
    // Send READ command
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_READ_SECTORS);
    
    // Wait for drive to be ready
    ata_wait_ready();
    
    // Wait for data to be ready
    if (!ata_wait_data()) {
        return false;
    }
    
    // Read the sector data
    for (int i = 0; i < SECTOR_SIZE / 2; i++) {
        uint16_t data = inw(ATA_PRIMARY_DATA);
        buffer[i*2] = data & 0xFF;
        buffer[i*2+1] = (data >> 8) & 0xFF;
    }
    
    return true;
}

/* Write a sector to disk */
bool ata_write_sector(uint32_t lba, uint8_t* buffer) {
    // Select drive and set highest 4 bits of LBA
    outb(ATA_PRIMARY_DRIVE_SELECT, ATA_DRIVE_MASTER | ((lba >> 24) & 0x0F));
    
    // Set sector count, LBA low/mid/high
    outb(ATA_PRIMARY_SECTOR_COUNT, 1);
    outb(ATA_PRIMARY_LBA_LOW, lba & 0xFF);
    outb(ATA_PRIMARY_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_PRIMARY_LBA_HIGH, (lba >> 16) & 0xFF);
    
    // Send WRITE command
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_WRITE_SECTORS);
    
    // Wait for drive to be ready
    ata_wait_ready();
    
    // Wait for data request
    if (!ata_wait_data()) {
        return false;
    }
    
    // Write the sector data
    for (int i = 0; i < SECTOR_SIZE / 2; i++) {
        outw(ATA_PRIMARY_DATA, (buffer[i*2+1] << 8) | buffer[i*2]);
    }
    
    // Flush the cache
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_FLUSH_CACHE);
    ata_wait_ready();
    
    return true;
}

/* FAT32 structures and definitions */
#define FAT32_EOC          0x0FFFFFF8  // End of cluster marker
#define FAT32_BAD_CLUSTER  0x0FFFFFF7  // Bad cluster marker
#define FAT32_FREE_CLUSTER 0x00000000  // Free cluster marker

// FAT32 Boot Sector (BPB - BIOS Parameter Block)
typedef struct {
    uint8_t  jump_code[3];            // Jump instruction to boot code
    uint8_t  oem_name[8];             // OEM name
    uint16_t bytes_per_sector;        // Bytes per sector
    uint8_t  sectors_per_cluster;     // Sectors per cluster
    uint16_t reserved_sectors;        // Reserved sectors
    uint8_t  num_fats;                // Number of FATs
    uint16_t root_entries;            // Root directory entries (0 for FAT32)
    uint16_t total_sectors_16;        // Total sectors (16-bit value)
    uint8_t  media_type;              // Media descriptor
    uint16_t sectors_per_fat_16;      // Sectors per FAT (16-bit value, 0 for FAT32)
    uint16_t sectors_per_track;       // Sectors per track
    uint16_t num_heads;               // Number of heads
    uint32_t hidden_sectors;          // Hidden sectors
    uint32_t total_sectors_32;        // Total sectors (32-bit value)
    
    // FAT32 Extended BPB
    uint32_t sectors_per_fat_32;      // Sectors per FAT (32-bit value)
    uint16_t ext_flags;               // Extension flags
    uint16_t fs_version;              // File system version
    uint32_t root_cluster;            // First cluster of root directory
    uint16_t fs_info;                 // Sector number of FS info structure
    uint16_t backup_boot_sector;      // Sector number of backup boot sector
    uint8_t  reserved[12];            // Reserved
    uint8_t  drive_number;            // Drive number
    uint8_t  reserved1;               // Reserved
    uint8_t  boot_signature;          // Boot signature (0x29)
    uint32_t volume_id;               // Volume ID
    uint8_t  volume_label[11];        // Volume label
    uint8_t  fs_type[8];              // File system type (FAT32)
    // Boot code and boot signature omitted
} __attribute__((packed)) fat32_bpb_t;

// FAT32 Directory Entry
typedef struct {
    uint8_t  name[8];                 // File name
    uint8_t  ext[3];                  // File extension
    uint8_t  attributes;              // File attributes
    uint8_t  reserved;                // Reserved
    uint8_t  create_time_tenth;       // Creation time (tenths of second)
    uint16_t create_time;             // Creation time
    uint16_t create_date;             // Creation date
    uint16_t access_date;             // Last access date
    uint16_t cluster_high;            // High 16 bits of first cluster
    uint16_t modify_time;             // Last modification time
    uint16_t modify_date;             // Last modification date
    uint16_t cluster_low;             // Low 16 bits of first cluster
    uint32_t file_size;               // File size
} __attribute__((packed)) fat32_dir_entry_t;

// File attribute bits
#define FAT_ATTR_READ_ONLY  0x01
#define FAT_ATTR_HIDDEN     0x02
#define FAT_ATTR_SYSTEM     0x04
#define FAT_ATTR_VOLUME_ID  0x08
#define FAT_ATTR_DIRECTORY  0x10
#define FAT_ATTR_ARCHIVE    0x20
#define FAT_ATTR_LFN        0x0F  // Long file name entry

// FAT32 filesystem state
typedef struct {
    fat32_bpb_t bpb;               // Boot Parameter Block
    uint32_t fat_begin_lba;        // Starting LBA of the first FAT
    uint32_t cluster_begin_lba;    // Starting LBA of the data region
    uint32_t sectors_per_cluster;  // Sectors per cluster
    uint32_t root_dir_cluster;     // Root directory cluster
    uint32_t bytes_per_cluster;    // Bytes per cluster
} fat32_fs_t;

// Global filesystem state
fat32_fs_t fat32_fs;

// Convert a cluster number to its LBA
uint32_t fat32_cluster_to_lba(uint32_t cluster) {
    // Clusters are numbered from 2, with 0 and 1 being reserved
    return fat32_fs.cluster_begin_lba + (cluster - 2) * fat32_fs.sectors_per_cluster;
}

// Read a cluster from disk into a buffer
bool fat32_read_cluster(uint32_t cluster, uint8_t* buffer) {
    uint32_t lba = fat32_cluster_to_lba(cluster);
    
    // Read all sectors in the cluster
    for (uint32_t i = 0; i < fat32_fs.sectors_per_cluster; i++) {
        if (!ata_read_sector(lba + i, buffer + i * SECTOR_SIZE)) {
            return false;
        }
    }
    
    return true;
}

// Write a cluster to disk
bool fat32_write_cluster(uint32_t cluster, uint8_t* buffer) {
    uint32_t lba = fat32_cluster_to_lba(cluster);
    
    // Write all sectors in the cluster
    for (uint32_t i = 0; i < fat32_fs.sectors_per_cluster; i++) {
        if (!ata_write_sector(lba + i, buffer + i * SECTOR_SIZE)) {
            return false;
        }
    }
    
    return true;
}

// Read a FAT entry
uint32_t fat32_read_fat_entry(uint32_t cluster) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fat32_fs.fat_begin_lba + (fat_offset / SECTOR_SIZE);
    uint32_t entry_offset = fat_offset % SECTOR_SIZE;
    
    // Read the FAT sector into our buffer
    if (!ata_read_sector(fat_sector, disk_buffer)) {
        return FAT32_BAD_CLUSTER;
    }
    
    // Extract the 32-bit FAT entry (masking out the top 4 bits which are reserved)
    uint32_t fat_entry = *((uint32_t*)(&disk_buffer[entry_offset])) & 0x0FFFFFFF;
    
    return fat_entry;
}

// Write a FAT entry
bool fat32_write_fat_entry(uint32_t cluster, uint32_t value) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fat32_fs.fat_begin_lba + (fat_offset / SECTOR_SIZE);
    uint32_t entry_offset = fat_offset % SECTOR_SIZE;
    
    // Read the FAT sector into our buffer
    if (!ata_read_sector(fat_sector, disk_buffer)) {
        return false;
    }
    
    // Preserve the top 4 reserved bits
    uint32_t current = *((uint32_t*)(&disk_buffer[entry_offset]));
    uint32_t reserved_bits = current & 0xF0000000;
    
    // Update the entry, preserving reserved bits
    *((uint32_t*)(&disk_buffer[entry_offset])) = (value & 0x0FFFFFFF) | reserved_bits;
    
    // Write the FAT sector back to disk
    if (!ata_write_sector(fat_sector, disk_buffer)) {
        return false;
    }
    
    return true;
}

// Allocate a new cluster
uint32_t fat32_allocate_cluster() {
    // Simple allocation strategy: just scan through the FAT for a free cluster
    // This is inefficient for large disks but works for our demonstration
    
    for (uint32_t cluster = 2; cluster < fat32_fs.bpb.total_sectors_32 / fat32_fs.sectors_per_cluster; cluster++) {
        if (fat32_read_fat_entry(cluster) == FAT32_FREE_CLUSTER) {
            // Mark as end of chain
            fat32_write_fat_entry(cluster, FAT32_EOC);
            return cluster;
        }
    }
    
    // No free clusters found
    return 0;
}

// Initialize the FAT32 filesystem
bool fat32_init() {
    terminal_writestring("Initializing FAT32 filesystem...\n");
    
    // Read the boot sector (BPB)
    if (!ata_read_sector(0, (uint8_t*)&fat32_fs.bpb)) {
        terminal_writestring("Failed to read boot sector\n");
        return false;
    }
    
    // Check for FAT32 signature
    if (fat32_fs.bpb.boot_signature != 0x29) {
        terminal_writestring("Invalid boot signature\n");
        return false;
    }
    
    // Basic sanity checks
    if (fat32_fs.bpb.bytes_per_sector != SECTOR_SIZE) {
        terminal_writestring("Unexpected sector size\n");
        return false;
    }
    
    // Calculate important values
    fat32_fs.fat_begin_lba = fat32_fs.bpb.reserved_sectors;
    fat32_fs.sectors_per_cluster = fat32_fs.bpb.sectors_per_cluster;
    fat32_fs.bytes_per_cluster = fat32_fs.sectors_per_cluster * SECTOR_SIZE;
    fat32_fs.root_dir_cluster = fat32_fs.bpb.root_cluster;
    
    // Calculate the beginning of the data region
    fat32_fs.cluster_begin_lba = fat32_fs.fat_begin_lba + 
                             (fat32_fs.bpb.num_fats * fat32_fs.bpb.sectors_per_fat_32);
    
    // Print filesystem information
    terminal_writestring("FAT32 filesystem detected:\n");
    terminal_writestring("  Bytes per sector: ");
    // Convert to string and display
    char bytes_str[16];
    itoa(fat32_fs.bpb.bytes_per_sector, bytes_str, 10);
    terminal_writestring(bytes_str);
    terminal_writestring("\n");
    
    terminal_writestring("  Sectors per cluster: ");
    itoa(fat32_fs.sectors_per_cluster, bytes_str, 10);
    terminal_writestring(bytes_str);
    terminal_writestring("\n");
    
    terminal_writestring("  Root directory cluster: ");
    itoa(fat32_fs.root_dir_cluster, bytes_str, 10);
    terminal_writestring(bytes_str);
    terminal_writestring("\n");
    
    return true;
}

// Structure to represent an open file
#define MAX_OPEN_FILES 8
typedef struct {
    bool in_use;
    uint32_t first_cluster;
    uint32_t current_cluster;
    uint32_t current_position;
    uint32_t file_size;
    bool is_directory;
} fat32_file_handle_t;

fat32_file_handle_t open_files[MAX_OPEN_FILES];

// Initialize file handles
void fat32_init_file_handles() {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        open_files[i].in_use = false;
    }
}

// Convert 8.3 filename from directory entry to a string
void fat32_83_to_str(const fat32_dir_entry_t* entry, char* str) {
    int i, j = 0;
    
    // Copy name (skip spaces)
    for (i = 0; i < 8; i++) {
        if (entry->name[i] != ' ') {
            str[j++] = entry->name[i];
        }
    }
    
    // Add dot and extension if there is one
    if (entry->ext[0] != ' ') {
        str[j++] = '.';
        for (i = 0; i < 3; i++) {
            if (entry->ext[i] != ' ') {
                str[j++] = entry->ext[i];
            }
        }
    }
    
    // Null terminate
    str[j] = '\0';
}

// Convert string to 8.3 filename format for directory entry
void fat32_str_to_83(const char* str, fat32_dir_entry_t* entry) {
    int i;
    
    // Initialize with spaces
    for (i = 0; i < 8; i++) {
        entry->name[i] = ' ';
    }
    for (i = 0; i < 3; i++) {
        entry->ext[i] = ' ';
    }
    
    // Copy name (up to dot or end)
    i = 0;
    while (str[i] && str[i] != '.' && i < 8) {
        entry->name[i] = str[i];
        i++;
    }
    
    // Skip to extension if there is one
    if (str[i] == '.') {
        i++;
        int j = 0;
        while (str[i] && j < 3) {
            entry->ext[j] = str[i];
            i++;
            j++;
        }
    }
}

// Simple integer to ASCII string conversion
void itoa(int value, char* str, int base) {
    static const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    
    char* ptr = str;
    bool negative = false;
    
    // Handle negative numbers
    if (value < 0 && base == 10) {
        negative = true;
        value = -value;
    }
    
    // Store digits in reverse order
    do {
        *ptr++ = digits[value % base];
        value /= base;
    } while (value);
    
    // Add negative sign if needed
    if (negative) {
        *ptr++ = '-';
    }
    
    // Terminate the string
    *ptr = '\0';
    
    // Reverse the string
    ptr--;
    char* start = str;
    char temp;
    while (start < ptr) {
        temp = *start;
        *start++ = *ptr;
        *ptr-- = temp;
    }
}

// Find a file in a directory by name
bool fat32_find_file(uint32_t dir_cluster, const char* filename, fat32_dir_entry_t* entry) {
    uint8_t* cluster_buffer = (uint8_t*)malloc(fat32_fs.bytes_per_cluster);
    if (!cluster_buffer) {
        return false;
    }
    
    uint32_t current_cluster = dir_cluster;
    
    while (current_cluster >= 2 && current_cluster < FAT32_EOC) {
        // Read the current directory cluster
        if (!fat32_read_cluster(current_cluster, cluster_buffer)) {
            free(cluster_buffer);
            return false;
        }
        
        // Scan entries in this cluster
        for (uint32_t offset = 0; offset < fat32_fs.bytes_per_cluster; offset += sizeof(fat32_dir_entry_t)) {
            fat32_dir_entry_t* dir_entry = (fat32_dir_entry_t*)(cluster_buffer + offset);
            
            // Skip free entries and long filename entries
            if (dir_entry->name[0] == 0) {
                // End of directory
                break;
            }
            if (dir_entry->name[0] == 0xE5 || dir_entry->attributes == FAT_ATTR_LFN) {
                continue;
            }
            
            // Convert entry name to string
            char entry_name[13];
            fat32_83_to_str(dir_entry, entry_name);
            
            // Compare with target
            if (strcmp(entry_name, filename)) {
                // Found it!
                if (entry) {
                    memcpy(entry, dir_entry, sizeof(fat32_dir_entry_t));
                }
                free(cluster_buffer);
                return true;
            }
        }
        
        // Move to next cluster in chain
        current_cluster = fat32_read_fat_entry(current_cluster);
    }
    
    free(cluster_buffer);
    return false;
}

// Open a file by name
int fat32_open(const char* filename) {
    fat32_dir_entry_t entry;
    
    // Find the file in the root directory
    if (!fat32_find_file(fat32_fs.root_dir_cluster, filename, &entry)) {
        return -1; // File not found
    }
    
    // Find a free file handle
    int handle = -1;
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!open_files[i].in_use) {
            handle = i;
            break;
        }
    }
    
    if (handle == -1) {
        return -2; // No free handles
    }
    
    // Set up the file handle
    open_files[handle].in_use = true;
    open_files[handle].first_cluster = (entry.cluster_high << 16) | entry.cluster_low;
    open_files[handle].current_cluster = open_files[handle].first_cluster;
    open_files[handle].current_position = 0;
    open_files[handle].file_size = entry.file_size;
    open_files[handle].is_directory = (entry.attributes & FAT_ATTR_DIRECTORY) != 0;
    
    return handle;
}

// Close a file
void fat32_close(int handle) {
    if (handle >= 0 && handle < MAX_OPEN_FILES) {
        open_files[handle].in_use = false;
    }
}

// Read data from a file
uint32_t fat32_read(int handle, void* buffer, uint32_t size) {
    if (handle < 0 || handle >= MAX_OPEN_FILES || !open_files[handle].in_use) {
        return 0;
    }
    
    fat32_file_handle_t* file = &open_files[handle];
    uint32_t bytes_read = 0;
    uint8_t* dest = (uint8_t*)buffer;
    
    // Don't read beyond the file size
    if (file->current_position + size > file->file_size) {
        size = file->file_size - file->current_position;
    }
    
    // Temporary buffer for cluster data
    uint8_t* cluster_buffer = (uint8_t*)malloc(fat32_fs.bytes_per_cluster);
    if (!cluster_buffer) {
        return 0;
    }
    
    while (size > 0) {
        // Calculate position within current cluster
        uint32_t cluster_offset = file->current_position % fat32_fs.bytes_per_cluster;
        
        // If we've reached the end of the current cluster, move to the next one
        if (cluster_offset == 0 && file->current_position > 0) {
            uint32_t next_cluster = fat32_read_fat_entry(file->current_cluster);
            if (next_cluster >= FAT32_EOC) {
                // End of file chain
                break;
            }
            file->current_cluster = next_cluster;
        }
        
        // Read the current cluster
        if (!fat32_read_cluster(file->current_cluster, cluster_buffer)) {
            free(cluster_buffer);
            return bytes_read;
        }
        
        // Determine how many bytes to copy from this cluster
        uint32_t bytes_to_copy = fat32_fs.bytes_per_cluster - cluster_offset;
        if (bytes_to_copy > size) {
            bytes_to_copy = size;
        }
        
        // Copy the data
        memcpy(dest, cluster_buffer + cluster_offset, bytes_to_copy);
        
        // Update counters
        dest += bytes_to_copy;
        file->current_position += bytes_to_copy;
        bytes_read += bytes_to_copy;
        size -= bytes_to_copy;
    }
    
    free(cluster_buffer);
    return bytes_read;
}

// Write data to a file
uint32_t fat32_write(int handle, const void* buffer, uint32_t size) {
    if (handle < 0 || handle >= MAX_OPEN_FILES || !open_files[handle].in_use) {
        return 0;
    }
    
    fat32_file_handle_t* file = &open_files[handle];
    uint32_t bytes_written = 0;
    const uint8_t* src = (const uint8_t*)buffer;
    
    // Temporary buffer for cluster data
    uint8_t* cluster_buffer = (uint8_t*)malloc(fat32_fs.bytes_per_cluster);
    if (!cluster_buffer) {
        return 0;
    }
    
    while (size > 0) {
        // Calculate position within current cluster
        uint32_t cluster_offset = file->current_position % fat32_fs.bytes_per_cluster;
        
        // If we've reached the end of the current cluster, move to the next one
        if (cluster_offset == 0 && file->current_position > 0) {
            uint32_t next_cluster = fat32_read_fat_entry(file->current_cluster);
            if (next_cluster >= FAT32_EOC) {
                // Need to allocate a new cluster
                next_cluster = fat32_allocate_cluster();
                if (next_cluster == 0) {
                    // Disk full
                    free(cluster_buffer);
                    return bytes_written;
                }
                
                // Link the new cluster to the chain
                fat32_write_fat_entry(file->current_cluster, next_cluster);
            }
            file->current_cluster = next_cluster;
        }
        
        // If we need to modify partial cluster data, first read the existing cluster
        if (cluster_offset > 0 || fat32_fs.bytes_per_cluster - cluster_offset > size) {
            if (!fat32_read_cluster(file->current_cluster, cluster_buffer)) {
                free(cluster_buffer);
                return bytes_written;
            }
        }
        
        // Determine how many bytes to write to this cluster
        uint32_t bytes_to_write = fat32_fs.bytes_per_cluster - cluster_offset;
        if (bytes_to_write > size) {
            bytes_to_write = size;
        }
        
        // Copy the data to the cluster buffer
        memcpy(cluster_buffer + cluster_offset, src, bytes_to_write);
        
        // Write the cluster back to disk
        if (!fat32_write_cluster(file->current_cluster, cluster_buffer)) {
            free(cluster_buffer);
            return bytes_written;
        }
        
        // Update counters
        src += bytes_to_write;
        file->current_position += bytes_to_write;
        bytes_written += bytes_to_write;
        size -= bytes_to_write;
        
        // Update file size if necessary
        if (file->current_position > file->file_size) {
            file->file_size = file->current_position;
        }
    }
    
    free(cluster_buffer);
    return bytes_written;
}

// Create a new file or directory
bool fat32_create(const char* filename, bool is_directory) {
    // Check if file already exists
    if (fat32_find_file(fat32_fs.root_dir_cluster, filename, NULL)) {
        return false; // File exists
    }
    
    // Allocate a cluster for the new file
    uint32_t new_cluster = fat32_allocate_cluster();
    if (new_cluster == 0) {
        return false; // No free space
    }
    
    // If it's a directory, initialize it with . and .. entries
    if (is_directory) {
        uint8_t* cluster_buffer = (uint8_t*)malloc(fat32_fs.bytes_per_cluster);
        if (!cluster_buffer) {
            return false;
        }
        
        // Clear the cluster
        memset(cluster_buffer, 0, fat32_fs.bytes_per_cluster);
        
        // Create "." entry
        fat32_dir_entry_t* dot_entry = (fat32_dir_entry_t*)cluster_buffer;
        memset(dot_entry, 0, sizeof(fat32_dir_entry_t));
        dot_entry->name[0] = '.';
        for (int i = 1; i < 8; i++) {
            dot_entry->name[i] = ' ';
        }
        for (int i = 0; i < 3; i++) {
            dot_entry->ext[i] = ' ';
        }
        dot_entry->attributes = FAT_ATTR_DIRECTORY;
        dot_entry->cluster_low = new_cluster & 0xFFFF;
        dot_entry->cluster_high = (new_cluster >> 16) & 0xFFFF;
        
        // Create ".." entry (parent = root directory)
        fat32_dir_entry_t* dotdot_entry = (fat32_dir_entry_t*)(cluster_buffer + sizeof(fat32_dir_entry_t));
        memset(dotdot_entry, 0, sizeof(fat32_dir_entry_t));
        dotdot_entry->name[0] = '.';
        dotdot_entry->name[1] = '.';
        for (int i = 2; i < 8; i++) {
            dotdot_entry->name[i] = ' ';
        }
        for (int i = 0; i < 3; i++) {
            dotdot_entry->ext[i] = ' ';
        }
        dotdot_entry->attributes = FAT_ATTR_DIRECTORY;
        dotdot_entry->cluster_low = fat32_fs.root_dir_cluster & 0xFFFF;
        dotdot_entry->cluster_high = (fat32_fs.root_dir_cluster >> 16) & 0xFFFF;
        
        // Write the initialized directory cluster
        if (!fat32_write_cluster(new_cluster, cluster_buffer)) {
            free(cluster_buffer);
            return false;
        }
        
        free(cluster_buffer);
    }
    
    // We need to find an empty slot in the root directory to add the new entry
    uint8_t* cluster_buffer = (uint8_t*)malloc(fat32_fs.bytes_per_cluster);
    if (!cluster_buffer) {
        return false;
    }
    
    uint32_t current_cluster = fat32_fs.root_dir_cluster;
    bool entry_added = false;
    
    while (current_cluster >= 2 && current_cluster < FAT32_EOC && !entry_added) {
        // Read the current directory cluster
        if (!fat32_read_cluster(current_cluster, cluster_buffer)) {
            free(cluster_buffer);
            return false;
        }
        
        // Look for a free entry slot
        for (uint32_t offset = 0; offset < fat32_fs.bytes_per_cluster; offset += sizeof(fat32_dir_entry_t)) {
            fat32_dir_entry_t* dir_entry = (fat32_dir_entry_t*)(cluster_buffer + offset);
            
            if (dir_entry->name[0] == 0 || dir_entry->name[0] == 0xE5) {
                // Free entry found, fill it with our new file data
                memset(dir_entry, 0, sizeof(fat32_dir_entry_t));
                fat32_str_to_83(filename, dir_entry);
                
                if (is_directory) {
                    dir_entry->attributes = FAT_ATTR_DIRECTORY;
                } else {
                    dir_entry->attributes = FAT_ATTR_ARCHIVE;
                }
                
                // Set creation/modification times (using fixed values for simplicity)
                dir_entry->create_date = 0x4000; // Year 2019
                dir_entry->create_time = 0x0000;
                dir_entry->modify_date = 0x4000;
                dir_entry->modify_time = 0x0000;
                
                // Set cluster and file size
                dir_entry->cluster_low = new_cluster & 0xFFFF;
                dir_entry->cluster_high = (new_cluster >> 16) & 0xFFFF;
                dir_entry->file_size = 0;
                
                // Write the updated directory cluster
                if (!fat32_write_cluster(current_cluster, cluster_buffer)) {
                    free(cluster_buffer);
                    return false;
                }
                
                entry_added = true;
                break;
            }
        }
        
        if (!entry_added) {
            // Check if there's another cluster in the chain
            uint32_t next_cluster = fat32_read_fat_entry(current_cluster);
            if (next_cluster >= FAT32_EOC) {
                // Need to allocate a new cluster for the directory
                next_cluster = fat32_allocate_cluster();
                if (next_cluster == 0) {
                    free(cluster_buffer);
                    return false; // No free space
                }
                
                // Link it to the directory chain
                fat32_write_fat_entry(current_cluster, next_cluster);
                
                // Initialize the new directory cluster with zeros
                memset(cluster_buffer, 0, fat32_fs.bytes_per_cluster);
                if (!fat32_write_cluster(next_cluster, cluster_buffer)) {
                    free(cluster_buffer);
                    return false;
                }
                
                // Add our new entry at the beginning of this new cluster
                fat32_dir_entry_t* dir_entry = (fat32_dir_entry_t*)cluster_buffer;
                memset(dir_entry, 0, sizeof(fat32_dir_entry_t));
                fat32_str_to_83(filename, dir_entry);
                
                if (is_directory) {
                    dir_entry->attributes = FAT_ATTR_DIRECTORY;
                } else {
                    dir_entry->attributes = FAT_ATTR_ARCHIVE;
                }
                
                // Set creation/modification times
                dir_entry->create_date = 0x4000; // Year 2019
                dir_entry->create_time = 0x0000;
                dir_entry->modify_date = 0x4000;
                dir_entry->modify_time = 0x0000;
                
                // Set cluster and file size
                dir_entry->cluster_low = new_cluster & 0xFFFF;
                dir_entry->cluster_high = (new_cluster >> 16) & 0xFFFF;
                dir_entry->file_size = 0;
                
                // Write the updated directory cluster
                if (!fat32_write_cluster(next_cluster, cluster_buffer)) {
                    free(cluster_buffer);
                    return false;
                }
                
                entry_added = true;
            }
            
            current_cluster = next_cluster;
        }
    }
    
    free(cluster_buffer);
    return entry_added;
}

// Delete a file or directory
bool fat32_delete(const char* filename) {
    fat32_dir_entry_t entry;
    
    // Find the file/directory in the root directory
    if (!fat32_find_file(fat32_fs.root_dir_cluster, filename, &entry)) {
        return false; // File not found
    }
    
    // Check if it's a directory and not empty (we don't support recursive deletion)
    uint32_t first_cluster = (entry.cluster_high << 16) | entry.cluster_low;
    if (entry.attributes & FAT_ATTR_DIRECTORY) {
        // Read the directory cluster
        uint8_t* cluster_buffer = (uint8_t*)malloc(fat32_fs.bytes_per_cluster);
        if (!cluster_buffer) {
            return false;
        }
        
        if (!fat32_read_cluster(first_cluster, cluster_buffer)) {
            free(cluster_buffer);
            return false;
        }
        
        // Check if there are any entries besides . and ..
        for (uint32_t offset = 2 * sizeof(fat32_dir_entry_t); offset < fat32_fs.bytes_per_cluster; offset += sizeof(fat32_dir_entry_t)) {
            fat32_dir_entry_t* dir_entry = (fat32_dir_entry_t*)(cluster_buffer + offset);
            
            if (dir_entry->name[0] == 0) {
                // End of directory
                break;
            }
            
            if (dir_entry->name[0] != 0xE5) {
                // Found a valid entry, directory not empty
                free(cluster_buffer);
                return false;
            }
        }
        
        free(cluster_buffer);
    }
    
    // Free the file/directory's clusters
    uint32_t current_cluster = first_cluster;
    while (current_cluster >= 2 && current_cluster < FAT32_EOC) {
        uint32_t next_cluster = fat32_read_fat_entry(current_cluster);
        fat32_write_fat_entry(current_cluster, FAT32_FREE_CLUSTER);
        current_cluster = next_cluster;
    }
    
    // Mark the directory entry as deleted
    uint8_t* cluster_buffer = (uint8_t*)malloc(fat32_fs.bytes_per_cluster);
    if (!cluster_buffer) {
        return false;
    }
    
    uint32_t current_dir_cluster = fat32_fs.root_dir_cluster;
    bool entry_deleted = false;
    
    while (current_dir_cluster >= 2 && current_dir_cluster < FAT32_EOC && !entry_deleted) {
        // Read the current directory cluster
        if (!fat32_read_cluster(current_dir_cluster, cluster_buffer)) {
            free(cluster_buffer);
            return false;
        }
        
        // Search for the entry to delete
        for (uint32_t offset = 0; offset < fat32_fs.bytes_per_cluster; offset += sizeof(fat32_dir_entry_t)) {
            fat32_dir_entry_t* dir_entry = (fat32_dir_entry_t*)(cluster_buffer + offset);
            
            if (dir_entry->name[0] == 0) {
                // End of directory
                break;
            }
            
            if (dir_entry->name[0] != 0xE5 && 
                (dir_entry->cluster_high << 16 | dir_entry->cluster_low) == first_cluster) {
                // Mark entry as deleted
                dir_entry->name[0] = 0xE5;
                
                // Write the updated directory cluster
                if (!fat32_write_cluster(current_dir_cluster, cluster_buffer)) {
                    free(cluster_buffer);
                    return false;
                }
                
                entry_deleted = true;
                break;
            }
        }
        
        if (!entry_deleted) {
            // Move to next cluster in directory chain
            current_dir_cluster = fat32_read_fat_entry(current_dir_cluster);
        }
    }
    
    free(cluster_buffer);
    return entry_deleted;
}

// List files in the root directory
void fat32_list_root() {
    uint8_t* cluster_buffer = (uint8_t*)malloc(fat32_fs.bytes_per_cluster);
    if (!cluster_buffer) {
        terminal_writestring("Memory allocation failed\n");
        return;
    }
    
    uint32_t current_cluster = fat32_fs.root_dir_cluster;
    
    terminal_writestring("Directory listing:\n");
    terminal_writestring("=====================================\n");
    terminal_writestring("Name       Ext  Size      Attributes\n");
    terminal_writestring("=====================================\n");
    
    while (current_cluster >= 2 && current_cluster < FAT32_EOC) {
        // Read the current directory cluster
        if (!fat32_read_cluster(current_cluster, cluster_buffer)) {
            terminal_writestring("Error reading directory cluster\n");
            free(cluster_buffer);
            return;
        }
        
        // Process entries in this cluster
        for (uint32_t offset = 0; offset < fat32_fs.bytes_per_cluster; offset += sizeof(fat32_dir_entry_t)) {
            fat32_dir_entry_t* entry = (fat32_dir_entry_t*)(cluster_buffer + offset);
            
            if (entry->name[0] == 0) {
                // End of directory
                break;
            }
            
            if (entry->name[0] == 0xE5 || entry->attributes == FAT_ATTR_LFN) {
                // Deleted entry or LFN entry
                continue;
            }
            
            // Print entry information
            char name_str[9] = {0};
            char ext_str[4] = {0};
            char size_str[16] = {0};
            char attr_str[8] = "------";
            
            // Copy and null-terminate name and extension
            memcpy(name_str, entry->name, 8);
            memcpy(ext_str, entry->ext, 3);
            
            // Trim trailing spaces
            for (int i = 7; i >= 0; i--) {
                if (name_str[i] == ' ') {
                    name_str[i] = 0;
                } else {
                    break;
                }
            }
            
            for (int i = 2; i >= 0; i--) {
                if (ext_str[i] == ' ') {
                    ext_str[i] = 0;
                } else {
                    break;
                }
            }
            
            // Convert size to string
            itoa(entry->file_size, size_str, 10);
            
            // Set attribute flags
            if (entry->attributes & FAT_ATTR_READ_ONLY) attr_str[0] = 'R';
            if (entry->attributes & FAT_ATTR_HIDDEN)    attr_str[1] = 'H';
            if (entry->attributes & FAT_ATTR_SYSTEM)    attr_str[2] = 'S';
            if (entry->attributes & FAT_ATTR_VOLUME_ID) attr_str[3] = 'V';
            if (entry->attributes & FAT_ATTR_DIRECTORY) attr_str[4] = 'D';
            if (entry->attributes & FAT_ATTR_ARCHIVE)   attr_str[5] = 'A';
            
            // Print the entry
            terminal_writestring(name_str);
            terminal_writestring("  ");
            terminal_writestring(ext_str);
            terminal_writestring("  ");
            terminal_writestring(size_str);
            terminal_writestring("  ");
            terminal_writestring(attr_str);
            terminal_writestring("\n");
        }
        
        // Move to next cluster in chain
        current_cluster = fat32_read_fat_entry(current_cluster);
    }
    
    free(cluster_buffer);
}

/* Command implementations for FAT32 */
void cmd_fsinfo() {
    char buffer[16];
    
    terminal_writestring("Filesystem Information:\n");
    
    terminal_writestring("  OEM Name: ");
    uint8_t oem_name_temp[9];
    memcpy(oem_name_temp, fat32_fs.bpb.oem_name, 8);
    oem_name_temp[8] = 0;
    terminal_writestring((char*)oem_name_temp);
    terminal_writestring("\n");
    
    terminal_writestring("  Volume Label: ");
    uint8_t volume_label_temp[12];
    memcpy(volume_label_temp, fat32_fs.bpb.volume_label, 11);
    volume_label_temp[11] = 0;
    terminal_writestring((char*)volume_label_temp);
    terminal_writestring("\n");
    
    terminal_writestring("  Filesystem Type: ");
    uint8_t fs_type_temp[9];
    memcpy(fs_type_temp, fat32_fs.bpb.fs_type, 8);
    fs_type_temp[8] = 0;
    terminal_writestring((char*)fs_type_temp);
    terminal_writestring("\n");
    
    terminal_writestring("  Bytes per Sector: ");
    itoa(fat32_fs.bpb.bytes_per_sector, buffer, 10);
    terminal_writestring(buffer);
    terminal_writestring("\n");
    
    terminal_writestring("  Sectors per Cluster: ");
    itoa(fat32_fs.bpb.sectors_per_cluster, buffer, 10);
    terminal_writestring(buffer);
    terminal_writestring("\n");
    
    terminal_writestring("  Reserved Sectors: ");
    itoa(fat32_fs.bpb.reserved_sectors, buffer, 10);
    terminal_writestring(buffer);
    terminal_writestring("\n");
    
    terminal_writestring("  Number of FATs: ");
    itoa(fat32_fs.bpb.num_fats, buffer, 10);
    terminal_writestring(buffer);
    terminal_writestring("\n");
    
    terminal_writestring("  Sectors per FAT: ");
    itoa(fat32_fs.bpb.sectors_per_fat_32, buffer, 10);
    terminal_writestring(buffer);
    terminal_writestring("\n");
    
    terminal_writestring("  Total Sectors: ");
    itoa(fat32_fs.bpb.total_sectors_32, buffer, 10);
    terminal_writestring(buffer);
    terminal_writestring("\n");
}

void cmd_ls() {
    fat32_list_root();
}

void cmd_cat() {
    // Extract the filename from the command buffer
    char filename[13];
    int i = 4; // Skip "cat "
    int j = 0;
    
    while (command_buffer[i] && j < 12) {
        filename[j++] = command_buffer[i++];
    }
    filename[j] = '\0';
    
    // Open the file
    int handle = fat32_open(filename);
    if (handle < 0) {
        terminal_writestring("Error: File not found\n");
        return;
    }
    
    // Read and display file contents
    uint8_t* buffer = (uint8_t*)malloc(512);
    if (!buffer) {
        terminal_writestring("Error: Memory allocation failed\n");
        fat32_close(handle);
        return;
    }
    
    uint32_t bytes_read;
    do {
        bytes_read = fat32_read(handle, buffer, 511);
        buffer[bytes_read] = '\0';
        terminal_writestring((char*)buffer);
    } while (bytes_read > 0);
    
    free(buffer);
    fat32_close(handle);
}

void cmd_write() {
    char* space_pos = strchr(command_buffer + 6, ' ');
    if (!space_pos) {
        terminal_writestring("Usage: write <filename> <content>\n");
        return;
    }
    
    // Extract filename
    char filename[13];
    int name_len = space_pos - (command_buffer + 6);
    if (name_len > 12) name_len = 12;
    
    memcpy(filename, command_buffer + 6, name_len);
    filename[name_len] = '\0';
    
    // Create the file if it doesn't exist
    if (!fat32_find_file(fat32_fs.root_dir_cluster, filename, NULL)) {
        if (!fat32_create(filename, false)) {
            terminal_writestring("Error: Could not create file\n");
            return;
        }
    }
    
    // Open the file
    int handle = fat32_open(filename);
    if (handle < 0) {
        terminal_writestring("Error: Could not open file\n");
        return;
    }
    
    // Write content to the file
    const char* content = space_pos + 1;
    size_t content_len = strlen(content);
    
    uint32_t bytes_written = fat32_write(handle, content, content_len);
    fat32_close(handle);
    
    terminal_writestring("Wrote ");
    char buffer[16];
    itoa(bytes_written, buffer, 10);
    terminal_writestring(buffer);
    terminal_writestring(" bytes to ");
    terminal_writestring(filename);
    terminal_writestring("\n");
}

void cmd_mkdir() {
    // Extract the directory name from the command buffer
    char dirname[13];
    int i = 6; // Skip "mkdir "
    int j = 0;
    
    while (command_buffer[i] && j < 12) {
        dirname[j++] = command_buffer[i++];
    }
    dirname[j] = '\0';
    
    // Create the directory
    if (fat32_create(dirname, true)) {
        terminal_writestring("Directory created: ");
        terminal_writestring(dirname);
        terminal_writestring("\n");
    } else {
        terminal_writestring("Error creating directory\n");
    }
}

void cmd_rm() {
    // Extract the filename from the command buffer
    char filename[13];
    int i = 3; // Skip "rm "
    int j = 0;
    
    while (command_buffer[i] && j < 12) {
        filename[j++] = command_buffer[i++];
    }
    filename[j] = '\0';
    
    // Delete the file
    if (fat32_delete(filename)) {
        terminal_writestring("File deleted: ");
        terminal_writestring(filename);
        terminal_writestring("\n");
    } else {
        terminal_writestring("Error deleting file\n");
    }
}

// Updated process_command function to include new commands
void update_process_command() {
    // This will replace your existing process_command function
    // Add null terminator to command
    command_buffer[command_length] = '\0';
    
    // Skip to first non-space character
    char* cmd = command_buffer;
    while (*cmd == ' ') cmd++;
    
    // Check commands
    if (strcmp(cmd, "help")) {
        cmd_help();
    } else if (strcmp(cmd, "clear")) {
        cmd_clear();
    } else if (strcmp(cmd, "hello")) {
        cmd_hello();
    } else if (strcmp(cmd, "fsinfo")) {
        cmd_fsinfo();
    } else if (strcmp(cmd, "ls")) {
        cmd_ls();
    } else if (strncmp(cmd, "cat ", 4)) {
        cmd_cat();
    } else if (strncmp(cmd, "write ", 6)) {
        cmd_write();
    } else if (strncmp(cmd, "mkdir ", 6)) {
        cmd_mkdir();
    } else if (strncmp(cmd, "rm ", 3)) {
        cmd_rm();
    } else {
        terminal_writestring("Unknown command: ");
        terminal_writestring(cmd);
        terminal_writestring("\n");
    }
}

// Updated help command to include file system commands
void update_cmd_help() {
    terminal_writestring("Available commands:\n");
    terminal_writestring("  help    - Show this help message\n");
    terminal_writestring("  clear   - Clear the screen\n");
    terminal_writestring("  hello   - Display a greeting\n");
    terminal_writestring("  fsinfo  - Display filesystem information\n");
    terminal_writestring("  ls      - List files in root directory\n");
    terminal_writestring("  cat <file> - Display file contents\n");
    terminal_writestring("  write <file> <content> - Write to a file\n");
    terminal_writestring("  mkdir <name> - Create a directory\n");
    terminal_writestring("  rm <file> - Delete a file or empty directory\n");
}

// Entry point to initialize filesystem - call this from your kernel_main
void init_filesystem() {
    // Initialize ATA disk
    if (ata_init()) {
        // Initialize FAT32 filesystem
        if (fat32_init()) {
            // Initialize file handles
            fat32_init_file_handles();
            terminal_writestring("FAT32 filesystem initialized successfully\n");
        } else {
            terminal_writestring("Failed to initialize FAT32 filesystem\n");
        }
    } else {
        terminal_writestring("Failed to initialize ATA disk\n");
    }
}