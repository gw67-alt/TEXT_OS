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

int atoi(const char* strg)
{

    // Initialize res to 0
    int res = 0;
    int i = 0;

    // Iterate through the string strg and compute res
    while (strg[i] != '\0') {
        res = res * 10 + (strg[i] - '0');
        i++;
    }
    return res;
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


    TerminalInput& TerminalInput::operator>>(int num) {
        char buffer[MAX_COMMAND_LENGTH]; // Use a buffer to read the string
        *this >> buffer; // Use the existing char* overload to read into the buffer
        num = atoi(buffer); // Convert the string to an integer
        return *this;
    }
    TerminalOutput& TerminalOutput::operator<<(int num) {
        char buffer[32]; // A buffer large enough to hold most integer values
        *this << buffer; // Use the existing operator<<(const char*) to output the string
        return *this;
    }
    
void print_prog2() {


    cout << "Enter 1: ";
    char input[80]; // Assuming a maximum of 80 characters for the number
    cin >> input;  // Read the input as a string
    int a = atoi(input);
    if (a == 1) {
        cout << "1 = " << input << "\n";
    }
}
