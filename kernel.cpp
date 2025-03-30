#include "kernel.h"
#include "terminal_hooks.h"
#include "terminal_io.h"
#include "iostream_wrapper.h"
#include "interrupts.h"

/* Command implementations */
void cmd_help() {
    cout << "Available commands:\n";
    cout << "  help  - Show this help message\n";
    cout << "  clear - Clear the screen\n";
    cout << "  hello - Display a greeting\n";
    cout << "  program1 - run a print program\n";
    cout << "  program2 - run a print program\n";
}

void cmd_hello() {
    cout << "Hello, user!\n";
}

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
        }
        if (cmd == "clear") {
            clear_screen();
        }
        if (cmd == "hello") {
            cmd_hello();
        }
        if (cmd == "program1") {
            print_prog();
        }
        if (cmd == "program2") {
            print_prog2();
        }
    }
}

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
