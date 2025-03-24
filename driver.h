#ifndef DRIVER_H
#define DRIVER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Memory allocation functions
void* kmalloc(size_t size);
void* kmalloc_aligned(size_t size, size_t align);
void kfree(void* ptr);
uint32_t virt_to_phys(void* virt_addr);

// I/O port functions
uint8_t inb(uint16_t port);
void outb(uint16_t port, uint8_t value);
uint16_t inw(uint16_t port);
void outw(uint16_t port, uint16_t value);
uint32_t inl(uint16_t port);
void outl(uint16_t port, uint32_t value);

// PCI enumeration functions
uint32_t find_ahci_controller(void);
void enumerate_pci_devices(void);

// AHCI driver functions
void init_ahci_drives(void);
void cmd_list_ahci_drives(void);

#endif /* DRIVER_H */