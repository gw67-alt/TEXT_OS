
#include <cstddef>
extern "C" void* memcpy(void* dest, const void* src, size_t count) {
    unsigned char* d = static_cast<unsigned char*>(dest);
    const unsigned char* s = static_cast<const unsigned char*>(src);
    for (size_t i = 0; i < count; i++) {
        d[i] = s[i];
    }
    return dest;
}