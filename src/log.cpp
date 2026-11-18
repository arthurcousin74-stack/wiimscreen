#include "log.h"

#include <cstdarg>
#include <cstring>
#include <deque>
#include <string>

static constexpr size_t kMaxLines = 40;

static SemaphoreHandle_t lock() {
    static SemaphoreHandle_t m = xSemaphoreCreateMutex();
    return m;
}

static std::deque<String>& lines() {
    static std::deque<String> l;
    return l;
}

// Ligne horodatée : `serialText` sur le port série, `screenText` dans le journal en RAM (écran)
static void store(const char* screenText, const char* serialText) {
    const uint32_t ms = millis();
    const unsigned long s = ms / 1000, tenth = ms % 1000 / 100;
    char line[288];
    snprintf(line, sizeof line, "%5lu.%lu s  %s", s, tenth, serialText);
    Serial.println(line);
    if (serialText != screenText) snprintf(line, sizeof line, "%5lu.%lu s  %s", s, tenth, screenText);
    xSemaphoreTake(lock(), portMAX_DELAY);
    lines().push_back(line);
    while (lines().size() > kMaxLines) lines().pop_front();
    xSemaphoreGive(lock());
}

void logEvent(const char* fmt, ...) {
    char msg[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    store(msg, msg);
}

void logTagged(const char* tag, const char* fmt, ...) {
    char all[224];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(all, sizeof all, fmt, ap);
    va_end(ap);
    // Paires clé=valeur avant le premier "| ", phrase après
    const char* bar = strstr(all, "| ");
    const char* sentence = bar ? bar + 2 : all;
    std::string kv = bar ? std::string(all, bar - all) : std::string();
    while (!kv.empty() && kv.back() == ' ') kv.pop_back();
    std::string msg(sentence);
    if (msg.size() > 159) msg.resize(159);  // même longueur que logEvent sur l'écran
    char serial[256];
    snprintf(serial, sizeof serial, "[%s]%s%s | %s", tag, kv.empty() ? "" : " ", kv.c_str(), msg.c_str());
    store(msg.c_str(), serial);
}

std::vector<String> logLines() {
    xSemaphoreTake(lock(), portMAX_DELAY);
    std::vector<String> out(lines().begin(), lines().end());
    xSemaphoreGive(lock());
    return out;
}
