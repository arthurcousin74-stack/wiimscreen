// PC: LittleFS on a directory of the computer (<state directory>/fs).
#pragma once
#include <cstdio>

#include "Arduino.h"

class File {
public:
    File() = default;
    explicit File(FILE* f) : f_(f) {}
    File(File&& o) noexcept : f_(o.f_) { o.f_ = nullptr; }
    File& operator=(File&& o) noexcept {
        close();
        f_ = o.f_;
        o.f_ = nullptr;
        return *this;
    }
    ~File() { close(); }
    explicit operator bool() const { return f_ != nullptr; }
    size_t size() const;
    int available() const;
    int read();
    size_t read(uint8_t* b, size_t n) { return f_ ? fread(b, 1, n, f_) : 0; }
    size_t write(const uint8_t* b, size_t n) { return f_ ? fwrite(b, 1, n, f_) : 0; }
    size_t write(uint8_t c) { return write(&c, 1); }
    size_t print(const char* s) { return write(reinterpret_cast<const uint8_t*>(s), std::strlen(s)); }
    void close() {
        if (f_) fclose(f_);
        f_ = nullptr;
    }

private:
    FILE* f_ = nullptr;
};

struct LittleFSSim {
    bool begin(bool formatOnFail = false);
    bool exists(const char* path);
    bool mkdir(const char* path);
    bool remove(const char* path);
    bool format();
    File open(const char* path, const char* mode = "r");
};
extern LittleFSSim LittleFS;
