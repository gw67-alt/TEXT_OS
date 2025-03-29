#define get_cout get_cout2
#define get_cin get_cin2
#include "terminal_io.h"

void print_prog2() {


    cout << "Enter username: ";
    char Str[80]; // Assuming a maximum of 80 characters for the number
    cin >> Str;  // Read the input as a string

    if (Str == "george") {
        cout << "Welcome " << Str << "!\n";
    }
}
