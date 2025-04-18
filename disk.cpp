/*
 * SATA Controller Debug Utility
 * For bare metal AMD64 environment
 */
#include "kernel.h"
#include "iostream_wrapper.h"
#include "pci.h"
#include "stdlib_hooks.h" // Assumed to provide basic utilities if needed
#include "identify.h"       // Includes the string R/W functions now
#include "fat32.h"       // Includes the string R/W functions now

 // AHCI registers offsets (Keep these definitions)
#define AHCI_CAP        0x00  // Host Capabilities
#define AHCI_GHC        0x04  // Global Host Control
#define AHCI_IS         0x08  // Interrupt Status
#define AHCI_PI         0x0C  // Ports Implemented
#define AHCI_VS         0x10  // Version
#define AHCI_PORT_BASE  0x100 // Port registers base
#define AHCI_PORT_SIZE  0x80  // Size of port register space

// Port registers offsets (add to PORT_BASE + port_num * PORT_SIZE)
#define PORT_CLB        0x00  // Command List Base Address
#define PORT_CLBU       0x04  // Command List Base Address Upper 32 bits
#define PORT_FB         0x08  // FIS Base Address
#define PORT_FBU        0x0C  // FIS Base Address Upper 32 bits
#define PORT_IS         0x10  // Interrupt Status
#define PORT_IE         0x14  // Interrupt Enable
#define PORT_CMD        0x18  // Command and Status
#define PORT_TFD        0x20  // Task File Data
#define PORT_SIG        0x24  // Signature
#define PORT_SSTS       0x28  // SATA Status
#define PORT_SCTL       0x2C  // SATA Control
#define PORT_SERR       0x30  // SATA Error
#define PORT_SACT       0x34  // SATA Active
#define PORT_CI         0x38  // Command Issue

/*
 * FAT32 Implementation for Bare Metal AMD64
 * Builds on SATA Controller code from identify.h
 */

// --- Assumed Includes and Definitions (Normally in Headers) ---
#include <stdint.h>         // For uintXX_t types
#include <stddef.h>         // For NULL, size_t
#include <stdbool.h>        // For bool type
#include "stdlib_hooks.h"       // Includes the string R/W functions now

#include "iostream_wrapper.h" // Include your bare-metal cout wrapper
#include "fat32.h"            // Include FAT32 specific definitions (structs, constants)
#include "identify.h"            // Include FAT32 specific definitions (structs, constants)

// --- Placeholder Definitions (should be in fat32.h) ---
#ifndef FAT32_EOC_MIN
#define FAT32_EOC_MIN 0x0FFFFFF8 // Minimum value for End Of Chain marker
#endif
// --- End Placeholder Definitions ---


// --- Forward declarations ---

// Helper function for converting int to string (returns pointer to static buffer)
const char* to_string(int value);

// --- End Forward declarations ---


// --- Global Variables ---
// WARNING: The use of a single global buffer is risky in complex scenarios.
uint8_t fat32_buffer[FAT32_CLUSTER_SIZE]; // Assumed large enough for one cluster

bool fat32_initialized = false;
uint32_t fat32_bytes_per_cluster;
uint32_t fat32_fat1_start_sector;
uint32_t fat32_root_dir_first_cluster;
uint32_t fat32_data_start_sector;
uint32_t fat32_total_clusters;
uint64_t fat32_volume_size_bytes;

// --- Static Helper Function Prototypes ---
static uint32_t cluster_to_sector(uint32_t cluster);
static void fill_boot_sector(fat32_boot_sector_t* bs, uint64_t total_sectors, const char* volume_label);
static void fill_fsinfo_sector(fat32_fsinfo_t* fs_info, uint32_t total_clusters);
static uint32_t fat32_get_next_cluster_internal(uint64_t ahci_base, int port, uint32_t current_cluster, uint8_t* sector_buffer);
static int fat32_allocate_cluster_internal(uint64_t ahci_base, int port, uint32_t* new_cluster, uint8_t* sector_buffer);
static int fat32_free_cluster_chain_internal(uint64_t ahci_base, int port, uint32_t start_cluster, uint8_t* sector_buffer);
static int fat32_update_fat_entry(uint64_t ahci_base, int port, uint32_t cluster, uint32_t value, uint8_t* sector_buffer);


/*
 * Initialize the FAT32 file system
 * Reads boot sector and initializes global variables
 */
int fat32_init(uint64_t ahci_base, int port) {
    int result;
    // Use fat32_buffer temporarily for the boot sector read
    fat32_boot_sector_t* boot_sector = (fat32_boot_sector_t*)fat32_buffer;

    // Read boot sector (LBA 0) - Use explicit cast for count if needed, but declaration change should fix it.
    result = read_sectors(ahci_base, port, 0, 1, fat32_buffer);
    if (result != 0) {
        cout << "ERROR: Failed to read FAT32 boot sector.\n";
        return -1;
    }

    // Verify boot sector signature
     // Ensure FAT32_SIGNATURE is defined correctly as 0xAA55 in fat32.h
    if (boot_sector->boot_signature != FAT32_SIGNATURE) {
        cout << "ERROR: Invalid FAT32 boot signature (Expected 0xAA55, Got 0x" << boot_sector->boot_signature << ").\n";
        //return -2;
    }

    if (boot_sector->bytes_per_sector != FAT32_SECTOR_SIZE) {
        cout << "ERROR: Unsupported sector size: " << boot_sector->bytes_per_sector << "\n";
        return -3;
    }
    if (boot_sector->sectors_per_cluster == 0) {
         cout << "ERROR: Invalid sectors_per_cluster value (0).\n";
         return -4;
    }

    // Initialize global variables
    fat32_bytes_per_cluster = (uint32_t)boot_sector->bytes_per_sector * boot_sector->sectors_per_cluster;
    fat32_fat1_start_sector = boot_sector->reserved_sectors;
    fat32_root_dir_first_cluster = boot_sector->root_cluster;

    // Calculate the data area start sector
    uint32_t fat_size = (boot_sector->fat_size_16 != 0) ? boot_sector->fat_size_16 : boot_sector->fat_size_32;
    if (fat_size == 0) {
         cout << "ERROR: FAT size is zero.\n";
         return -5;
    }
    fat32_data_start_sector = boot_sector->reserved_sectors +
        ((uint32_t)boot_sector->num_fats * fat_size);

    // Calculate total clusters in the data area
    uint64_t total_sectors_64 = (boot_sector->total_sectors_16 == 0) ?
        boot_sector->total_sectors_32 : boot_sector->total_sectors_16;

    if (total_sectors_64 == 0) {
         cout << "ERROR: Total sectors is zero.\n";
         return -6;
    }
    if (fat32_data_start_sector >= total_sectors_64) {
        cout << "ERROR: Data start sector beyond total sectors.\n";
        return -7;
    }

    uint64_t data_sectors = total_sectors_64 - fat32_data_start_sector;
    fat32_total_clusters = data_sectors / boot_sector->sectors_per_cluster;

    // Basic check for FAT32 type based on cluster count
    if (fat32_total_clusters < 65525) {
        cout << "WARNING: Cluster count suggests FAT16/FAT12, not FAT32.\n";
        // Decide if this should be a hard error or just a warning
        // return -8;
    }


    // Calculate volume size in bytes
    fat32_volume_size_bytes = total_sectors_64 * boot_sector->bytes_per_sector;

    cout << "FAT32 Initialized:\n";
    cout << "  Bytes per sector: " << boot_sector->bytes_per_sector << "\n";
    cout << "  Sectors per cluster: " << (int)boot_sector->sectors_per_cluster << "\n";
    cout << "  Reserved sectors: " << boot_sector->reserved_sectors << "\n";
    cout << "  Number of FATs: " << (int)boot_sector->num_fats << "\n";
    cout << "  Sectors per FAT: " << fat_size << "\n";
    cout << "  Root directory cluster: " << boot_sector->root_cluster << "\n";
    cout << "  FSInfo Sector: " << boot_sector->fs_info << "\n";
    cout << "  Backup Boot Sector: " << boot_sector->backup_boot_sector << "\n";
    // Cast uint64_t for printing - potential truncation! Fix TerminalOutput::operator<<
    cout << "  Total Sectors: " << (unsigned int)total_sectors_64 << "\n";
    cout << "  Volume label: ";
    for (int i = 0; i < 11; i++) {
        cout << boot_sector->volume_label[i];
    }
    cout << "\n";
    cout << "  Total clusters: " << fat32_total_clusters << "\n";
    // Cast uint64_t for printing - potential truncation! Fix TerminalOutput::operator<<
    cout << "  Volume size: " << (unsigned int)(fat32_volume_size_bytes / (1024 * 1024)) << " MB\n";

    fat32_initialized = true;
    return 0;
}

/*
 * Format a volume as FAT32
 *
 * WARNING: This will destroy all data on the volume!
 */
int fat32_format_volume(uint64_t ahci_base, int port, uint64_t total_sectors, const char* volume_label) {
    int result;
    uint8_t sector_buffer[FAT32_SECTOR_SIZE]; // Local buffer for FSInfo/FAT writes

    cout << "Formatting volume as FAT32...\n";
    cout << "WARNING: This will erase all data on the target port!\n";

    // --- Create and Write Boot Sector ---
    for (uint32_t i = 0; i < FAT32_SECTOR_SIZE; i++) {
        fat32_buffer[i] = 0;
    }
    fat32_boot_sector_t* boot_sector = (fat32_boot_sector_t*)fat32_buffer;
    fill_boot_sector(boot_sector, total_sectors, volume_label); // Fills fat32_buffer

    uint32_t fat_sectors = boot_sector->fat_size_32;
     if (fat_sectors == 0) {
         cout << "ERROR: Calculated FAT size is zero during format.\n";
         return -0x10;
     }

    result = write_sectors(ahci_base, port, 0, 1, fat32_buffer);
    if (result != 0) { cout << "ERROR: Failed to write FAT32 boot sector.\n"; return -1; }

    if (boot_sector->backup_boot_sector > 0 && boot_sector->backup_boot_sector < boot_sector->reserved_sectors) {
        result = write_sectors(ahci_base, port, boot_sector->backup_boot_sector, 1, fat32_buffer);
        if (result != 0) { cout << "ERROR: Failed to write FAT32 backup boot sector.\n"; return -2; }
    } else { cout << "WARNING: Invalid or missing backup boot sector location.\n"; }

    // --- Create and Write FS Info Sector ---
    for (uint32_t i = 0; i < FAT32_SECTOR_SIZE; i++) { sector_buffer[i] = 0; }
    uint32_t data_sectors = total_sectors - (boot_sector->reserved_sectors + ((uint32_t)boot_sector->num_fats * fat_sectors));
    // Ensure sectors_per_cluster is not zero before division
    uint32_t spc = boot_sector->sectors_per_cluster;
    if (spc == 0) {
        cout << "ERROR: sectors_per_cluster is zero in boot sector data (format).\n";
        return -0x11; // Another unique error
    }
    uint32_t total_clusters = data_sectors / spc;

    fat32_fsinfo_t* fs_info = (fat32_fsinfo_t*)sector_buffer;
    fill_fsinfo_sector(fs_info, total_clusters);

     if (boot_sector->fs_info > 0 && boot_sector->fs_info < boot_sector->reserved_sectors) {
        result = write_sectors(ahci_base, port, boot_sector->fs_info, 1, sector_buffer);
        if (result != 0) { cout << "ERROR: Failed to write FAT32 FS Info sector.\n"; return -3; }
     } else { cout << "ERROR: Invalid or missing FSInfo sector location in boot sector.\n"; return -3; }

    // --- Initialize FAT Tables ---
    for (uint32_t i = 0; i < FAT32_SECTOR_SIZE; i++) { sector_buffer[i] = 0; }
    uint32_t* fat = (uint32_t*)sector_buffer;
    fat[0] = 0x0FFFFFF8 | (FAT32_MEDIA_DESCRIPTOR << 24);
    fat[1] = 0x0FFFFFFF;
    if (boot_sector->root_cluster < (FAT32_SECTOR_SIZE / sizeof(uint32_t))) { // Check bounds
        fat[boot_sector->root_cluster] = 0x0FFFFFFF;
    } else {
        cout << "ERROR: Root cluster index too large for first FAT sector init.\n";
        return -0x12;
    }


    uint32_t fat1_start = boot_sector->reserved_sectors;
    for (uint8_t fat_num = 0; fat_num < boot_sector->num_fats; fat_num++) {
        uint32_t fat_start_sector = fat1_start + (fat_num * fat_sectors);
        result = write_sectors(ahci_base, port, fat_start_sector, 1, sector_buffer);
        if (result != 0) { cout << "ERROR: Failed to write FAT32 FAT" << (int)fat_num + 1 << " first sector.\n"; return -4; }
    }

    // Clear remaining FAT sectors
    for (uint32_t i = 0; i < FAT32_SECTOR_SIZE; i++) { sector_buffer[i] = 0; }
    cout << "Writing remaining FAT sectors (this may take a while)...\n";
    for (uint8_t fat_num = 0; fat_num < boot_sector->num_fats; fat_num++) {
        uint32_t fat_start_sector = fat1_start + (fat_num * fat_sectors);
        for (uint32_t sector = 1; sector < fat_sectors; sector++) {
            result = write_sectors(ahci_base, port, fat_start_sector + sector, 1, sector_buffer);
            if (result != 0) { cout << "ERROR: Failed to write FAT32 FAT" << (int)fat_num + 1 << " sector " << sector << ".\n"; return -5; }
        }
         cout << "Finished writing FAT " << (int)fat_num + 1 << ".\n";
    }

    // --- Initialize Root Directory ---
    uint32_t bytes_per_cluster_local = (uint32_t)boot_sector->bytes_per_sector * spc; // Use calculated spc
    for (uint32_t i = 0; i < bytes_per_cluster_local; i++) { // Use calculated cluster size
        if (i < FAT32_CLUSTER_SIZE) { // Prevent overflow if definition is smaller
             fat32_buffer[i] = 0;
        } else break;
    }

    fat32_dir_entry_t* dir_entry = (fat32_dir_entry_t*)fat32_buffer;
    int label_len = strlen(volume_label);
    for (int i = 0; i < 11; i++) {
        if (i < label_len && volume_label[i] != '.') { dir_entry->name[i] = volume_label[i]; } else { dir_entry->name[i] = ' '; }
    }
    for (int i = 0; i < 3; i++) dir_entry->ext[i] = ' ';
    dir_entry->attributes = 0x08;

    uint16_t time = (10 << 11) | (30 << 5) | (0 >> 1);
    uint16_t date = ((2025 - 1980) << 9) | (4 << 5) | 18;
    dir_entry->creation_time_ms = 0; dir_entry->creation_time = time; dir_entry->creation_date = date;
    dir_entry->last_access_date = date; dir_entry->last_mod_time = time; dir_entry->last_mod_date = date;
    dir_entry->first_cluster_high = 0; dir_entry->first_cluster_low = 0; dir_entry->file_size = 0;

    uint32_t data_start = boot_sector->reserved_sectors + ((uint32_t)boot_sector->num_fats * fat_sectors);
    uint32_t root_dir_sector = data_start + ((boot_sector->root_cluster - 2) * spc);

    result = write_sectors(ahci_base, port, root_dir_sector, spc, fat32_buffer);
    if (result != 0) { cout << "ERROR: Failed to write FAT32 root directory.\n"; return -6; }

    cout << "Volume formatted successfully!\n";
    // Cast uint64_t for printing - potential truncation! Fix TerminalOutput::operator<<
    cout << "  Total sectors: " << (unsigned int)total_sectors << "\n";
    cout << "  Data sectors: " << (unsigned int)data_sectors << "\n";
    cout << "  Total clusters: " << total_clusters << "\n";
    // Cast uint64_t for printing - potential truncation! Fix TerminalOutput::operator<<
    uint64_t vol_size_bytes = total_sectors * boot_sector->bytes_per_sector;
    cout << "  Volume size: " << (unsigned int)(vol_size_bytes / (1024 * 1024)) << " MB\n";

    fat32_initialized = false;
    return fat32_init(ahci_base, port);
}


/*
 * Read a file from the FAT32 volume
 * Returns 0 on success
 */
int fat32_read_file(uint64_t ahci_base, int port, uint32_t dir_cluster,
    const char* filename, void* buffer, uint32_t buffer_size, uint32_t* bytes_read) {

    uint8_t fat_sector_buffer[FAT32_SECTOR_SIZE]; // Local buffer for FAT reads

    if (!fat32_initialized) { cout << "ERROR: FAT32 not initialized. Call fat32_init() first.\n"; return -1; }
    if (dir_cluster < 2 || dir_cluster >= fat32_total_clusters + 2) { cout << "ERROR: Invalid directory cluster " << dir_cluster << ".\n"; return -2; }
    if (!filename || filename[0] == '\0') { cout << "ERROR: Invalid filename.\n"; return -3; }
    if (!buffer || buffer_size == 0) { cout << "ERROR: Invalid buffer or zero size for file read.\n"; return -4; }
    if (bytes_read) { *bytes_read = 0; }

    char short_name[8]; char short_ext[3];
    for(int i=0; i<8; ++i) short_name[i] = ' '; for(int i=0; i<3; ++i) short_ext[i] = ' ';
    int name_len = strlen(filename); int ext_pos = -1; int name_part_len = name_len; int ext_part_len = 0;
    for (int i = name_len - 1; i >= 0; i--) { if (filename[i] == '.') { ext_pos = i; name_part_len = i; ext_part_len = name_len - (i + 1); break; } }
    for (int i = 0; i < 8; i++) { short_name[i] = (i < name_part_len) ? filename[i] : ' '; } // Case?
    if (ext_pos >= 0) { for (int i = 0; i < 3; i++) { short_ext[i] = (i < ext_part_len) ? filename[ext_pos + 1 + i] : ' '; } }
    else { for (int i = 0; i < 3; i++) short_ext[i] = ' '; }

    uint32_t current_search_cluster = dir_cluster;
    bool file_found = false; uint32_t file_first_cluster = 0; uint32_t file_size = 0;
    fat32_dir_entry_t* entry_ptr = NULL;

    while (current_search_cluster >= 2 && current_search_cluster < FAT32_EOC) {
        int result = fat32_read_cluster(ahci_base, port, current_search_cluster, fat32_buffer);
        if (result != 0) { cout << "ERROR: Failed to read directory cluster " << current_search_cluster << ".\n"; return -5; }

        fat32_dir_entry_t* dir_entries = (fat32_dir_entry_t*)fat32_buffer;
        int entries_per_cluster = fat32_bytes_per_cluster / sizeof(fat32_dir_entry_t);
        bool cluster_searched = false; // Renamed from file_found in inner loop

        for (int i = 0; i < entries_per_cluster; i++) {
            uint8_t first_byte = dir_entries[i].name[0];
            if (first_byte == 0x00) { cluster_searched = true; current_search_cluster = FAT32_EOC; break; }
            if (first_byte == 0xE5) continue;
            if (dir_entries[i].attributes == 0x0F) continue;
            if (dir_entries[i].attributes & 0x08 || dir_entries[i].attributes & 0x10) continue;

            bool name_match = true;
            for (int j = 0; j < 8; j++) { if (dir_entries[i].name[j] != short_name[j]) { name_match = false; break; } }
            if (name_match) {
                bool ext_match = true;
                for (int j = 0; j < 3; j++) { if (dir_entries[i].ext[j] != short_ext[j]) { ext_match = false; break; } }
                if (ext_match) {
                    file_found = true;
                    file_first_cluster = (uint32_t)dir_entries[i].first_cluster_high << 16 | dir_entries[i].first_cluster_low;
                    file_size = dir_entries[i].file_size;
                    entry_ptr = &dir_entries[i];
                    goto search_done;
                }
            }
        }
        if (current_search_cluster != FAT32_EOC) {
            uint32_t next_cluster = fat32_get_next_cluster_internal(ahci_base, port, current_search_cluster, fat_sector_buffer);
            current_search_cluster = next_cluster;
        }
    }
search_done:
    if (!file_found) { cout << "ERROR: File '" << filename << "' not found.\n"; return -6; }

    uint32_t current_file_cluster = file_first_cluster;
    uint32_t total_bytes_read = 0;
    uint8_t* out_buf = (uint8_t*)buffer;

    while (total_bytes_read < file_size && current_file_cluster >= 2 && current_file_cluster < FAT32_EOC) {
        int result = fat32_read_cluster(ahci_base, port, current_file_cluster, fat32_buffer);
        if (result != 0) { cout << "ERROR: Failed to read file data cluster " << current_file_cluster << ".\n"; if (bytes_read) *bytes_read = total_bytes_read; return -7; }

        uint32_t bytes_remaining_in_file = file_size - total_bytes_read;
        uint32_t bytes_to_copy = (bytes_remaining_in_file < fat32_bytes_per_cluster) ? bytes_remaining_in_file : fat32_bytes_per_cluster;
        if (total_bytes_read + bytes_to_copy > buffer_size) {
             bytes_to_copy = buffer_size - total_bytes_read;
             cout << "WARNING: User buffer too small for entire file. Truncating read.\n";
             if (bytes_to_copy == 0) break;
        }

        // Use memcpy for potentially faster copy
        memcpy(out_buf + total_bytes_read, fat32_buffer, bytes_to_copy);
        total_bytes_read += bytes_to_copy;

        if (total_bytes_read < file_size) {
            uint32_t next_cluster = fat32_get_next_cluster_internal(ahci_base, port, current_file_cluster, fat_sector_buffer);
            current_file_cluster = next_cluster;
            if (next_cluster >= FAT32_EOC && total_bytes_read < file_size) { cout << "WARNING: Unexpected end of cluster chain before reaching file size.\n"; break; }
            if (next_cluster < 2 && next_cluster != 0 && next_cluster < FAT32_EOC ) { cout << "WARNING: Invalid cluster number " << next_cluster << " found in chain.\n"; break; }
        }
    }

    if (bytes_read) { *bytes_read = total_bytes_read; }
     if (total_bytes_read < file_size && total_bytes_read < buffer_size && current_file_cluster < FAT32_EOC) {
          cout << "WARNING: Read stopped prematurely. Read " << total_bytes_read << " of " << file_size << " bytes.\n";
     }
    cout << "Successfully read " << total_bytes_read << " bytes from file '" << filename << "'.\n";
    return 0;
}


/*
 * Delete a file from the FAT32 volume
 * Returns 0 on success
 */
int fat32_delete_file(uint64_t ahci_base, int port, uint32_t dir_cluster, const char* filename) {

    uint8_t fat_sector_buffer[FAT32_SECTOR_SIZE];

    if (!fat32_initialized) { cout << "ERROR: FAT32 not initialized.\n"; return -1; }
    if (dir_cluster < 2 || dir_cluster >= fat32_total_clusters + 2) { cout << "ERROR: Invalid directory cluster " << dir_cluster << ".\n"; return -2; }
    if (!filename || filename[0] == '\0') { cout << "ERROR: Invalid filename.\n"; return -3; }

    char short_name[8]; char short_ext[3];
    for(int i=0; i<8; ++i) short_name[i] = ' '; for(int i=0; i<3; ++i) short_ext[i] = ' ';
    int name_len = strlen(filename); int ext_pos = -1; int name_part_len = name_len; int ext_part_len = 0;
    for (int i = name_len - 1; i >= 0; i--) { if (filename[i] == '.') { ext_pos = i; name_part_len = i; ext_part_len = name_len - (i + 1); break; } }
    for (int i = 0; i < 8; i++) { short_name[i] = (i < name_part_len) ? filename[i] : ' '; } // Case?
    if (ext_pos >= 0) { for (int i = 0; i < 3; i++) { short_ext[i] = (i < ext_part_len) ? filename[ext_pos + 1 + i] : ' '; } }
    else { for (int i = 0; i < 3; i++) short_ext[i] = ' '; }

    uint32_t current_search_cluster = dir_cluster;
    bool file_found = false; uint32_t file_first_cluster = 0;
    uint32_t file_dir_cluster = 0; int file_entry_index = -1;

    while (current_search_cluster >= 2 && current_search_cluster < FAT32_EOC) {
        int result = fat32_read_cluster(ahci_base, port, current_search_cluster, fat32_buffer);
        if (result != 0) { cout << "ERROR: Failed read dir cluster " << current_search_cluster << ".\n"; return -4; }

        fat32_dir_entry_t* dir_entries = (fat32_dir_entry_t*)fat32_buffer;
        int entries_per_cluster = fat32_bytes_per_cluster / sizeof(fat32_dir_entry_t);
        bool end_marker_hit = false;

        for (int i = 0; i < entries_per_cluster; i++) {
             uint8_t first_byte = dir_entries[i].name[0];
             if (first_byte == 0x00) { end_marker_hit = true; break; }
             if (first_byte == 0xE5) continue;
             if (dir_entries[i].attributes == 0x0F) continue;
             if (dir_entries[i].attributes & 0x08 || dir_entries[i].attributes & 0x10) continue;

             bool name_match = true;
             for (int j = 0; j < 8; j++) { if (dir_entries[i].name[j] != short_name[j]) { name_match = false; break; } }
             if (name_match) {
                 bool ext_match = true;
                 for (int j = 0; j < 3; j++) { if (dir_entries[i].ext[j] != short_ext[j]) { ext_match = false; break; } }
                 if (ext_match) {
                     file_found = true;
                     file_first_cluster = (uint32_t)dir_entries[i].first_cluster_high << 16 | dir_entries[i].first_cluster_low;
                     file_dir_cluster = current_search_cluster;
                     file_entry_index = i;
                     goto delete_search_done;
                 }
             }
        }
        if (!end_marker_hit) {
            uint32_t next_cluster = fat32_get_next_cluster_internal(ahci_base, port, current_search_cluster, fat_sector_buffer);
            current_search_cluster = next_cluster;
        } else { current_search_cluster = FAT32_EOC; }
    }
delete_search_done:
    if (!file_found) { cout << "ERROR: File '" << filename << "' not found for deletion.\n"; return -5; }

    if (file_first_cluster >= 2) {
        int result = fat32_free_cluster_chain_internal(ahci_base, port, file_first_cluster, fat_sector_buffer);
        if (result != 0) { cout << "WARNING: Failed to free file's clusters.\n"; }
    }

    // Assume fat32_buffer still holds the directory cluster data
    fat32_dir_entry_t* dir_entries = (fat32_dir_entry_t*)fat32_buffer;
    dir_entries[file_entry_index].name[0] = 0xE5;

    int result = fat32_write_cluster(ahci_base, port, file_dir_cluster, fat32_buffer);
    if (result != 0) { cout << "ERROR: Failed write dir cluster after marking deleted.\n"; return -8; }

    cout << "Successfully deleted file '" << filename << "'.\n";
    return 0;
}


/*
 * List files in a directory
 * Returns 0 on success
 */
int fat32_list_directory(uint64_t ahci_base, int port, uint32_t dir_cluster) {

    uint8_t fat_sector_buffer[FAT32_SECTOR_SIZE];

    if (!fat32_initialized) { cout << "ERROR: FAT32 not initialized.\n"; return -1; }
    if (dir_cluster < 2 || dir_cluster >= fat32_total_clusters + 2) { cout << "ERROR: Invalid directory cluster.\n"; return -2; }

    cout << "Directory listing (cluster " << dir_cluster << "):\n";
    cout << "-------------------------------------------------\n";
    cout << "Name         Type     Size        First Cluster\n";
    cout << "-------------------------------------------------\n";

    uint32_t current_search_cluster = dir_cluster;
    uint32_t total_files = 0; uint32_t total_dirs = 0; uint64_t total_size = 0;
    bool end_marker_hit = false;

    while (current_search_cluster >= 2 && current_search_cluster < FAT32_EOC) {
        int result = fat32_read_cluster(ahci_base, port, current_search_cluster, fat32_buffer);
        if (result != 0) { cout << "ERROR: Failed read dir cluster " << current_search_cluster << ".\n"; return -3; }

        fat32_dir_entry_t* dir_entries = (fat32_dir_entry_t*)fat32_buffer;
        int entries_per_cluster = fat32_bytes_per_cluster / sizeof(fat32_dir_entry_t);

        for (int i = 0; i < entries_per_cluster; i++) {
            uint8_t first_byte = dir_entries[i].name[0];
            if (first_byte == 0x00) { end_marker_hit = true; break; }
            if (first_byte == 0xE5) continue;
            if (dir_entries[i].attributes == 0x0F) continue;
            if (dir_entries[i].name[0] == '.' && dir_entries[i].name[1] == ' ') continue;
            if (dir_entries[i].name[0] == '.' && dir_entries[i].name[1] == '.' && dir_entries[i].name[2] == ' ') continue;

            char filename[13]; int name_pos = 0; bool has_ext = false;
            for (int j = 0; j < 8; j++) { if (dir_entries[i].name[j] != ' ') filename[name_pos++] = dir_entries[i].name[j]; else break; }
            for (int j = 0; j < 3; j++) { if (dir_entries[i].ext[j] != ' ') { has_ext = true; break; } }
            if (has_ext) {
                filename[name_pos++] = '.';
                for (int j = 0; j < 3; j++) { if (dir_entries[i].ext[j] != ' ') filename[name_pos++] = dir_entries[i].ext[j]; else break; }
            }
            filename[name_pos] = '\0';

            uint32_t first_cluster = (uint32_t)dir_entries[i].first_cluster_high << 16 | dir_entries[i].first_cluster_low;
            const char* type_str; uint32_t entry_size = dir_entries[i].file_size;
            if (dir_entries[i].attributes & 0x10) { type_str = "<DIR> "; total_dirs++; }
            else if (dir_entries[i].attributes & 0x08) { type_str = "<VOL> "; }
            else { type_str = "FILE  "; total_files++; total_size += entry_size; }

            cout << filename;
            for (uint32_t j = name_pos; j < 12; j++) cout << " ";
            cout << type_str << " ";

            if (dir_entries[i].attributes & 0x10) { cout << "         "; }
            else {
                // Use const char* directly from to_string
                const char* size_str = to_string(entry_size);
                int size_len = strlen(size_str);
                cout << size_str;
                for (int j = size_len; j < 9; j++) cout << " ";
            }
            cout << " " << first_cluster << "\n";
        }
        if (!end_marker_hit) {
            uint32_t next_cluster = fat32_get_next_cluster_internal(ahci_base, port, current_search_cluster, fat_sector_buffer);
            current_search_cluster = next_cluster;
        } else { current_search_cluster = FAT32_EOC; }
    }

    cout << "-------------------------------------------------\n";
    // Cast uint64_t for printing - potential truncation! Fix TerminalOutput::operator<<
    cout << "Total: " << total_files << " files, " << total_dirs << " directories, "
         << (unsigned int)total_size << " bytes\n";

    return 0;
}

/*
 * Helper: Convert int to string. Returns ptr to static buffer.
 * WARNING: Not thread-safe. OK for single-task bare-metal.
 */
const char* to_string(int value) { // Changed return type to const char*
    static char buffer[32];
    char *ptr = buffer + sizeof(buffer) - 1;
    *ptr = '\0';
    bool is_negative = false;
    unsigned int u_value;

    if (value == 0) { *--ptr = '0'; return ptr; }

    if (value < 0) { is_negative = true; u_value = -value; }
    else { u_value = value; }

    while (u_value > 0) {
        *--ptr = '0' + (u_value % 10);
        u_value /= 10;
        if (ptr == buffer) break;
    }

    if (is_negative) {
         if (ptr != buffer) { *--ptr = '-'; }
         else { return "OVF"; } // Error: Buffer too small
    }
    return ptr;
}


/*
 * Write a file to the FAT32 volume
 * If the file exists, it is overwritten.
 * Returns 0 on success
 */
int fat32_write_file(uint64_t ahci_base, int port, uint32_t dir_cluster,
    const char* filename, const void* buffer, uint32_t buffer_size) {

    uint8_t fat_sector_buffer[FAT32_SECTOR_SIZE];

    if (!fat32_initialized) { cout << "ERROR: FAT32 not initialized.\n"; return -1; }
    if (dir_cluster < 2 || dir_cluster >= fat32_total_clusters + 2) { cout << "ERROR: Invalid directory cluster.\n"; return -2; }
    if (!filename || filename[0] == '\0') { cout << "ERROR: Invalid filename.\n"; return -3; }
    if (!buffer && buffer_size > 0) { cout << "ERROR: Invalid null buffer for non-zero size.\n"; return -4; }

    char short_name[8]; char short_ext[3];
    for(int i=0; i<8; ++i) short_name[i] = ' '; for(int i=0; i<3; ++i) short_ext[i] = ' ';
    int name_len = strlen(filename); int ext_pos = -1; int name_part_len = name_len; int ext_part_len = 0;
    for (int i = name_len - 1; i >= 0; i--) { if (filename[i] == '.') { ext_pos = i; name_part_len = i; ext_part_len = name_len - (i + 1); break; } }
    for (int i = 0; i < 8; i++) { short_name[i] = (i < name_part_len) ? filename[i] : ' '; } // Case?
    if (ext_pos >= 0) { for (int i = 0; i < 3; i++) { short_ext[i] = (i < ext_part_len) ? filename[ext_pos + 1 + i] : ' '; } }
    else { for (int i = 0; i < 3; i++) short_ext[i] = ' '; }

    uint32_t current_search_cluster = dir_cluster;
    bool existing_file_found = false; bool free_slot_found = false;
    uint32_t file_first_cluster = 0; uint32_t entry_dir_cluster = 0; int entry_index = -1;

    while (current_search_cluster >= 2 && current_search_cluster < FAT32_EOC) {
        int result = fat32_read_cluster(ahci_base, port, current_search_cluster, fat32_buffer);
        if (result != 0) { cout << "ERROR: Failed read dir cluster " << current_search_cluster << " for write.\n"; return -5; }

        fat32_dir_entry_t* dir_entries = (fat32_dir_entry_t*)fat32_buffer;
        int entries_per_cluster = fat32_bytes_per_cluster / sizeof(fat32_dir_entry_t);
        bool end_marker_hit = false;

        for (int i = 0; i < entries_per_cluster; i++) {
             uint8_t first_byte = dir_entries[i].name[0];
             if (first_byte == 0x00) { if (!free_slot_found) { free_slot_found = true; entry_dir_cluster = current_search_cluster; entry_index = i; } end_marker_hit = true; break; }
             if (first_byte == 0xE5) { if (!free_slot_found) { free_slot_found = true; entry_dir_cluster = current_search_cluster; entry_index = i; } continue; }
             if (dir_entries[i].attributes == 0x0F || dir_entries[i].attributes & 0x08 || dir_entries[i].attributes & 0x10) continue;

             bool name_match = true;
             for (int j = 0; j < 8; j++) { if (dir_entries[i].name[j] != short_name[j]) { name_match = false; break; } }
             if (name_match) {
                 bool ext_match = true;
                 for (int j = 0; j < 3; j++) { if (dir_entries[i].ext[j] != short_ext[j]) { ext_match = false; break; } }
                 if (ext_match) {
                     existing_file_found = true; file_first_cluster = (uint32_t)dir_entries[i].first_cluster_high << 16 | dir_entries[i].first_cluster_low;
                     entry_dir_cluster = current_search_cluster; entry_index = i;
                     goto write_search_done;
                 }
             }
        }

        if (!existing_file_found && !end_marker_hit) {
             uint32_t next_cluster = fat32_get_next_cluster_internal(ahci_base, port, current_search_cluster, fat_sector_buffer);
             if (next_cluster >= FAT32_EOC) {
                 if (!free_slot_found) {
                     cout << "INFO: Extending directory cluster chain.\n";
                     uint32_t new_dir_cluster;
                     result = fat32_allocate_cluster_internal(ahci_base, port, &new_dir_cluster, fat_sector_buffer);
                     if (result != 0) { cout << "ERROR: Failed allocate new dir cluster.\n"; return -6; }
                     result = fat32_update_fat_entry(ahci_base, port, current_search_cluster, new_dir_cluster, fat_sector_buffer);
                     if (result != 0) { cout << "ERROR: Failed link new dir cluster.\n"; return -7; }
                     memset(fat32_buffer, 0, fat32_bytes_per_cluster); // Use memset
                     result = fat32_write_cluster(ahci_base, port, new_dir_cluster, fat32_buffer);
                     if (result != 0) { cout << "ERROR: Failed clear new dir cluster.\n"; return -8; }
                     free_slot_found = true; entry_dir_cluster = new_dir_cluster; entry_index = 0;
                     goto write_search_done;
                 } else { current_search_cluster = FAT32_EOC; }
             } else if (next_cluster == 0xFFFFFFFF) { cout << "ERROR: Failed to get next cluster after " << current_search_cluster << ".\n"; return -10; }
             else { current_search_cluster = next_cluster; }
        } else { current_search_cluster = FAT32_EOC; }
    }
write_search_done:

    if (!existing_file_found && !free_slot_found) { cout << "ERROR: Could not find or create a directory entry slot.\n"; return -9; }

    if (existing_file_found && file_first_cluster >= 2) {
        cout << "INFO: File '" << filename << "' exists. Deleting old content.\n";
        int result = fat32_free_cluster_chain_internal(ahci_base, port, file_first_cluster, fat_sector_buffer);
        if (result != 0) { cout << "WARNING: Failed to free existing file's clusters.\n"; }
        file_first_cluster = 0;
    }

    uint32_t new_first_cluster = 0;
    uint32_t clusters_needed = (buffer_size + fat32_bytes_per_cluster - 1) / fat32_bytes_per_cluster;

    if (buffer_size > 0) {
        uint32_t previous_allocated_cluster = 0;
        const uint8_t* in_buf = (const uint8_t*)buffer;
        uint32_t buffer_offset = 0;
        cout << "INFO: Allocating " << clusters_needed << " clusters for file data.\n";

        for (uint32_t i = 0; i < clusters_needed; i++) {
            uint32_t allocated_cluster;
            int result = fat32_allocate_cluster_internal(ahci_base, port, &allocated_cluster, fat_sector_buffer);
            if (result != 0) {
                cout << "ERROR: Failed allocate cluster " << i + 1 << ".\n";
                if (new_first_cluster >= 2) { cout << "INFO: Freeing partial chain.\n"; fat32_free_cluster_chain_internal(ahci_base, port, new_first_cluster, fat_sector_buffer); }
                return -10;
            }
            if (i == 0) { new_first_cluster = allocated_cluster; }
            else {
                 result = fat32_update_fat_entry(ahci_base, port, previous_allocated_cluster, allocated_cluster, fat_sector_buffer);
                 if (result != 0) { cout << "ERROR: Failed link cluster " << previous_allocated_cluster << " to " << allocated_cluster << ".\n"; if (new_first_cluster >= 2) { cout << "INFO: Freeing partial chain.\n"; fat32_free_cluster_chain_internal(ahci_base, port, new_first_cluster, fat_sector_buffer); } return -11; }
            }
            previous_allocated_cluster = allocated_cluster;

            uint32_t bytes_to_write_this_cluster = (buffer_size - buffer_offset < fat32_bytes_per_cluster) ? (buffer_size - buffer_offset) : fat32_bytes_per_cluster;
            // Use memcpy
            memcpy(fat32_buffer, in_buf + buffer_offset, bytes_to_write_this_cluster);
            if (bytes_to_write_this_cluster < fat32_bytes_per_cluster) {
                 // Zero pad the rest of the cluster buffer
                 memset(fat32_buffer + bytes_to_write_this_cluster, 0, fat32_bytes_per_cluster - bytes_to_write_this_cluster);
            }

            result = fat32_write_cluster(ahci_base, port, allocated_cluster, fat32_buffer);
            if (result != 0) { cout << "ERROR: Failed write data to cluster " << allocated_cluster << ".\n"; if (new_first_cluster >= 2) { cout << "INFO: Freeing partial chain.\n"; fat32_free_cluster_chain_internal(ahci_base, port, new_first_cluster, fat_sector_buffer); } return -12; }
            buffer_offset += bytes_to_write_this_cluster;
        }
    }

    int result = fat32_read_cluster(ahci_base, port, entry_dir_cluster, fat32_buffer);
    if (result != 0) {
         cout << "ERROR: Failed re-read dir cluster " << entry_dir_cluster << " for final update.\n";
         if (new_first_cluster >= 2) { cout << "WARNING: Orphaned data created. Cleanup attempt.\n"; fat32_free_cluster_chain_internal(ahci_base, port, new_first_cluster, fat_sector_buffer); }
         return -13;
    }

    fat32_dir_entry_t* entry = &((fat32_dir_entry_t*)fat32_buffer)[entry_index];

    if (!existing_file_found) {
        for (int i = 0; i < 8; i++) entry->name[i] = short_name[i];
        for (int i = 0; i < 3; i++) entry->ext[i] = short_ext[i];
        entry->attributes = 0x20; // Archive
        uint16_t time = (10 << 11) | (30 << 5) | (0 >> 1); // Use RTC
        uint16_t date = ((2025 - 1980) << 9) | (4 << 5) | 18; // Use RTC
        entry->creation_time_ms = 0; entry->creation_time = time; entry->creation_date = date;
    }

    entry->file_size = buffer_size;
    entry->first_cluster_high = (uint16_t)(new_first_cluster >> 16);
    entry->first_cluster_low = (uint16_t)(new_first_cluster & 0xFFFF);

    uint16_t mod_time = (10 << 11) | (31 << 5) | (0 >> 1); // Use RTC
    uint16_t mod_date = ((2025 - 1980) << 9) | (4 << 5) | 18; // Use RTC
    entry->last_mod_time = mod_time; entry->last_mod_date = mod_date; entry->last_access_date = mod_date;

    result = fat32_write_cluster(ahci_base, port, entry_dir_cluster, fat32_buffer);
    if (result != 0) { cout << "ERROR: Failed write updated dir cluster " << entry_dir_cluster << ".\n"; return -14; }

    cout << "Successfully wrote " << buffer_size << " bytes to file '" << filename << "'.\n";
    return 0;
}


/*
 * Read a cluster from the FAT32 volume into the specified buffer
 */
int fat32_read_cluster(uint64_t ahci_base, int port, uint32_t cluster_num, void* buffer) {
    if (!fat32_initialized) { cout << "ERROR: FAT32 not initialized.\n"; return -1; }
    if (cluster_num < 2 || cluster_num >= fat32_total_clusters + 2) { cout << "ERROR: Invalid cluster number " << cluster_num << " for read.\n"; return -2; }

    uint64_t sector = cluster_to_sector(cluster_num);
    uint32_t sectors_per_cluster = fat32_bytes_per_cluster / FAT32_SECTOR_SIZE;
    if (sectors_per_cluster == 0) { // Sanity check
        cout << "ERROR: Sectors per cluster is zero in read_cluster.\n"; return -3;
    }
    // Cast count to uint16_t for read_sectors call
    if (sectors_per_cluster > 0xFFFF) {
         cout << "ERROR: Cluster size exceeds uint16_t sector count limit.\n"; return -4;
    }
    return read_sectors(ahci_base, port, sector, (uint16_t)sectors_per_cluster, buffer);
}

/*
 * Write a cluster to the FAT32 volume from the specified buffer
 */
int fat32_write_cluster(uint64_t ahci_base, int port, uint32_t cluster_num, const void* buffer) {
    if (!fat32_initialized) { cout << "ERROR: FAT32 not initialized.\n"; return -1; }
    if (cluster_num < 2 || cluster_num >= fat32_total_clusters + 2) { cout << "ERROR: Invalid cluster number " << cluster_num << " for write.\n"; return -2; }

    uint64_t sector = cluster_to_sector(cluster_num);
    uint32_t sectors_per_cluster = fat32_bytes_per_cluster / FAT32_SECTOR_SIZE;
     if (sectors_per_cluster == 0) { // Sanity check
        cout << "ERROR: Sectors per cluster is zero in write_cluster.\n"; return -3;
    }
    // Cast count to uint16_t for write_sectors call
    if (sectors_per_cluster > 0xFFFF) {
         cout << "ERROR: Cluster size exceeds uint16_t sector count limit.\n"; return -4;
    }
    return write_sectors(ahci_base, port, sector, (uint16_t)sectors_per_cluster, const_cast<void*>(buffer)); // MODIFIED LINE
}


//-----------------------------------------------------------------------------
// Internal Helper Functions using dedicated sector buffer for FAT/FSInfo access
//-----------------------------------------------------------------------------

static uint32_t fat32_get_next_cluster_internal(uint64_t ahci_base, int port, uint32_t current_cluster, uint8_t* sector_buffer) {
    if (!fat32_initialized) return 0xFFFFFFFF; // Error
    if (current_cluster < 2 || current_cluster >= fat32_total_clusters + 2) return 0xFFFFFFFF; // Error

    uint32_t fat_offset_bytes = current_cluster * 4;
    uint32_t fat_sector = fat32_fat1_start_sector + (fat_offset_bytes / FAT32_SECTOR_SIZE);
    uint32_t entry_offset_in_sector = fat_offset_bytes % FAT32_SECTOR_SIZE;

    // Read sector - use explicit cast for count
    int result = read_sectors(ahci_base, port, fat_sector, 1, sector_buffer);
    if (result != 0) { cout << "ERROR: Failed read FAT sector " << fat_sector << " in get_next_cluster.\n"; return 0xFFFFFFFF; }

    uint32_t next_cluster = *((uint32_t*)(sector_buffer + entry_offset_in_sector));
    next_cluster &= 0x0FFFFFFF;

    if (next_cluster >= FAT32_EOC_MIN) { return FAT32_EOC; } // Use defined EOC_MIN
    if (next_cluster >= 2 && next_cluster < fat32_total_clusters + 2) { return next_cluster; }
    if (next_cluster == 0) { cout << "WARNING: Cluster 0 link in chain for " << current_cluster << ".\n"; return FAT32_EOC; }
    cout << "WARNING: Invalid cluster link " << next_cluster << " for cluster " << current_cluster << ".\n";
    return 0xFFFFFFFF; // Error
}

static int fat32_update_fat_entry(uint64_t ahci_base, int port, uint32_t cluster, uint32_t value, uint8_t* sector_buffer) {
     if (!fat32_initialized) return -1;
     if (cluster < 2 || cluster >= fat32_total_clusters + 2) return -2;

     uint32_t fat_offset_bytes = cluster * 4;
     uint32_t fat_sector = fat32_fat1_start_sector + (fat_offset_bytes / FAT32_SECTOR_SIZE);
     uint32_t entry_offset_in_sector = fat_offset_bytes % FAT32_SECTOR_SIZE;
     // Calculate sectors_per_fat more robustly if possible, e.g., from init data
     fat32_boot_sector_t bs_temp; // Need boot sector info
     int res = read_sectors(ahci_base, port, 0, 1, &bs_temp);
     if (res != 0) { cout << "ERROR: Failed read boot sector in update_fat.\n"; return -99; }
     uint32_t sectors_per_fat = (bs_temp.fat_size_16 != 0) ? bs_temp.fat_size_16 : bs_temp.fat_size_32;
     if (sectors_per_fat == 0) { cout << "ERROR: Sectors per FAT is zero in update_fat.\n"; return -98;}


     value &= 0x0FFFFFFF;

     // --- Update Primary FAT ---
     int result = read_sectors(ahci_base, port, fat_sector, 1, sector_buffer);
     if (result != 0) { cout << "ERROR: Failed read FAT1 sector " << fat_sector << " for update.\n"; return -3; }
     *((uint32_t*)(sector_buffer + entry_offset_in_sector)) = value;
     result = write_sectors(ahci_base, port, fat_sector, 1, sector_buffer);
     if (result != 0) { cout << "ERROR: Failed write FAT1 sector " << fat_sector << " for update.\n"; return -4; }

     // --- Update Mirror FATs ---
     for (uint8_t fat_num = 1; fat_num < bs_temp.num_fats; fat_num++) { // Use num_fats from boot sector
         uint32_t mirror_fat_sector = fat32_fat1_start_sector + (fat_num * sectors_per_fat) + (fat_offset_bytes / FAT32_SECTOR_SIZE);
         // Update value in buffer (it's already there from primary update)
         // *((uint32_t*)(sector_buffer + entry_offset_in_sector)) = value; // Redundant if buffer wasn't reused
         result = write_sectors(ahci_base, port, mirror_fat_sector, 1, sector_buffer);
         if (result != 0) { cout << "WARNING: Failed write mirror FAT" << (int)fat_num + 1 << " sector " << mirror_fat_sector << ".\n"; }
     }
     return 0;
}


static int fat32_allocate_cluster_internal(uint64_t ahci_base, int port, uint32_t* new_cluster, uint8_t* sector_buffer) {
    if (!fat32_initialized) return -1;
    if (new_cluster == NULL) return -2;

    fat32_boot_sector_t* bs_temp = (fat32_boot_sector_t*)sector_buffer;
    int result = read_sectors(ahci_base, port, 0, 1, sector_buffer);
    if (result != 0) { cout << "ERROR: Failed read boot sector in allocate_cluster.\n"; return -3; }
    uint16_t fs_info_sector = bs_temp->fs_info;
    if (fs_info_sector == 0 || fs_info_sector >= bs_temp->reserved_sectors) { cout << "ERROR: Invalid FSInfo sector location.\n"; return -4; }

    result = read_sectors(ahci_base, port, fs_info_sector, 1, sector_buffer);
    if (result != 0) { cout << "ERROR: Failed read FS Info sector " << fs_info_sector << ".\n"; return -4; }

    fat32_fsinfo_t* fs_info = (fat32_fsinfo_t*)sector_buffer;
    if (fs_info->lead_signature != 0x41615252 || fs_info->structure_signature != 0x61417272) {
        cout << "WARNING: Invalid FSInfo signature.\n";
        fs_info->free_cluster_count = 0xFFFFFFFF; fs_info->next_free_cluster = 2;
    }

    uint32_t free_count = fs_info->free_cluster_count;
    if (free_count == 0) { cout << "ERROR: No free clusters available.\n"; return -5; }

    uint32_t start_cluster = fs_info->next_free_cluster;
    if (start_cluster < 2 || start_cluster >= fat32_total_clusters + 2) { start_cluster = 2; }

    uint32_t current_scan_cluster = start_cluster;
    uint32_t checked_clusters = 0; uint32_t found_free_cluster = 0;

    while (checked_clusters < fat32_total_clusters) {
        uint32_t fat_offset_bytes = current_scan_cluster * 4;
        uint32_t fat_sector = fat32_fat1_start_sector + (fat_offset_bytes / FAT32_SECTOR_SIZE);
        uint32_t entry_offset_in_sector = fat_offset_bytes % FAT32_SECTOR_SIZE;

        result = read_sectors(ahci_base, port, fat_sector, 1, sector_buffer);
        if (result != 0) { cout << "ERROR: Failed read FAT sector " << fat_sector << " during scan.\n"; return -6; }

        uint32_t cluster_value = *((uint32_t*)(sector_buffer + entry_offset_in_sector));
        if ((cluster_value & 0x0FFFFFFF) == FAT32_FREE_CLUSTER) { found_free_cluster = current_scan_cluster; break; }

        current_scan_cluster++;
        if (current_scan_cluster >= fat32_total_clusters + 2) { current_scan_cluster = 2; }
        checked_clusters++;
        if (current_scan_cluster == start_cluster && checked_clusters > 0) break;
    }

    if (found_free_cluster == 0) { cout << "ERROR: No free clusters found.\n"; return -7; }

     result = fat32_update_fat_entry(ahci_base, port, found_free_cluster, FAT32_EOC, sector_buffer);
     if (result != 0) { cout << "ERROR: Failed mark allocated cluster " << found_free_cluster << " as EOC.\n"; return -8; }

    // Update FS Info (re-read first)
    result = read_sectors(ahci_base, port, fs_info_sector, 1, sector_buffer);
     if (result != 0) { cout << "WARNING: Failed re-read FS Info for update.\n"; }
     else {
         fs_info = (fat32_fsinfo_t*)sector_buffer;
         if (fs_info->lead_signature == 0x41615252 && fs_info->structure_signature == 0x61417272) {
             if (fs_info->free_cluster_count != 0xFFFFFFFF && fs_info->free_cluster_count > 0) { fs_info->free_cluster_count--; }
             fs_info->next_free_cluster = found_free_cluster + 1;
             if (fs_info->next_free_cluster >= fat32_total_clusters + 2) { fs_info->next_free_cluster = 2; }
             result = write_sectors(ahci_base, port, fs_info_sector, 1, sector_buffer);
             if (result != 0) { cout << "WARNING: Failed write updated FS Info.\n"; }
         }
     }

    // Zero out the cluster
    memset(fat32_buffer, 0, fat32_bytes_per_cluster); // Use memset
    result = fat32_write_cluster(ahci_base, port, found_free_cluster, fat32_buffer);
    if (result != 0) { cout << "WARNING: Failed zero allocated cluster " << found_free_cluster << ".\n"; }

    *new_cluster = found_free_cluster;
    return 0;
}


static int fat32_free_cluster_chain_internal(uint64_t ahci_base, int port, uint32_t start_cluster, uint8_t* sector_buffer) {
    if (!fat32_initialized) return -1;
    if (start_cluster < 2 || start_cluster >= fat32_total_clusters + 2) return -2;

    uint32_t freed_count = 0;
    uint32_t current_cluster = start_cluster;

    while (current_cluster >= 2 && current_cluster < FAT32_EOC) {
        uint32_t next_cluster = fat32_get_next_cluster_internal(ahci_base, port, current_cluster, sector_buffer);
        int result = fat32_update_fat_entry(ahci_base, port, current_cluster, FAT32_FREE_CLUSTER, sector_buffer);
        if (result != 0) { cout << "ERROR: Failed mark cluster " << current_cluster << " free.\n"; return -3; }
        freed_count++;
        if (next_cluster == 0xFFFFFFFF) { cout << "ERROR: Error reading next cluster after " << current_cluster << ".\n"; current_cluster = FAT32_EOC; }
        else { current_cluster = next_cluster; }
    }

    if (freed_count > 0) {
        fat32_boot_sector_t bs_temp;
        int result = read_sectors(ahci_base, port, 0, 1, &bs_temp);
        if (result != 0) { cout << "WARNING: Failed read boot sector after free.\n"; }
        else {
            uint16_t fs_info_sector = bs_temp.fs_info;
             if (fs_info_sector > 0 && fs_info_sector < bs_temp.reserved_sectors) {
                 result = read_sectors(ahci_base, port, fs_info_sector, 1, sector_buffer);
                 if (result != 0) { cout << "WARNING: Failed read FS Info after free.\n"; }
                 else {
                    fat32_fsinfo_t* fs_info = (fat32_fsinfo_t*)sector_buffer;
                     if (fs_info->lead_signature == 0x41615252 && fs_info->structure_signature == 0x61417272) {
                         if (fs_info->free_cluster_count != 0xFFFFFFFF) { fs_info->free_cluster_count += freed_count; }
                         fs_info->next_free_cluster = start_cluster;
                         result = write_sectors(ahci_base, port, fs_info_sector, 1, sector_buffer);
                         if (result != 0) { cout << "WARNING: Failed write updated FS Info after free.\n"; }
                     } else { cout << "WARNING: Invalid FSInfo signature; not updating count.\n"; }
                 }
             } else { cout << "WARNING: Invalid FSInfo location; cannot update count.\n"; }
        }
    }
    cout << "INFO: Freed " << freed_count << " clusters starting from " << start_cluster << ".\n";
    return 0;
}


//-----------------------------------------------------------------------------
// Helper Functions (Static)
//-----------------------------------------------------------------------------

static uint32_t cluster_to_sector(uint32_t cluster) {
    if (cluster < 2) return 0;
    // Avoid division by zero if init failed or BPB is corrupt
    if (FAT32_SECTOR_SIZE == 0) return 0;
    uint32_t sectors_per_cluster = fat32_bytes_per_cluster / FAT32_SECTOR_SIZE;
    if (sectors_per_cluster == 0) return 0; // Avoid division by zero in calculation
    return fat32_data_start_sector + ((cluster - 2) * sectors_per_cluster);
}


static void fill_boot_sector(fat32_boot_sector_t* bs, uint64_t total_sectors, const char* volume_label) {
    if (!bs) return;
    memset(bs, 0, sizeof(fat32_boot_sector_t)); // Zero out the structure first

    bs->jump_boot[0] = 0xEB; bs->jump_boot[1] = 0x58; bs->jump_boot[2] = 0x90;
    strncpy(bs->oem_name, "MYOSFAT3", 8); // Use strncpy for safety

    bs->bytes_per_sector = FAT32_SECTOR_SIZE;
    if (total_sectors * FAT32_SECTOR_SIZE >= 32ULL * 1024 * 1024 * 1024) bs->sectors_per_cluster = 64;
    else if (total_sectors * FAT32_SECTOR_SIZE >= 16ULL * 1024 * 1024 * 1024) bs->sectors_per_cluster = 32;
    else if (total_sectors * FAT32_SECTOR_SIZE >= 2ULL * 1024 * 1024 * 1024) bs->sectors_per_cluster = 16;
    else if (total_sectors * FAT32_SECTOR_SIZE >= 512 * 1024 * 1024) bs->sectors_per_cluster = 8;
    else bs->sectors_per_cluster = 4; // Ensure this isn't 0

    bs->reserved_sectors = FAT32_RESERVED_SECTORS; bs->num_fats = FAT32_NUM_FATS; bs->root_entries = 0;
    bs->total_sectors_16 = 0; bs->media_type = FAT32_MEDIA_DESCRIPTOR; bs->fat_size_16 = 0;
    bs->sectors_per_track = 63; bs->num_heads = 255; bs->hidden_sectors = 0;

    if (total_sectors > 0xFFFFFFFF) { cout << "WARNING: Volume size exceeds 32-bit limit!\n"; bs->total_sectors_32 = 0xFFFFFFFF; }
    else { bs->total_sectors_32 = (uint32_t)total_sectors; }

    uint64_t approx_data_sectors = total_sectors - bs->reserved_sectors;
    uint64_t approx_total_clusters = (bs->sectors_per_cluster > 0) ? approx_data_sectors / bs->sectors_per_cluster : 0;
    uint32_t fat_size_32 = (bs->bytes_per_sector > 0) ? (uint32_t)(((approx_total_clusters * 4) + bs->bytes_per_sector - 1 ) / bs->bytes_per_sector) : 0;
    if (fat_size_32 == 0) { cout << "ERROR: Calculated FAT size is zero in fill_boot_sector.\n"; /* Handle error? */ fat_size_32 = 1;} // Avoid zero

    bs->fat_size_32 = fat_size_32; bs->extended_flags = 0; bs->fs_version = 0;
    bs->root_cluster = FAT32_ROOT_CLUSTER; bs->fs_info = 1; bs->backup_boot_sector = 6;

    bs->drive_number = 0x80;
    // *** VERIFY THIS FIELD NAME in your fat32.h struct definition ***
    bs->extended_boot_signature = 0x29; // Corrected field name (ASSUMED!)
    bs->volume_id = 0x12345678; // Use a real random/time based ID later

    memset(bs->volume_label, ' ', 11); // Fill with spaces first
    int label_len = volume_label ? strlen(volume_label) : 0;
    for (int i = 0; i < 11 && i < label_len; i++) { if (volume_label[i] != '.') bs->volume_label[i] = volume_label[i]; } // Case?
    strncpy(bs->fs_type, "FAT32   ", 8);

    ((uint8_t*)bs)[510] = 0x55; ((uint8_t*)bs)[511] = 0xAA; // Boot signature
}


static void fill_fsinfo_sector(fat32_fsinfo_t* fs_info, uint32_t total_clusters) {
    if (!fs_info) return;
    memset(fs_info, 0, sizeof(fat32_fsinfo_t)); // Zero out structure

    fs_info->lead_signature = 0x41615252; fs_info->structure_signature = 0x61417272;
    fs_info->free_cluster_count = (total_clusters > 0) ? total_clusters - 1 : 0;
    fs_info->next_free_cluster = (total_clusters > 1) ? 3 : 2;

    ((uint8_t*)fs_info)[510] = 0x55; ((uint8_t*)fs_info)[511] = 0xAA; // Trail signature part
}


int fat32_create_entry(uint64_t ahci_base, int port, uint32_t parent_dir_cluster,
                       const char* name, uint8_t attributes,
                       uint32_t* out_entry_cluster, int* out_entry_index,
                       uint32_t* allocated_data_cluster)
{
    uint8_t fat_sector_buffer[FAT32_SECTOR_SIZE];

    if (!fat32_initialized) return -1;
    if (parent_dir_cluster < 2 || parent_dir_cluster >= fat32_total_clusters + 2) return -2;
    if (!name || name[0] == '\0') return -3;
    if (!out_entry_cluster || !out_entry_index) return -4;

    *out_entry_cluster = 0; *out_entry_index = -1;
    if (allocated_data_cluster) *allocated_data_cluster = 0;

    char short_name[8]; char short_ext[3];
    for(int i=0; i<8; ++i) short_name[i] = ' '; for(int i=0; i<3; ++i) short_ext[i] = ' ';
    int name_len = strlen(name); int ext_pos = -1; int name_part_len = name_len; int ext_part_len = 0;
    for (int i = name_len - 1; i >= 0; i--) { if (name[i] == '.') { ext_pos = i; name_part_len = i; ext_part_len = name_len - (i + 1); break; } }
    for (int i = 0; i < 8; i++) { short_name[i] = (i < name_part_len) ? name[i] : ' '; }
    // Corrected 'filename' to 'name' here:
    if (ext_pos >= 0) { for (int i = 0; i < 3; i++) { short_ext[i] = (i < ext_part_len) ? name[ext_pos + 1 + i] : ' '; } }
    else { for (int i = 0; i < 3; i++) short_ext[i] = ' '; }

    uint32_t current_search_cluster = parent_dir_cluster;
    uint32_t first_free_slot_cluster = 0; int first_free_slot_index = -1;

    while (current_search_cluster >= 2 && current_search_cluster < FAT32_EOC) {
        int result = fat32_read_cluster(ahci_base, port, current_search_cluster, fat32_buffer);
        if (result != 0) { cout << "ERROR: Failed read dir cluster " << current_search_cluster << ".\n"; return -5; }

        fat32_dir_entry_t* dir_entries = (fat32_dir_entry_t*)fat32_buffer;
        int entries_per_cluster = fat32_bytes_per_cluster / sizeof(fat32_dir_entry_t);
        bool end_marker_hit = false;

        for (int i = 0; i < entries_per_cluster; i++) {
             uint8_t first_byte = dir_entries[i].name[0];
             if (first_byte == 0x00) { if (first_free_slot_index == -1) { first_free_slot_cluster = current_search_cluster; first_free_slot_index = i; } end_marker_hit = true; break; }
             if (first_byte == 0xE5) { if (first_free_slot_index == -1) { first_free_slot_cluster = current_search_cluster; first_free_slot_index = i; } continue; }
             if (dir_entries[i].attributes == 0x0F) continue;

             bool name_match = true;
             for (int j = 0; j < 8; j++) { if (dir_entries[i].name[j] != short_name[j]) { name_match = false; break; } }
             if (name_match) {
                 bool ext_match = true;
                 for (int j = 0; j < 3; j++) { if (dir_entries[i].ext[j] != short_ext[j]) { ext_match = false; break; } }
                 if (ext_match) { cout << "ERROR: Entry '" << name << "' already exists.\n"; return -6; }
             }
        }

        if (end_marker_hit || first_free_slot_index != -1) { current_search_cluster = FAT32_EOC; }
        else {
             uint32_t next_cluster = fat32_get_next_cluster_internal(ahci_base, port, current_search_cluster, fat_sector_buffer);
             if (next_cluster >= FAT32_EOC) {
                 cout << "INFO: Extending directory cluster chain for create.\n";
                 uint32_t new_dir_cluster;
                 result = fat32_allocate_cluster_internal(ahci_base, port, &new_dir_cluster, fat_sector_buffer);
                 if (result != 0) { cout << "ERROR: Failed allocate new dir cluster.\n"; return -7; }
                 result = fat32_update_fat_entry(ahci_base, port, current_search_cluster, new_dir_cluster, fat_sector_buffer);
                 if (result != 0) { cout << "ERROR: Failed link new dir cluster.\n"; return -8; }
                 memset(fat32_buffer, 0, fat32_bytes_per_cluster);
                 result = fat32_write_cluster(ahci_base, port, new_dir_cluster, fat32_buffer);
                 if (result != 0) { cout << "ERROR: Failed clear new dir cluster.\n"; return -9; }
                 first_free_slot_cluster = new_dir_cluster; first_free_slot_index = 0;
                 current_search_cluster = FAT32_EOC;
             } else if (next_cluster == 0xFFFFFFFF) { cout << "ERROR: Failed get next cluster after " << current_search_cluster << ".\n"; return -10; }
             else { current_search_cluster = next_cluster; }
        }
    }

    if (first_free_slot_index == -1) { cout << "ERROR: Could not find/create dir entry slot.\n"; return -11; }

    uint32_t data_cluster = 0;
    if (allocated_data_cluster != NULL) {
         int result = fat32_allocate_cluster_internal(ahci_base, port, &data_cluster, fat_sector_buffer);
         if (result != 0) { cout << "ERROR: Failed allocate data cluster.\n"; return -12; }
         *allocated_data_cluster = data_cluster;

         if (attributes & 0x10) { // If directory, create '.' and '..'
              memset(fat32_buffer, 0, fat32_bytes_per_cluster);
              fat32_dir_entry_t* dot_entries = (fat32_dir_entry_t*)fat32_buffer;
              dot_entries[0].name[0] = '.'; memset(dot_entries[0].name + 1, ' ', 7); memset(dot_entries[0].ext, ' ', 3);
              dot_entries[0].attributes = 0x10;
              dot_entries[0].first_cluster_low = (uint16_t)(data_cluster & 0xFFFF); dot_entries[0].first_cluster_high = (uint16_t)(data_cluster >> 16);
              // TODO: Set dot entry times

              dot_entries[1].name[0] = '.'; dot_entries[1].name[1] = '.'; memset(dot_entries[1].name + 2, ' ', 6); memset(dot_entries[1].ext, ' ', 3);
              dot_entries[1].attributes = 0x10;
              dot_entries[1].first_cluster_low = (uint16_t)(parent_dir_cluster & 0xFFFF); dot_entries[1].first_cluster_high = (uint16_t)(parent_dir_cluster >> 16);
              // TODO: Set dot dot entry times

              result = fat32_write_cluster(ahci_base, port, data_cluster, fat32_buffer);
              if (result != 0) { cout << "ERROR: Failed write '.' '..' to new dir cluster " << data_cluster << ".\n"; return -13; }
         }
    }

    int result = fat32_read_cluster(ahci_base, port, first_free_slot_cluster, fat32_buffer);
    if (result != 0) { cout << "ERROR: Failed re-read dir cluster " << first_free_slot_cluster << ".\n"; return -14; }

    fat32_dir_entry_t* entry = &((fat32_dir_entry_t*)fat32_buffer)[first_free_slot_index];
    for(int i=0; i<8; ++i) entry->name[i] = short_name[i]; for(int i=0; i<3; ++i) entry->ext[i] = short_ext[i];
    entry->attributes = attributes;

    uint16_t time = (11 << 11) | (00 << 5) | (0 >> 1); // Use RTC
    uint16_t date = ((2025 - 1980) << 9) | (4 << 5) | 18; // Use RTC
    entry->creation_time_ms = 0; entry->creation_time = time; entry->creation_date = date;
    entry->last_access_date = date; entry->last_mod_time = time; entry->last_mod_date = date;

    entry->first_cluster_high = (uint16_t)(data_cluster >> 16); entry->first_cluster_low = (uint16_t)(data_cluster & 0xFFFF);
    entry->file_size = 0;

    result = fat32_write_cluster(ahci_base, port, first_free_slot_cluster, fat32_buffer);
    if (result != 0) { cout << "ERROR: Failed write updated dir cluster " << first_free_slot_cluster << ".\n"; return -15; }

    *out_entry_cluster = first_free_slot_cluster; *out_entry_index = first_free_slot_index;

    cout << "INFO: Created new entry '" << name << "' in dir cluster " << first_free_slot_cluster << " index " << first_free_slot_index << ".\n";
    return 0;
}
// Function to print hex value with label (Keep this function)
void print_hex(const char* label, uint32_t value) {
    cout << label;

    // Convert to hex
    char hex_chars[16] = { '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F' };
    char buffer[11]; // Increased size for 0x + 8 digits + null

    buffer[0] = '0';
    buffer[1] = 'x';

    // Fill from right to left for potentially shorter numbers if desired, but fixed 8 is fine too.
    for (int i = 0; i < 8; i++) {
        uint8_t nibble = (value >> (28 - i * 4)) & 0xF;
        buffer[2 + i] = hex_chars[nibble];
    }
    buffer[10] = '\0';

    cout << buffer << "\n";
}

// Helper to print 64-bit hex
void print_hex64(const char* label, uint64_t value) {
    cout << label;
    char hex_chars[16] = { '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F' };
    char buffer[19]; // 0x + 16 digits + null

    buffer[0] = '0';
    buffer[1] = 'x';
    for (int i = 0; i < 16; i++) {
        uint8_t nibble = (value >> (60 - i * 4)) & 0xF;
        buffer[2 + i] = hex_chars[nibble];
    }
    buffer[18] = '\0';
    cout << buffer << "\n";
}


// Simple string to unsigned int conversion (basic, no error checking)
// Assumes valid decimal number input.
unsigned int simple_stou(const char* str) {
    unsigned int res = 0;
    while (*str >= '0' && *str <= '9') {
        res = res * 10 + (*str - '0');
        str++;
    }
    return res;
}

// Simple string to unsigned long long conversion (basic, no error checking)
// Assumes valid decimal number input.
uint64_t simple_stoull(const char* str) {
    uint64_t res = 0;
    while (*str >= '0' && *str <= '9') {
        res = res * 10 + (*str - '0');
        str++;
    }
    return res;
}


// Function to decode port status (Keep this function)
void decode_port_status(uint32_t ssts) {
    uint8_t det = ssts & 0xF;
    uint8_t ipm = (ssts >> 8) & 0xF;

    cout << "  Device Detection: ";
    switch (det) {
    case 0: cout << "No device detected, PHY offline"; break;
    case 1: cout << "Device present but no communication"; break;
    case 3: cout << "Device present and communication established"; break;
    case 4: cout << "PHY offline, in BIST or loopback mode"; break;
    default: cout << "Unknown state"; break;
    }
    cout << "\n";

    cout << "  Interface Power Management: ";
    switch (ipm) {
    case 0: cout << "Not present, disabled"; break;
    case 1: cout << "Active state"; break;
    case 2: cout << "Partial power management state"; break;
    case 6: cout << "Slumber power management state"; break;
    case 8: cout << "DevSleep power management state"; break;
    default: cout << "Unknown state"; break;
    }
    cout << "\n";
}

// Function to decode task file data (Keep this function)
void decode_task_file(uint32_t tfd) {
    uint8_t status = tfd & 0xFF;
    uint8_t error = (tfd >> 8) & 0xFF;

    cout << "  Status register: ";
    if (status == 0 && error == 0) {
        cout << "(No error reported)"; // Avoid printing nothing if status is 0
    }
    else {
        if (status & 0x80) cout << "BSY ";
        if (status & 0x40) cout << "DRDY ";
        if (status & 0x20) cout << "DF ";
        if (status & 0x10) cout << "DSC ";
        if (status & 0x08) cout << "DRQ ";
        if (status & 0x04) cout << "CORR ";
        if (status & 0x02) cout << "IDX ";
        if (status & 0x01) cout << "ERR ";
    }
    cout << "\n";

    if (status & 0x01) {
        cout << "  Error register: ";
        if (error & 0x80) cout << "ICRC ";
        if (error & 0x40) cout << "UNC ";
        if (error & 0x20) cout << "MC ";
        if (error & 0x10) cout << "IDNF ";
        if (error & 0x08) cout << "MCR ";
        if (error & 0x04) cout << "ABRT ";
        if (error & 0x02) cout << "TK0NF ";
        if (error & 0x01) cout << "AMNF ";
        cout << "\n";
    }
}

// Function to decode port command register (Keep this function)
void decode_port_cmd(uint32_t cmd) {
    cout << "  Command register: ";
    if (cmd & 0x0001) cout << "ST ";
    if (cmd & 0x0002) cout << "SUD ";
    if (cmd & 0x0004) cout << "POD ";
    if (cmd & 0x0008) cout << "CLO ";
    if (cmd & 0x0010) cout << "FRE ";
    if (cmd & 0x0020) cout << "MPSS ";
    if (cmd & 0x4000) cout << "FR ";
    if (cmd & 0x8000) cout << "CR ";
    cout << "\n";
}



// Required AHCI/Port defines if not included elsewhere
#ifndef AHCI_PI
#define AHCI_PI         0x0C  // Ports Implemented (relative to ahci_base)
#define AHCI_PORT_BASE  0x100 // Port registers base offset
#define AHCI_PORT_SIZE  0x80  // Size of port register space
#define PORT_SIG        0x24  // Signature (relative to port base)
#define PORT_SSTS       0x28  // SATA Status (relative to port base)
#endif

// Assumes identify.h provides these or similar
extern uint8_t data_buffer[];
extern bool lba48_available;
extern void print_hex(const char* label, uint32_t value); // For debugging output

/**
 * @brief Prompts user and attempts to format a specific AHCI port as FAT32.
 * @param ahci_base Base address of the AHCI controller.
 * @param port_to_format The port number (0-31) to attempt formatting.
 * @return 0 on successful format (driver initialized), negative error code on failure or cancellation.
 */
int run_fat32_format(uint64_t ahci_base, int port_to_format) {

    cout << "\n--- Attempting Format on Port " << port_to_format << " ---\n";

    // 1. Validate Port Number and Implementation
    uint32_t pi = read_mem32(ahci_base + AHCI_PI); // Ports Implemented
    if (port_to_format < 0 || port_to_format > 31 || !(pi & (1 << port_to_format))) {
         cout << "ERROR: Invalid or unimplemented port number: " << port_to_format << "\n";
         return -1; // Invalid port error
    }

    // 2. Check Port Status and Device Signature
    uint64_t port_addr = ahci_base + AHCI_PORT_BASE + (port_to_format * AHCI_PORT_SIZE);
    uint32_t sig = read_mem32(port_addr + PORT_SIG);
    uint32_t ssts = read_mem32(port_addr + PORT_SSTS);
    uint8_t det = ssts & 0xF; // Device Detection status

    // Check if it's a potentially formattable SATA drive with comms established
    // Allow SATA (0x0101) or SATAPI (0xEB14) for flexibility, although formatting ATAPI doesn't make sense usually.
    if (!((sig == 0x00000101 || sig == 0xEB140101) && det == 3)) {
         cout << "ERROR: Port " << port_to_format
              << " does not appear to have an active SATA/SATAPI drive ready for formatting.\n";
         print_hex(" Signature: ", sig);
         print_hex(" SStatus: ", ssts);
         return -2; // Device not ready/suitable error
    }
     if (sig != 0x00000101) {
           cout << "WARNING: Target device is not a standard SATA drive (possibly ATAPI). Formatting may not be appropriate.\n";
     }

    // 3. Get Volume Label
    cout << "Enter Volume Label for Port " << port_to_format
         << " (max 11 chars, no spaces/dots recommended, ENTER for none): ";
    char label_buf[16]; // Allow slightly larger input buffer
    cin >> label_buf; // Assuming cin reads until space/newline

    // Handle empty input - use default or empty label
    if (label_buf[0] == '\0') {
        // Option 1: Use a default label
        // strcpy(label_buf, "NO NAME    ");
        // Option 2: Ensure it's truly empty for format function (which should handle it)
        // Keep label_buf as "" or handle in fill_boot_sector
        cout << "Using empty volume label.\n";
    }
    // Optional: Add more validation (length, allowed chars, uppercase conversion)


    // 4. Get Drive Size via IDENTIFY Command
    cout << "Sending IDENTIFY to Port " << port_to_format << " to determine size...\n";
    lba48_available = false; // Reset flag before call
    int id_res = send_identify_command(ahci_base, port_to_format);
    uint64_t total_sectors_fmt = 0;

    if (id_res != 0) {
        cout << "ERROR: Failed to send IDENTIFY command (Code: " << id_res
             << "). Cannot determine size for format.\n";
        return -4; // IDENTIFY command failed
    }

    // Extract size from identify data (assuming global data_buffer)
    uint16_t* id_data = (uint16_t*)data_buffer;
    if (lba48_available) { // Check flag set by send_identify_command
        total_sectors_fmt = ((uint64_t)id_data[100]) | ((uint64_t)id_data[101] << 16) |
                            ((uint64_t)id_data[102] << 32) | ((uint64_t)id_data[103] << 48);
    } else if (id_data[49] & (1 << 9)) { // Check LBA28 support bit
        total_sectors_fmt = ((uint32_t)id_data[60]) | ((uint32_t)id_data[61] << 16);
    }

    // FAT32 requires a minimum number of clusters (usually resulting in drives > 32MB)
    // Let's add a basic check - this threshold might need tuning.
    // A volume requiring < 65525 clusters should typically be FAT16.
    uint32_t sectors_per_cluster_est = 8; // Estimate typical 4KB cluster size for check
    if (total_sectors_fmt / sectors_per_cluster_est < 65525) {
         cout << "WARNING: Drive size might be too small for FAT32. Consider using FAT16.\n";
    }

    if (total_sectors_fmt == 0) {
        cout << "ERROR: Could not determine valid drive size from IDENTIFY data.\n";
        return -5; // Cannot get size error
    }

    // 5. Final Confirmation
    cout << "Drive size: " << (unsigned int)total_sectors_fmt << " sectors "; // Cast warning
    uint64_t size_mib = (total_sectors_fmt * 512) / (1024 * 1024);
    cout << "(" << (unsigned int)size_mib << " MiB approx).\n"; // Cast warning
    cout << "\n*** WARNING: About to format Port " << port_to_format << "! ALL DATA WILL BE LOST! ***\n";
    cout << "Format with label '" << (label_buf[0] == '\0' ? "<NONE>" : label_buf) << "'? (Type 'YES' exactly to confirm): ";
    char confirm_resp[8];
    cin >> confirm_resp;

    // Explicit confirmation check using strcmp for safety
    if (strcmp(confirm_resp, "YES") == 0)
    {
        // 6. Call Format Function
        cout << "Formatting Port " << port_to_format << "...\n";
        // Use empty string if label was empty, otherwise use buffer
        const char* label_to_use = (label_buf[0] == '\0') ? "" : label_buf;
        int format_result = fat32_format_volume(ahci_base, port_to_format, total_sectors_fmt, label_to_use);

        // 7. Report Result
        if (format_result == 0) {
            cout << "--- Format SUCCESSFUL (Port " << port_to_format << ") ---\n";
            // FAT32 driver should be initialized now by fat32_format_volume
            return 0; // Success
        } else {
            cout << "--- Format FAILED (Port " << port_to_format << ", Code: " << format_result << ") ---\n";
            return -6; // Format function failed
        }
    } else {
        cout << "Format cancelled.\n";
        return -7; // User cancelled error
    }
}


// Debug function to fully examine SATA controller state
void fs() {
    cout << "Filesystem Debug\n";
    cout << "--------------------\n";

    // Find AHCI controller via PCI
    uint64_t ahci_base = 0;
    uint16_t bus, dev, func;
    uint16_t ahci_bus = 0, ahci_dev = 0, ahci_func = 0; // Store location

    for (bus = 0; bus < 256 && !ahci_base; bus++) {
        for (dev = 0; dev < 32 && !ahci_base; dev++) {
            for (func = 0; func < 8 && !ahci_base; func++) {
                // Check if device exists first (Vendor ID != 0xFFFF)
                uint32_t vendor_device_check = pci_read_config_dword(bus, dev, func, 0x00);
                if ((vendor_device_check & 0xFFFF) == 0xFFFF) {
                    continue; // No device here
                }

                uint32_t class_reg = pci_read_config_dword(bus, dev, func, 0x08);
                uint8_t class_code = (class_reg >> 24) & 0xFF;
                uint8_t subclass = (class_reg >> 16) & 0xFF;
                uint8_t prog_if = (class_reg >> 8) & 0xFF;

                if (class_code == 0x01 && subclass == 0x06 && prog_if == 0x01) {
                    uint32_t bar5 = pci_read_config_dword(bus, dev, func, 0x24);
                    // Check if BAR5 is memory mapped and non-zero
                    if ((bar5 & 0x1) == 0 && (bar5 & ~0xF) != 0) {
                        ahci_base = bar5 & ~0xF;
                        ahci_bus = bus;
                        ahci_dev = dev;
                        ahci_func = func;

                        cout << "Found AHCI controller at PCI ";
                        cout << (int)bus << ":" << (int)dev << "." << (int)func << "\n"; // Use dot separator common practice

                        // Get vendor and device ID
                        uint32_t vendor_device = pci_read_config_dword(bus, dev, func, 0x00);
                        uint16_t vendor_id = vendor_device & 0xFFFF;
                        uint16_t device_id = (vendor_device >> 16) & 0xFFFF;

                        // Use print_hex for consistency (need to adapt for 16-bit)
                        print_hex(" Vendor ID: ", vendor_id); // Assuming print_hex handles width ok
                        print_hex(" Device ID: ", device_id);

                        cout << "\nPress enter to continue...\n\n";
                        char input[2]; // Allow for potential newline char
                        cin >> input; // Read a line to consume potential newline
                    }
                }
            }
        }
    }

    if (!ahci_base) {
        cout << "No AHCI controller found or BAR5 not valid.\n";
        return;
    }

    // Print ABAR address using helper
    print_hex64("AHCI Base Address (ABAR): ", ahci_base);
    cout << "\n";

    // Read and print AHCI global registers
    uint32_t cap = read_mem32(ahci_base + AHCI_CAP);
    uint32_t ghc = read_mem32(ahci_base + AHCI_GHC);
    uint32_t is = read_mem32(ahci_base + AHCI_IS);
    uint32_t pi = read_mem32(ahci_base + AHCI_PI);
    uint32_t vs = read_mem32(ahci_base + AHCI_VS);

    print_hex("Capabilities (CAP):       ", cap);
    print_hex("Global Host Control (GHC):", ghc);
    print_hex("Interrupt Status (IS):    ", is);
    print_hex("Ports Implemented (PI):   ", pi);
    print_hex("Version (VS):             ", vs);

    // Check if AHCI mode is enabled
    cout << "AHCI Mode (GHC.AE):       " << ((ghc & 0x80000000) ? "Enabled" : "Disabled") << "\n\n";

    // Get number of ports and scan each implemented port
    cout << "Port Status:\n";
    cout << "------------\n";

    bool any_device_found = false;
    for (int i = 0; i < 8; i++) {
        if (pi & (1 << i)) {
            uint64_t port_addr = ahci_base + AHCI_PORT_BASE + (i * AHCI_PORT_SIZE);

            cout << "Port " << i << ":\n";

            // Read port registers
            uint32_t ssts = read_mem32(port_addr + PORT_SSTS);
            uint32_t tfd = read_mem32(port_addr + PORT_TFD);
            uint32_t sig = read_mem32(port_addr + PORT_SIG);
            uint32_t cmd = read_mem32(port_addr + PORT_CMD);
            uint32_t serr = read_mem32(port_addr + PORT_SERR);

            // Print raw values
            print_hex("  SSTS (SATA Status):   ", ssts);
            print_hex("  TFD (Task File Data): ", tfd);
            print_hex("  SIG (Signature):      ", sig);
            print_hex("  CMD (Command/Status): ", cmd);
            print_hex("  SERR (SATA Error):    ", serr);

            // Decode port status
            decode_port_status(ssts);
            decode_task_file(tfd);
            decode_port_cmd(cmd);

            // Check signature to identify device type
            cout << "  Device type: ";
            bool is_sata_drive = (sig == 0x00000101);
            if (is_sata_drive) {
                cout << "SATA Drive (Non-ATAPI)";
                any_device_found = true;
            }
            else if (sig == 0xEB140101) {
                cout << "SATAPI Drive (ATAPI)";
                any_device_found = true; // Can still potentially read/write using ATAPI commands
            }
            else if (sig == 0xC33C0101) {
                cout << "Enclosure Management Bridge (SEMB)";
            }
            else if (sig == 0x96690101) {
                cout << "Port Multiplier";
            }
            else {
                // Check detection status again
                uint8_t det = ssts & 0xF;
                if (det == 0 || det == 4) {
                    cout << "No Device Detected";
                }
                else {
                    print_hex("Unknown Device Signature: ", sig);
                }
            }
            cout << "\n";

            // Print Command List Base Address and FIS Base Address if FRE or ST is set
            if (cmd & (HBA_PORT_CMD_FRE | HBA_PORT_CMD_ST)) {
                uint64_t clb = ((uint64_t)read_mem32(port_addr + PORT_CLBU) << 32) | read_mem32(port_addr + PORT_CLB);
                uint64_t fb = ((uint64_t)read_mem32(port_addr + PORT_FBU) << 32) | read_mem32(port_addr + PORT_FB);
                print_hex64("  Command List Base (CLB):", clb);
                print_hex64("  FIS Base (FB):          ", fb);
            }


            cout << "\nPress enter to continue...\n\n";
            char input[2];
            cin >> input;
        }
    }

    // --- IDENTIFY Command Section ---
    if (any_device_found) { // Only offer if we found something plausible
        cout << "\nSend IDENTIFY commands to detected SATA/SATAPI devices? (y/n): ";
        char response[4]; // Increase size for safety
        cin >> response;

        if (response[0] == 'y' || response[0] == 'Y') {
            for (int i = 0; i < 8; i++) {
                if (pi & (1 << i)) {
                    uint64_t port_addr = ahci_base + AHCI_PORT_BASE + (i * AHCI_PORT_SIZE);
                    uint32_t sig = read_mem32(port_addr + PORT_SIG);
                    // Only try IDENTIFY on plausible devices (SATA/SATAPI) and if communication seems established
                    uint32_t ssts = read_mem32(port_addr + PORT_SSTS);
                    uint8_t det = ssts & 0xF;

                    if ((sig == 0x00000101 || sig == 0xEB140101) && det == 3) {
                        cout << "Send IDENTIFY to port " << i << "? (y/n): ";
                        char port_response[4];
                        cin >> port_response;

                        if (port_response[0] == 'y' || port_response[0] == 'Y') {
                            int result = send_identify_command(ahci_base, i);
                            if (result == 0) {
                                cout << "IDENTIFY command for port " << i << " succeeded.\n";
                            }
                            else {
                                cout << "IDENTIFY command for port " << i << " failed (Error code: " << result << ").\n";
                            }
                            cout << "\nPress enter to continue...\n\n";
                            char input[2];
                            cin >> input;
                        }
                    }
                }
            }
        }
    }
    else {
        cout << "\nNo active SATA/SATAPI devices found, skipping IDENTIFY command option.\n";
    }

if (any_device_found) {
        cout << "\nPerform Format / FAT32 Test operations? (y/n): ";
        char fs_ops_resp[4];
        cin >> fs_ops_resp;

        if (fs_ops_resp[0] == 'y' || fs_ops_resp[0] == 'Y') {

            // --- Format Option ---
            cout << "\nFormat a port as FAT32? *** WARNING: DATA LOSS! *** (y/n): ";
            char format_resp[4];
            cin >> format_resp;

            if (format_resp[0] == 'y' || format_resp[0] == 'Y') {
                cout << "Enter Port number to format (0-31): ";
                char port_input[8];
                cin >> port_input;
                int port_to_format = simple_stou(port_input); // Assuming simple_stou handles non-numbers gracefully or you add checks

                // Validate port
                uint32_t current_pi = read_mem32(ahci_base + AHCI_PI);
                if (port_to_format < 0 || port_to_format > 31 || !(current_pi & (1 << port_to_format))) {
                     cout << "ERROR: Invalid or unimplemented port number: " << port_to_format << "\n";
                } else {
                    uint64_t port_addr = ahci_base + AHCI_PORT_BASE + (port_to_format * AHCI_PORT_SIZE);
                    uint32_t sig = read_mem32(port_addr + PORT_SIG);
                    uint32_t ssts = read_mem32(port_addr + PORT_SSTS);
                    uint8_t det = ssts & 0xF;

                    // Check if it's a potentially formattable SATA drive
                    if (!((sig == 0x00000101 || sig == 0xEB140101) && det == 3)) {
                         cout << "ERROR: Port " << port_to_format << " does not appear to have an active SATA/SATAPI drive.\n";
                    } else {
                         // Get Volume Label
                         cout << "Enter Volume Label (max 11 chars, no spaces/dots recommended): ";
                         char label_buf[16]; // Allow slightly larger input buffer
                         cin >> label_buf;
                         // Optional: Validate/sanitize label_buf here (e.g., uppercase, length check)

                         // Get Drive Size via IDENTIFY
                         cout << "Sending IDENTIFY to Port " << port_to_format << " to determine size...\n";
                         // Ensure lba48_available is reset or managed if multiple IDENTIFYs run
                         lba48_available = false;
                         int id_res = send_identify_command(ahci_base, port_to_format);
                         uint64_t total_sectors_fmt = 0;

                         if (id_res == 0) {
                              // Assuming data_buffer holds identify data and lba48_available is set globally by display/send_identify
                              uint16_t* id_data = (uint16_t*)data_buffer;
                              if (lba48_available) { // Check flag set by send_identify_command's processing
                                   total_sectors_fmt = ((uint64_t)id_data[100]) | ((uint64_t)id_data[101] << 16) |
                                                       ((uint64_t)id_data[102] << 32) | ((uint64_t)id_data[103] << 48);
                              } else if (id_data[49] & (1 << 9)) { // LBA28
                                   total_sectors_fmt = ((uint32_t)id_data[60]) | ((uint32_t)id_data[61] << 16);
                              }

                              if (total_sectors_fmt > 0) {
                                   cout << "Drive size: " << (unsigned int)total_sectors_fmt << " sectors.\n"; // Cast warning
                                   cout << "\n*** LAST CHANCE! Format Port " << port_to_format
                                        << " (" << (unsigned int)(total_sectors_fmt / (2*1024)) << " MiB approx)" // Rough MiB calculation
                                        << " with label '" << label_buf << "'? (y/n): ";
                                   char confirm_resp[4];
                                   cin >> confirm_resp;

                                   if (confirm_resp[0] == 'y' || confirm_resp[0] == 'Y') {
                                       cout << "Formatting...\n";
                                       int format_result = fat32_format_volume(ahci_base, port_to_format, total_sectors_fmt, label_buf);
                                       if (format_result == 0) {
                                            cout << "--- Format SUCCESSFUL (Port " << port_to_format << ") ---\n";
                                            // FAT32 driver is now initialized for this port by fat32_format_volume
                                       } else {
                                            cout << "--- Format FAILED (Port " << port_to_format << ", Code: " << format_result << ") ---\n";
                                       }
                                   } else { cout << "Format cancelled.\n"; }
                              } else { cout << "ERROR: Could not determine valid drive size from IDENTIFY data.\n"; }
                         } else { cout << "ERROR: Failed to send IDENTIFY command (Code: " << id_res << "). Cannot determine size for format.\n"; }
                    } // End check if formattable drive
                } // End check port validity
            } // End if format response 'y'
        } // end if fs_ops_resp 'y'
    } // end if (any_device_found)
}
