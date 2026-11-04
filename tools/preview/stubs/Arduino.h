// Aperçu PC : le strict nécessaire d'Arduino pour compiler src/ui.cpp hors ESP32.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>

class String {
public:
    String() = default;
    String(const char* s) : s_(s ? s : "") {}
    String(const std::string& s) : s_(s) {}
    String(int v) : s_(std::to_string(v)) {}
    const char* c_str() const { return s_.c_str(); }
    unsigned length() const { return (unsigned)s_.size(); }
    String& operator+=(const String& o) { s_ += o.s_; return *this; }
    bool operator==(const String& o) const { return s_ == o.s_; }
    bool operator!=(const String& o) const { return s_ != o.s_; }
    friend String operator+(const String& a, const String& b) { return String(a.s_ + b.s_); }
    friend String operator+(const char* a, const String& b) { return String(std::string(a) + b.s_); }
    friend String operator+(const String& a, const char* b) { return String(a.s_ + b); }

private:
    std::string s_;
};

typedef void* SemaphoreHandle_t;
typedef void* TaskHandle_t;
