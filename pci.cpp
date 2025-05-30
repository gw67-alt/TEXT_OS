#include "hardware_specs.h"
#include "interrupts.h"
#include "iostream_wrapper.h"
// #include "kernel.h" // Assuming provides type definitions if not in types.h
#include "stdlib_hooks.h"
#include "terminal_hooks.h"
#include "terminal_io.h"
#include "types.h" // Ensure uint8_t, uint16_t, uint32_t, uint64_t, size_t are defined

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC
#define PCI_VENDOR_ID 0x00      // Offset for Vendor ID / Device ID
#define PCI_COMMAND_REGISTER 0x04 // Offset for Command Register / Status Register
#define PCI_CLASS_REVISION 0x08 // Offset for Class Code, Subclass, Prog IF, Revision ID
#define PCI_HEADER_TYPE 0x0E    // Offset for Header Type (part of DWORD at 0x0C)


// Direct implementation of outl/inl functions
void direct_outl(uint16_t port, uint32_t value) {
    asm volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

uint32_t direct_inl(uint16_t port) {
    uint32_t ret;
    asm volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// PCI device structure - ensure it has the class/subclass/progIF/revision fields
struct pci_device {
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t command;
    uint16_t status;       // Typically read from offset 0x06 (upper word of DWORD at 0x04)
    uint8_t revision_id;
    uint8_t prog_if;
    uint8_t subclass;
    uint8_t class_code;
    uint8_t cache_line_size; // Offset 0x0C
    uint8_t latency_timer;   // Offset 0x0D
    uint8_t header_type;     // Offset 0x0E
    uint8_t bist;            // Offset 0x0F
    uint32_t bar[6];
};

// --- NEW: Structures for PCI Class/Subclass/ProgIF Names ---
struct ProgIFEntry {
    uint8_t prog_if_code;
    const char* name;
};

struct SubclassEntry {
    uint8_t subclass_code;
    const char* name;
    const ProgIFEntry* prog_ifs;
    size_t num_prog_ifs;
};

struct ClassCodeEntry {
    uint8_t class_code_val;
    const char* name;
    const SubclassEntry* subclasses;
    size_t num_subclasses;
};

// --- Database for PCI Class Codes, Subclasses, and Programming Interfaces ---
// (This is a representative subset. A full list is very extensive.)

// Prog IFs for 0x01 (Mass Storage) -> 0x01 (IDE Controller)
static const ProgIFEntry ide_controller_prog_ifs[] = {
    {0x00, "ISA Compatibility mode-only IDE controller"},
    {0x05, "PCI native mode-only IDE controller"},
    {0x0A, "ISA Compatibility mode IDE controller, supports both channels PCI native"},
    {0x0F, "PCI native mode IDE controller, supports both channels ISA compatibility"},
    {0x80, "ISA Compatibility mode-only IDE controller, bus mastering"},
    {0x85, "PCI native mode-only IDE controller, bus mastering"},
    {0x8A, "ISA Compatibility mode IDE controller, supports both channels PCI native, bus mastering"},
    {0x8F, "PCI native mode IDE controller, supports both channels ISA compatibility, bus mastering"}
};
// Prog IFs for 0x01 (Mass Storage) -> 0x06 (SATA Controller)
static const ProgIFEntry sata_controller_prog_ifs[] = {
    {0x00, "Vendor specific interface"},
    {0x01, "AHCI 1.0"},
    {0x02, "Serial Storage Bus"}
};
// Prog IFs for 0x01 (Mass Storage) -> 0x08 (NVM Controller)
static const ProgIFEntry nvm_controller_prog_ifs[] = {
    {0x01, "NVMHCI"},
    {0x02, "NVM Express (NVMe)"}
};
// Prog IFs for 0x03 (Display) -> 0x00 (VGA-Compatible Controller)
static const ProgIFEntry vga_controller_prog_ifs[] = {
    {0x00, "VGA controller"},
    {0x01, "8514-compatible controller"}
};
// Prog IFs for 0x06 (Bridge) -> 0x04 (PCI-to-PCI Bridge)
static const ProgIFEntry pci_to_pci_bridge_prog_ifs[] = {
    {0x00, "Normal decode"},
    {0x01, "Subtractive decode"}
};
// Prog IFs for 0x0C (Serial Bus) -> 0x03 (USB Controller)
static const ProgIFEntry usb_controller_prog_ifs[] = {
    {0x00, "UHCI (Universal Host Controller Interface - USB 1.x)"},
    {0x10, "OHCI (Open Host Controller Interface - USB 1.x)"},
    {0x20, "EHCI (Enhanced Host Controller Interface - USB 2.0)"},
    {0x30, "XHCI (Extensible Host Controller Interface - USB 3.x/USB 4)"},
    {0x80, "Unspecified USB controller"},
    {0xFE, "USB Device (Not a host controller)"}
};

// Subclasses
static const SubclassEntry mass_storage_subclasses[] = {
    {0x00, "SCSI Bus Controller", nullptr, 0},
    {0x01, "IDE Controller", ide_controller_prog_ifs, sizeof(ide_controller_prog_ifs)/sizeof(ProgIFEntry)},
    {0x02, "Floppy Disk Controller", nullptr, 0},
    {0x03, "IPI Bus Controller", nullptr, 0},
    {0x04, "RAID Controller", nullptr, 0},
    {0x05, "ATA Controller", nullptr, 0}, // Specific PI for single/chained DMA exist
    {0x06, "Serial ATA (SATA) Controller", sata_controller_prog_ifs, sizeof(sata_controller_prog_ifs)/sizeof(ProgIFEntry)},
    {0x07, "Serial Attached SCSI (SAS) Controller", nullptr, 0},
    {0x08, "Non-Volatile Memory (NVM) Controller", nvm_controller_prog_ifs, sizeof(nvm_controller_prog_ifs)/sizeof(ProgIFEntry)}
};
static const SubclassEntry network_controller_subclasses[] = {
    {0x00, "Ethernet Controller", nullptr, 0},
    {0x01, "Token Ring Controller", nullptr, 0},
    {0x02, "FDDI Controller", nullptr, 0},
    {0x03, "ATM Controller", nullptr, 0},
    {0x04, "ISDN Controller", nullptr, 0},
    {0x05, "WorldFip Controller", nullptr, 0},
    {0x06, "PICMG 2.14 Multi Computing", nullptr, 0},
    {0x07, "InfiniBand Controller", nullptr, 0},
    {0x08, "Fabric Controller", nullptr, 0},
    {0x80, "Other Network Controller", nullptr, 0}
};
static const SubclassEntry display_controller_subclasses[] = {
    {0x00, "VGA-Compatible Controller", vga_controller_prog_ifs, sizeof(vga_controller_prog_ifs)/sizeof(ProgIFEntry)},
    {0x01, "XGA Controller", nullptr, 0},
    {0x02, "3D Controller (Not VGA-Compatible)", nullptr, 0},
    {0x80, "Other Display Controller", nullptr, 0}
};
static const SubclassEntry multimedia_device_subclasses[] = {
    {0x00, "Multimedia Video Controller", nullptr, 0},
    {0x01, "Multimedia Audio Controller", nullptr, 0},
    {0x02, "Computer Telephony Device", nullptr, 0},
    {0x03, "Audio Device", nullptr, 0} // (e.g. HDA)
};
static const SubclassEntry memory_controller_subclasses[] = {
    {0x00, "RAM Controller", nullptr, 0},
    {0x01, "Flash Controller", nullptr, 0},
    {0x80, "Other Memory Controller", nullptr, 0}
};
static const SubclassEntry bridge_device_subclasses[] = {
    {0x00, "Host Bridge", nullptr, 0},
    {0x01, "ISA Bridge", nullptr, 0},
    {0x02, "EISA Bridge", nullptr, 0},
    {0x03, "MCA Bridge", nullptr, 0},
    {0x04, "PCI-to-PCI Bridge", pci_to_pci_bridge_prog_ifs, sizeof(pci_to_pci_bridge_prog_ifs)/sizeof(ProgIFEntry)},
    {0x05, "PCMCIA Bridge", nullptr, 0},
    {0x06, "NuBus Bridge", nullptr, 0},
    {0x07, "CardBus Bridge", nullptr, 0},
    {0x08, "RACEway Bridge", nullptr, 0},
    {0x09, "Semi-transparent PCI-to-PCI Bridge", nullptr, 0},
    {0x0A, "InfiniBand-to-PCI Host Bridge", nullptr, 0},
    {0x80, "Other Bridge Device", nullptr, 0}
};
static const SubclassEntry simple_comm_controller_subclasses[] = {
    {0x00, "Serial Controller", nullptr, 0}, // Many ProgIFs for UART types
    {0x01, "Parallel Controller", nullptr, 0},
    {0x02, "Multiport Serial Controller", nullptr, 0},
    {0x03, "Modem", nullptr, 0},
    {0x04, "GPIB (IEEE 488.1/2) Controller", nullptr, 0},
    {0x05, "Smart Card Controller", nullptr, 0},
    {0x80, "Other Communication Controller", nullptr, 0}
};
static const SubclassEntry base_system_peripheral_subclasses[] = {
    {0x00, "PIC (Programmable Interrupt Controller)", nullptr, 0},
    {0x01, "DMA Controller", nullptr, 0},
    {0x02, "Timer", nullptr, 0},
    {0x03, "RTC (Real Time Clock) Controller", nullptr, 0},
    {0x04, "PCI Hot-Plug Controller", nullptr, 0},
    {0x05, "SD Host controller", nullptr, 0},
    {0x06, "IOMMU (I/O Memory Management Unit)", nullptr, 0},
    {0x80, "Other System Peripheral", nullptr, 0}
};
static const SubclassEntry serial_bus_controller_subclasses[] = {
    {0x00, "FireWire (IEEE 1394) Controller", nullptr, 0},
    {0x01, "ACCESS Bus Controller", nullptr, 0},
    {0x02, "SSA (Serial Storage Architecture) Controller", nullptr, 0},
    {0x03, "USB Controller", usb_controller_prog_ifs, sizeof(usb_controller_prog_ifs)/sizeof(ProgIFEntry)},
    {0x04, "Fibre Channel Controller", nullptr, 0},
    {0x05, "SMBus (System Management Bus) Controller", nullptr, 0},
    {0x06, "InfiniBand Controller", nullptr, 0},
    {0x07, "IPMI Interface", nullptr, 0},
    {0x08, "SERCOS Interface (IEC 61491)", nullptr, 0},
    {0x09, "CANbus Controller", nullptr, 0}
};

// Main Class Code Array
static const ClassCodeEntry known_class_codes[] = {
    {0x00, "Unclassified Device", nullptr, 0}, // (Includes pre-PCI 2.0 devices)
    {0x01, "Mass Storage Controller", mass_storage_subclasses, sizeof(mass_storage_subclasses)/sizeof(SubclassEntry)},
    {0x02, "Network Controller", network_controller_subclasses, sizeof(network_controller_subclasses)/sizeof(SubclassEntry)},
    {0x03, "Display Controller", display_controller_subclasses, sizeof(display_controller_subclasses)/sizeof(SubclassEntry)},
    {0x04, "Multimedia Device", multimedia_device_subclasses, sizeof(multimedia_device_subclasses)/sizeof(SubclassEntry)},
    {0x05, "Memory Controller", memory_controller_subclasses, sizeof(memory_controller_subclasses)/sizeof(SubclassEntry)},
    {0x06, "Bridge Device", bridge_device_subclasses, sizeof(bridge_device_subclasses)/sizeof(SubclassEntry)},
    {0x07, "Simple Communication Controller", simple_comm_controller_subclasses, sizeof(simple_comm_controller_subclasses)/sizeof(SubclassEntry)},
    {0x08, "Base System Peripheral", base_system_peripheral_subclasses, sizeof(base_system_peripheral_subclasses)/sizeof(SubclassEntry)},
    {0x09, "Input Device Controller", nullptr, 0}, // Keyboard, Pen, Mouse etc.
    {0x0A, "Docking Station", nullptr, 0},
    {0x0B, "Processor", nullptr, 0},
    {0x0C, "Serial Bus Controller", serial_bus_controller_subclasses, sizeof(serial_bus_controller_subclasses)/sizeof(SubclassEntry)},
    {0x0D, "Wireless Controller", nullptr, 0}, // RF, Bluetooth, WiMAX etc.
    {0x0E, "Intelligent I/O Controller", nullptr, 0},
    {0x0F, "Satellite Communication Controller", nullptr, 0},
    {0x10, "Encryption/Decryption Controller", nullptr, 0},
    {0x11, "Data Acquisition and Signal Processing Controller", nullptr, 0},
    {0xFF, "Device does not fit any defined classes", nullptr, 0}
};

// --- NEW: Function to get PCI type names ---
void get_pci_type_names(uint8_t class_code_val, uint8_t subclass_code_val, uint8_t prog_if_val,
                        const char** cc_name_ptr, const char** sc_name_ptr, const char** pi_name_ptr) {
    *cc_name_ptr = "Unknown Class";
    *sc_name_ptr = "Unknown Subclass";
    *pi_name_ptr = "Unknown Prog IF";

    for (size_t i = 0; i < sizeof(known_class_codes) / sizeof(ClassCodeEntry); ++i) {
        if (known_class_codes[i].class_code_val == class_code_val) {
            *cc_name_ptr = known_class_codes[i].name;
            if (known_class_codes[i].subclasses != nullptr) {
                for (size_t j = 0; j < known_class_codes[i].num_subclasses; ++j) {
                    if (known_class_codes[i].subclasses[j].subclass_code == subclass_code_val) {
                        *sc_name_ptr = known_class_codes[i].subclasses[j].name;
                        if (known_class_codes[i].subclasses[j].prog_ifs != nullptr) {
                            for (size_t k = 0; k < known_class_codes[i].subclasses[j].num_prog_ifs; ++k) {
                                if (known_class_codes[i].subclasses[j].prog_ifs[k].prog_if_code == prog_if_val) {
                                    *pi_name_ptr = known_class_codes[i].subclasses[j].prog_ifs[k].name;
                                    return; // Found all three
                                }
                            }
                        }
                        // Found Class and Subclass, specific ProgIF not in DB or not applicable here
                        // Set a generic if PI specific to this subclass are not enumerated, otherwise it remains "Unknown"
                        if (known_class_codes[i].subclasses[j].prog_ifs == nullptr || known_class_codes[i].subclasses[j].num_prog_ifs == 0) {
                           *pi_name_ptr = (prog_if_val == 0x00 && (*sc_name_ptr != "Unknown Subclass")) ? "Generic/Default" : "Device Specific";
                        }
                        return;
                    }
                }
            }
            // Found Class, Subclass not in DB or not applicable
            if (known_class_codes[i].subclasses == nullptr) {
                *sc_name_ptr = "N/A";
                *pi_name_ptr = "N/A";
            }
            return;
        }
    }
}


uint32_t pci_read_config_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address;
    uint32_t lbus = (uint32_t)bus;
    uint32_t lslot = (uint32_t)slot;
    uint32_t lfunc = (uint32_t)func;
    address = (uint32_t)((lbus << 16) | (lslot << 11) |
                         (lfunc << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
    direct_outl(PCI_CONFIG_ADDRESS, address);
    return direct_inl(PCI_CONFIG_DATA);
}

// pci_write_config_dword can remain the same if needed by other parts of the system

uint16_t get_pci_status(uint8_t bus, uint8_t slot, uint8_t func) {
    // Status is upper 16 bits of the DWORD at offset 0x04
    return (uint16_t)((pci_read_config_dword(bus, slot, func, PCI_COMMAND_REGISTER) >> 16) & 0xFFFF);
}

uint16_t get_pci_command(uint8_t bus, uint8_t slot, uint8_t func) {
    // Command is lower 16 bits of the DWORD at offset 0x04
    return (uint16_t)(pci_read_config_dword(bus, slot, func, PCI_COMMAND_REGISTER) & 0xFFFF);
}

// Read BIST, Header Type, Latency Timer, Cache Line Size from offsets 0x0C-0x0F
void read_pci_header_details(uint8_t bus, uint8_t slot, uint8_t func, pci_device* dev) {
    uint32_t reg_0C = pci_read_config_dword(bus, slot, func, 0x0C);
    dev->cache_line_size = (uint8_t)(reg_0C & 0xFF);
    dev->latency_timer   = (uint8_t)((reg_0C >> 8) & 0xFF);
    dev->header_type     = (uint8_t)((reg_0C >> 16) & 0xFF);
    dev->bist            = (uint8_t)((reg_0C >> 24) & 0xFF);
}

void read_pci_bars(uint8_t bus, uint8_t slot, uint8_t func, struct pci_device* dev) {
    // Use the already read dev->header_type
    int bars_to_read = 6; // Standard devices (Type 00h header) have 6 BARs
    if ((dev->header_type & 0x7F) == 0x01) { // PCI-to-PCI bridge (Type 01h header) has 2 BARs
        bars_to_read = 2;
    } else if ((dev->header_type & 0x7F) == 0x02) { // PCI-to-CardBus bridge (Type 02h header) has 1 BAR
        bars_to_read = 1;
    }

    for (int i = 0; i < 6; i++) { // Initialize all BARs to 0 first
        dev->bar[i] = 0;
    }

    for (int i = 0; i < bars_to_read; i++) {
        uint8_t bar_offset = 0x10 + (i * 4);
        dev->bar[i] = pci_read_config_dword(bus, slot, func, bar_offset);
    }
}

int check_device_function(uint8_t bus, uint8_t device, uint8_t func) {
    uint32_t vendor_device = pci_read_config_dword(bus, device, func, PCI_VENDOR_ID);
    uint16_t vendor = (uint16_t)(vendor_device & 0xFFFF);
    // A device exists if Vendor ID is not 0xFFFF (or 0x0000, though less common for non-existence)
    return (vendor != 0xFFFF && vendor != 0x0000);
}

// MODIFIED: scan_pci function
void scan_pci() {
    struct pci_device dev; // Re-use this struct for each function
    
    cout << "Scanning PCI bus for device types...\n";

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device_slot = 0; device_slot < 32; device_slot++) {
            // Check function 0 first to see if device exists at this slot
            if (!check_device_function(bus, device_slot, 0)) {
                continue;
            }
            
            // Read header type from function 0 to determine if multi-function
            read_pci_header_details(bus, device_slot, 0, &dev); // populates dev.header_type
            uint8_t max_functions = (dev.header_type & 0x80) ? 8 : 1; // Check multifunction bit

            for (uint8_t func = 0; func < max_functions; func++) {
                if (!check_device_function(bus, device_slot, func)) {
                    continue; // Skip this function if it's not present
                }

                // Populate PCI device structure for the current function
                uint32_t vendor_device_reg = pci_read_config_dword(bus, device_slot, func, PCI_VENDOR_ID);
                dev.vendor_id = (uint16_t)(vendor_device_reg & 0xFFFF);
                dev.device_id = (uint16_t)(vendor_device_reg >> 16);

                uint32_t class_rev_reg = pci_read_config_dword(bus, device_slot, func, PCI_CLASS_REVISION);
                dev.revision_id = (uint8_t)(class_rev_reg & 0xFF);
                dev.prog_if     = (uint8_t)((class_rev_reg >> 8) & 0xFF);
                dev.subclass    = (uint8_t)((class_rev_reg >> 16) & 0xFF);
                dev.class_code  = (uint8_t)((class_rev_reg >> 24) & 0xFF);
                
                dev.command = get_pci_command(bus, device_slot, func);
                dev.status = get_pci_status(bus, device_slot, func);
                read_pci_header_details(bus, device_slot, func, &dev); // Read Header Type, BIST etc.

                // Get device type names
                const char* cc_name_str;
                const char* sc_name_str;
                const char* pi_name_str;
                get_pci_type_names(dev.class_code, dev.subclass, dev.prog_if, &cc_name_str, &sc_name_str, &pi_name_str);
                
                cout << "PCI Device: Bus " << bus << ", Dev " << device_slot << ", Func " << (int)func << "\n";
                cout << "  Vendor: " << std::hex << dev.vendor_id << ", Device: " << dev.device_id << std::dec << "\n";
                cout << "  Class Code:  " << std::hex << (int)dev.class_code << " (" << cc_name_str << ")\n";
                cout << "  Subclass:    " << std::hex << (int)dev.subclass << " (" << sc_name_str << ")\n";
                cout << "  Prog IF:     " << std::hex << (int)dev.prog_if << " (" << pi_name_str << ")\n";
                cout << "  Revision ID: " << std::hex << (int)dev.revision_id << std::dec << "\n";
                cout << "  Header Type: " << std::hex << (int)dev.header_type << std::dec 
                     << (((dev.header_type & 0x7F) == 0x00) ? " (Generic Device)" : 
                         (((dev.header_type & 0x7F) == 0x01) ? " (PCI-to-PCI Bridge)" : 
                         (((dev.header_type & 0x7F) == 0x02) ? " (CardBus Bridge)" : " (Unknown Type)")))
                     << ((dev.header_type & 0x80) ? ", Multi-function" : "") << "\n";
                cout << "  Command: " << std::hex << dev.command << ", Status: " << dev.status << std::dec << "\n";


                // Read and display BARs
                read_pci_bars(bus, device_slot, func, &dev);
                int bars_to_display = 6;
                 if ((dev.header_type & 0x7F) == 0x01) bars_to_display = 2;
                 else if ((dev.header_type & 0x7F) == 0x02) bars_to_display = 1;

                for (int i = 0; i < bars_to_display; i++) {
                    cout << "  BAR" << i << ": " << std::hex << dev.bar[i] << std::dec;
                    if (dev.bar[i] == 0 && !(i > 0 && ((dev.bar[i-1] & 0x07) == 0x04) && (dev.header_type & 0x7F) == 0x00 )) { 
                        // More careful check for 0 BAR not being an upper half of a 64-bit mapping
                        cout << " (Unused or Upper part)\n";
                        continue; 
                    }
                    cout << "\n";

                    if (dev.bar[i] & 0x1) { // I/O Space BAR
                        cout << "    Type: I/O Space\n";
                        cout << "    Base Address: " << std::hex << (dev.bar[i] & 0xFFFFFFFC) << std::dec << "\n";
                    } else { // Memory Space BAR
                        cout << "    Type: Memory Space\n";
                        uint8_t mem_space_type = (dev.bar[i] >> 1) & 0x3;
                        bool prefetchable = (dev.bar[i] >> 3) & 0x1;

                        cout << "    Prefetchable: " << (prefetchable ? "Yes" : "No") << "\n";

                        switch (mem_space_type) {
                            case 0x00: // 32-bit
                                cout << "    Mapping: 32-bit address space\n";
                                cout << "    Base Address: " << std::hex << (dev.bar[i] & 0xFFFFFFF0) << std::dec << "\n";
                                break;
                            case 0x01: // <1MB or 32-bit
                                cout << "    Mapping: Below 1MB (legacy) or 32-bit address space\n";
                                cout << "    Base Address: " << std::hex << (dev.bar[i] & 0xFFFFFFF0) << std::dec << "\n";
                                break;
                            case 0x02: // 64-bit
                                cout << "    Mapping: 64-bit address space\n";
                                if (i + 1 < bars_to_display) {
                                    uint32_t base_address_64 = ((uint64_t)dev.bar[i+1] << 32) | (dev.bar[i] & 0xFFFFFFF0);
                                    cout << "    Base Address: " << std::hex << base_address_64 << std::dec << "\n";
                                    cout << "    (BAR" << (i+1) << " [0x" << std::hex << dev.bar[i+1] << std::dec << "] is upper 32 bits)\n";
                                    i++; 
                                } else {
                                    cout << "    Base Address (lower 32-bits, upper part missing/invalid): " << std::hex << (dev.bar[i] & 0xFFFFFFF0) << std::dec << "\n";
                                }
                                break;
                            default: // Reserved
                                cout << "    Mapping: Reserved type\n";
                                break;
                        }
                    }
                }
                cout << "\n"; 
                
                if (func == max_functions - 1) { // Prompt after last function of a device slot
                    cout << "Press enter to continue to next device slot...\n";
                    char input_buf[2]; 
                    cin >> input_buf; 
                }
            } // End function loop
        } // End device_slot loop
    } // End bus loop
    cout << "PCI scan complete.\n";
}
