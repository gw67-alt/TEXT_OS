#include "terminal_io.h"
#include <cstdlib> // For atoi

void print_prog() {
    TerminalOutput& cout = get_cout();
    TerminalInput& cin = get_cin();

    cout << "Enter something to test cin and cout: ";
    char Str[80]; // Assuming a maximum of 80 characters for the number
    cin >> Str;  // Read the input as a string
    cout << "You entered: " << Str << "\n";
}
