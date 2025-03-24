#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "kernel.h"
#include "utility.h"
#include "hardware_specs.h"  // Include the hardware specs header

/* Kernel Main Entry Point */
void kernel_main(void) {
    // Initialize terminal
    terminal_initialize();
    
    // Set terminal color to green on black
    terminal_setcolor(make_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK));
    
    // Welcome message
    terminal_writestring("Kernel Booting...\n");
    
    // Initialize and detect hardware
    hardware_specs_initialize();
    
    // Kernel idle loop
    terminal_writestring("\nKernel Ready. Entering Idle Loop.\n");
    while (1) {
        // Halt or perform background tasks
        __asm__ volatile ("hlt");
    }
}