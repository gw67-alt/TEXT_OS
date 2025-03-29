#define get_cout get_cout2
#define get_cin get_cin2
#define string_compare string_compare2
#include "terminal_io.h"



bool string_compare(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *s1 == *s2;
}
class StringRef {
    private:
        const char* data;

    public:
        // Implicit constructor to allow automatic conversion
        StringRef(const char* str) : data(str) {}

        // Get the underlying string
        const char* c_str() const { return data; }

        // Compare with another string using string_compare
        bool operator==(const StringRef& other) const {
            return string_compare(data, other.data);
        }
    };


    
void print_prog2() {


    cout << "Enter username: ";
    char input[80]; // Assuming a maximum of 80 characters for the number
    cin >> input;  // Read the input as a string

    StringRef cmd(input); // Create a StringRef from the input
    if (cmd == "george") {
        cout << "Welcome " << input << "!\n";
    }
}
