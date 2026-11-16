#pragma once
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
inline void* heap_caps_malloc(size_t n, unsigned) { return std::malloc(n); }
inline size_t heap_caps_get_free_size(unsigned caps) { return caps & MALLOC_CAP_SPIRAM ? 30000u * 1024 : 300u * 1024; }
inline size_t heap_caps_get_minimum_free_size(unsigned) { return 250u * 1024; }
