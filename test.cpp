#include "terminal_io.h"
#include <cstdlib> // For atoi

void print_prog() {
    TerminalOutput& mycout = get_cout();
    TerminalInput& mycin = get_cin();

    mycout << "Enter a number: ";
    char numStr[80]; // Assuming a maximum of 80 characters for the number
    mycin >> numStr;  // Read the input as a string
    mycout << "You entered: " << numStr << "\n";
}