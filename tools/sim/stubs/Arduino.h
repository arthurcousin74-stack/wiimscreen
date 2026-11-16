// Simulator: the parts of Arduino-ESP32 that wiimscreen uses, on a PC (POSIX threads, std::string).
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "freertos_sim.h"

#define PROGMEM
#define F(s) (s)
#define FPSTR(s) (s)
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))

uint32_t millis();
uint32_t micros();
void delay(uint32_t ms);
inline void yield() {}
long random(long max);
long random(long min, long max);

class String {
public:
    String() = default;
    String(const char* s) : s_(s ? s : "") {}
    String(const std::string& s) : s_(s) {}
    String(char c) : s_(1, c) {}
    String(int v) : s_(std::to_string(v)) {}
    String(unsigned v) : s_(std::to_string(v)) {}
    String(long v) : s_(std::to_string(v)) {}
    String(unsigned long v) : s_(std::to_string(v)) {}
    String(long long v) : s_(std::to_string(v)) {}
    String(unsigned long long v) : s_(std::to_string(v)) {}
    String(double v, int decimals = 2) {
        char b[64];
        snprintf(b, sizeof b, "%.*f", decimals, v);
        s_ = b;
    }
    const char* c_str() const { return s_.c_str(); }
    unsigned length() const { return (unsigned)s_.size(); }
    bool isEmpty() const { return s_.empty(); }
    void reserve(unsigned n) { s_.reserve(n); }
    char operator[](unsigned i) const { return i < s_.size() ? s_[i] : 0; }
    char& operator[](unsigned i) { return s_[i]; }
    char charAt(unsigned i) const { return (*this)[i]; }
    String& operator+=(const String& o) { s_ += o.s_; return *this; }
    String& operator+=(const char* o) { s_ += o ? o : ""; return *this; }
    String& operator+=(char c) { s_ += c; return *this; }
    String& operator+=(int v) { s_ += std::to_string(v); return *this; }
    String& operator+=(unsigned v) { s_ += std::to_string(v); return *this; }
    String& operator+=(long v) { s_ += std::to_string(v); return *this; }
    String& operator+=(unsigned long v) { s_ += std::to_string(v); return *this; }
    bool concat(const String& o) { s_ += o.s_; return true; }
    bool operator==(const String& o) const { return s_ == o.s_; }
    bool operator!=(const String& o) const { return s_ != o.s_; }
    bool operator==(const char* o) const { return s_ == (o ? o : ""); }
    bool operator!=(const char* o) const { return !(*this == o); }
    bool operator<(const String& o) const { return s_ < o.s_; }
    bool equals(const String& o) const { return s_ == o.s_; }
    bool equalsIgnoreCase(const String& o) const {
        if (s_.size() != o.s_.size()) return false;
        for (size_t i = 0; i < s_.size(); ++i)
            if (std::tolower((unsigned char)s_[i]) != std::tolower((unsigned char)o.s_[i])) return false;
        return true;
    }
    long toInt() const { return std::strtol(s_.c_str(), nullptr, 10); }
    float toFloat() const { return std::strtof(s_.c_str(), nullptr); }
    bool startsWith(const String& p) const { return s_.compare(0, p.s_.size(), p.s_) == 0; }
    bool endsWith(const String& p) const {
        return s_.size() >= p.s_.size() && s_.compare(s_.size() - p.s_.size(), p.s_.size(), p.s_) == 0;
    }
    int indexOf(char c, unsigned from = 0) const { auto p = s_.find(c, from); return p == std::string::npos ? -1 : (int)p; }
    int indexOf(const String& t, unsigned from = 0) const {
        auto p = s_.find(t.s_, from);
        return p == std::string::npos ? -1 : (int)p;
    }
    int lastIndexOf(char c) const { auto p = s_.rfind(c); return p == std::string::npos ? -1 : (int)p; }
    String substring(unsigned a) const { return a >= s_.size() ? String() : String(s_.substr(a)); }
    String substring(unsigned a, unsigned b) const {
        if (b > s_.size()) b = (unsigned)s_.size();
        return a >= b ? String() : String(s_.substr(a, b - a));
    }
    void replace(const String& from, const String& to) {
        if (from.s_.empty()) return;
        for (size_t p = 0; (p = s_.find(from.s_, p)) != std::string::npos; p += to.s_.size()) s_.replace(p, from.s_.size(), to.s_);
    }
    void remove(unsigned index, unsigned count = (unsigned)-1) { if (index < s_.size()) s_.erase(index, count); }
    void trim() {
        size_t a = 0, b = s_.size();
        while (a < b && std::isspace((unsigned char)s_[a])) ++a;
        while (b > a && std::isspace((unsigned char)s_[b - 1])) --b;
        s_ = s_.substr(a, b - a);
    }
    void toLowerCase() { for (auto& c : s_) c = (char)std::tolower((unsigned char)c); }
    void toUpperCase() { for (auto& c : s_) c = (char)std::toupper((unsigned char)c); }
    friend String operator+(const String& a, const String& b) { return String(a.s_ + b.s_); }
    friend String operator+(const char* a, const String& b) { return String(std::string(a) + b.s_); }
    friend String operator+(const String& a, const char* b) { return String(a.s_ + b); }
    friend String operator+(const String& a, char b) { return String(a.s_ + b); }
    friend String operator+(const String& a, int b) { return String(a.s_ + std::to_string(b)); }
    friend String operator+(const String& a, unsigned b) { return String(a.s_ + std::to_string(b)); }
    friend String operator+(const String& a, long b) { return String(a.s_ + std::to_string(b)); }
    friend String operator+(const String& a, unsigned long b) { return String(a.s_ + std::to_string(b)); }

private:
    std::string s_;
};

struct SerialSim {
    void begin(unsigned long) {}
    void println(const char* s);
    void println(const String& s) { println(s.c_str()); }
    void print(const char* s);
    int printf(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
    void flush() {}
};
extern SerialSim Serial;

struct EspSim {
    [[noreturn]] void restart();
    uint64_t getEfuseMac() const { return 0x3F2AULL; }
    uint32_t getFreeHeap() const { return 300000; }
};
extern EspSim ESP;

uint32_t esp_random();
