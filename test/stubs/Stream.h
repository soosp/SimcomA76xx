#pragma once
/**
 * @file Stream.h (host test stub)
 * @brief Minimal Arduino Stream: the calls SimcomA76xx makes.
 */
#include <cstddef>
#include <cstdint>
#include <cstring>

class Stream {
public:
    virtual ~Stream() {}
    virtual int    available() = 0;
    virtual int    read() = 0;
    virtual size_t write(uint8_t c) = 0;
    virtual size_t write(const uint8_t* b, size_t n) {   // as in Arduino's Print
        size_t k = 0;
        while (n--) k += write(*b++);
        return k;
    }
    size_t print(const char* s) { size_t n = 0; while (*s) n += write((uint8_t)*s++); return n; }
    size_t print(char c)        { return write((uint8_t)c); }
};
