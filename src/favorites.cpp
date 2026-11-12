#include "favorites.h"

#include <Arduino.h>
#include <LittleFS.h>

#include <algorithm>
#include <cctype>

namespace fav {

namespace {

SemaphoreHandle_t lock() {
    static SemaphoreHandle_t m = xSemaphoreCreateMutex();
    return m;
}

struct Guard {
    Guard() { xSemaphoreTake(lock(), portMAX_DELAY); }
    ~Guard() { xSemaphoreGive(lock()); }
};

bool g_ok = false;
std::vector<wr::Favorite> g_list;
uint32_t g_rev = 0;
constexpr const char* kIndex = "/fav/index.txt";

bool readFile(const char* path, std::string& out) {
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    out.clear();
    out.reserve(f.size());
    char buf[512];
    while (f.available()) {
        int n = f.read(reinterpret_cast<uint8_t*>(buf), sizeof buf);
        if (n <= 0) break;
        out.append(buf, n);
    }
    f.close();
    return true;
}

bool writeFile(const char* path, const std::string& data) {
    File f = LittleFS.open(path, "w");
    if (!f) return false;
    size_t n = f.write(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    f.close();
    return n == data.size();
}

bool saveIndex() { return writeFile(kIndex, wr::favoritesToText(g_list)); }

std::string path(const std::string& id) { return "/fav/" + id + ".xml"; }

}  // namespace

bool begin() {
    Guard g;
    if (g_ok) return true;
    if (!LittleFS.begin(true)) return false;  // formate la partition au premier démarrage
    if (!LittleFS.exists("/fav")) LittleFS.mkdir("/fav");
    std::string text;
    if (readFile(kIndex, text)) g_list = wr::favoritesFromText(text);
    g_ok = true;
    ++g_rev;
    return true;
}

std::vector<wr::Favorite> list() {
    Guard g;
    return g_list;
}

uint32_t revision() {
    Guard g;
    return g_rev;
}

bool add(wr::Favorite& f, const std::string& queueContext, std::string& error) {
    Guard g;
    if (!g_ok) {
        error = "m\xC3\xA9moire indisponible";
        return false;
    }
    auto same = std::find_if(g_list.begin(), g_list.end(), [&](const wr::Favorite& x) { return x.key == f.key; });
    if (same == g_list.end() && g_list.size() >= kMax) {
        error = "24 favoris au plus";
        return false;
    }
    char idBuf[12];
    snprintf(idBuf, sizeof idBuf, "%08lx", (unsigned long)esp_random());
    f.id = same != g_list.end() ? same->id : idBuf;
    if (!writeFile(path(f.id).c_str(), queueContext)) {
        error = "\xC3\xA9" "criture impossible (m\xC3\xA9moire pleine ?)";
        return false;
    }
    if (same != g_list.end()) *same = f;
    else g_list.push_back(f);
    if (!saveIndex()) {
        error = "\xC3\xA9" "criture de l'index impossible";
        return false;
    }
    ++g_rev;
    return true;
}

bool remove(const std::string& id) {
    Guard g;
    auto it = std::find_if(g_list.begin(), g_list.end(), [&](const wr::Favorite& x) { return x.id == id; });
    if (it == g_list.end()) return false;
    g_list.erase(it);
    LittleFS.remove(path(id).c_str());
    saveIndex();
    ++g_rev;
    return true;
}

bool saveQobuzTemplate(const std::string& queueContext) {
    wr::QueueInfo q = wr::parseQueueContext(queueContext);
    if (q.sourceName != "Qobuz") return false;
    const std::string small = wr::queueFromTemplate(queueContext, q.listName, q.searchUrl);  // sans les pistes
    Guard g;
    return g_ok && !small.empty() && writeFile("/fav/qobuz_template.xml", small);
}

bool saveSourceModel(const std::string& source, const std::string& queueContext) {
    std::string safe;
    for (char c : source) safe += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    wr::QueueInfo q = wr::parseQueueContext(queueContext);
    const std::string small = wr::queueFromTemplate(queueContext, q.listName, q.searchUrl);
    Guard g;
    return g_ok && !safe.empty() && !small.empty() && writeFile(("/fav/model_" + safe.substr(0, 24) + ".xml").c_str(), small);
}

bool qobuzTemplate(std::string& out) {
    Guard g;
    return g_ok && readFile("/fav/qobuz_template.xml", out) && !out.empty();
}

bool context(const std::string& id, std::string& out) {
    Guard g;
    return g_ok && readFile(path(id).c_str(), out);
}

}  // namespace fav
