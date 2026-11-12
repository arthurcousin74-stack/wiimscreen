#include "log.h"

#include <cstdarg>
#include <deque>

static constexpr size_t kMaxLines = 40;

static SemaphoreHandle_t lock() {
    static SemaphoreHandle_t m = xSemaphoreCreateMutex();
    return m;
}

static std::deque<String>& lines() {
    static std::deque<String> l;
    return l;
}

void logEvent(const char* fmt, ...) {
    char msg[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    const uint32_t ms = millis();
    char line[192];
    snprintf(line, sizeof line, "%5lu.%lu s  %s", (unsigned long)(ms / 1000), (unsigned long)(ms % 1000 / 100), msg);
    Serial.println(line);
    xSemaphoreTake(lock(), portMAX_DELAY);
    lines().push_back(line);
    while (lines().size() > kMaxLines) lines().pop_front();
    xSemaphoreGive(lock());
}

std::vector<String> logLines() {
    xSemaphoreTake(lock(), portMAX_DELAY);
    std::vector<String> out(lines().begin(), lines().end());
    xSemaphoreGive(lock());
    return out;
}
