/**
 * AMD64 Hardware Configuration Reader
 * 
 * This is a bare metal implementation for reading AMD64 hardware configuration
 * using iostream_wrapper for output.
 */

 #include "kernel.h"
 #include "terminal_hooks.h"
 #include "terminal_io.h"
 #include "iostream_wrapper.h"
 #include "interrupts.h"
 #include "hardware_specs.h"
 #include "stdlib_hooks.h"
 
 
 /* Command processing function */
 void command_prompt() {
     char input[MAX_COMMAND_LENGTH + 1]; // Add 1 for null terminator
     
     /* Display initial prompt */
     while (true) {
         cout << "> ";
 
         // Safely read input and null-terminate
         cin >> input;
         input[MAX_COMMAND_LENGTH] = '\0'; // Ensure null termination
 
         StringRef cmd(input); // Create a StringRef from the input
 
         if (cmd == "help") {
             cmd_help();
         } else if (cmd == "clear") {
             clear_screen();
         } else if (cmd == "cpu") {
             cmd_cpu();
         } else if (cmd == "memory") {
             cmd_memory();
         } else if (cmd == "cache") {
             cmd_cache();
         } else if (cmd == "topology") {
             cmd_topology();
         } else if (cmd == "features") {
             cmd_features();
         } else if (cmd == "pstates") {
             cmd_pstates();
         } else if (cmd == "full") {
             cmd_full();
         } else if (cmd == "program1") {
             print_prog();
         } else if (cmd == "program2") {
             print_prog2();
            } 
         }
     }

  /* Command implementations */
  void cmd_help() {
    cout << "Available commands:\n";
    cout << "  help         - Show this help message\n";
    cout << "  clear        - Clear the screen\n";
    cout << "  cpu          - Display CPU information\n";
    cout << "  memory       - Display memory configuration\n";
    cout << "  cache        - Display cache information\n";
    cout << "  topology     - Display CPU topology\n";
    cout << "  features     - Display CPU features\n";
    cout << "  pstates      - Display P-States information\n";
    cout << "  full         - Display all hardware information\n";
    cout << "  program1     - Run test program 1\n";
    cout << "  program2     - Run test program 2\n";
}

 /* Main kernel entry point */
 extern "C" void kernel_main() {
    /* Initialize terminal interface */
    terminal_initialize();
 
    /* Initialize terminal I/O */
    init_terminal_io();
 
    /* Initialize keyboard and timer interrupts */
    init_keyboard();
 
    cout << "Hello, kernel World!" << '\n';
    cout << "Initialization complete. Start typing commands...\n";
 
    command_prompt();
 }