// Computer layer: clock (millis, micros, delay), random numbers, and FreeRTOS on std::thread (tasks, mutexes,
// task notifications). See pc_internal.h for the other pc_platform*.cpp files.
#include <random>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "Arduino.h"

// ------------------------------------------------------------------ time

static const auto kStart = std::chrono::steady_clock::now();

uint32_t millis() {
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - kStart)
        .count();
}
uint32_t micros() {
    return (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - kStart)
        .count();
}
void delay(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
long random(long max) { return max > 0 ? std::rand() % max : 0; }
long random(long min, long max) { return max > min ? min + std::rand() % (max - min) : min; }
// Seeded once per run (an unseeded std::rand gave the same favorite id at every start: two favorites shared a file)
uint32_t esp_random() {
    static std::mt19937 gen{std::random_device{}()};
    return gen();
}

// ------------------------------------------------------------------ FreeRTOS

// Not recursive, like a FreeRTOS mutex: taking it twice in one task blocks here too (instead of hiding the bug)
SemaphoreHandle_t xSemaphoreCreateMutex() { return new std::mutex(); }
BaseType_t xSemaphoreTake(SemaphoreHandle_t m, TickType_t) {
    static_cast<std::mutex*>(m)->lock();
    return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t m) {
    static_cast<std::mutex*>(m)->unlock();
    return pdTRUE;
}

namespace {
struct Notify {
    std::mutex m;
    std::condition_variable cv;
    uint32_t count = 0;
};
thread_local Notify* t_self = nullptr;
}  // namespace

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char*, uint32_t, void* arg, UBaseType_t, TaskHandle_t* out,
                                   BaseType_t) {
    auto* n = new Notify();  // never freed: tasks live as long as the program (as on the device)
    std::thread([fn, arg, n] {
        t_self = n;
        fn(arg);
    }).detach();
    if (out) *out = n;
    return pdPASS;
}
void vTaskDelay(TickType_t ticks) { delay(ticks); }
void xTaskNotifyGive(TaskHandle_t task) {
    if (!task) return;
    auto* n = static_cast<Notify*>(task);
    std::lock_guard<std::mutex> g(n->m);
    ++n->count;
    n->cv.notify_one();
}
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t wait) {
    if (!t_self) {
        delay(wait);
        return 0;
    }
    std::unique_lock<std::mutex> l(t_self->m);
    t_self->cv.wait_for(l, std::chrono::milliseconds(wait), [] { return t_self->count > 0; });
    const uint32_t v = t_self->count;
    if (clear) t_self->count = 0;
    else if (t_self->count) --t_self->count;
    return v;
}
