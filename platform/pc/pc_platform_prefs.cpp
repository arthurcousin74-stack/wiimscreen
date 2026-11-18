// Computer layer: Preferences on a text file (settings.txt in the state folder, the simulated NVS) and the
// settings of a first start (pc::defaultSettings). See pc_internal.h for the other pc_platform*.cpp files.
#include <fstream>
#include <map>
#include <mutex>

#include "Preferences.h"
#include "pc.h"
#include "pc_internal.h"

using namespace pc_internal;

// ------------------------------------------------------------------ settings (Preferences)

namespace pc_internal {
std::recursive_mutex& prefsLock() {  // the WiiM task saves the fingerprint/address while the loop reads settings
    static std::recursive_mutex m;
    return m;
}
std::map<std::string, std::string>& prefs() {
    static std::map<std::string, std::string> p;
    return p;
}
std::string prefsPath() { return pc::stateDir() + "/settings.txt"; }
void loadPrefs() {
    static bool loaded = false;
    if (loaded) return;
    loaded = true;
    std::ifstream f(prefsPath());
    std::string line;
    while (std::getline(f, line)) {
        auto t = line.find('\t');
        if (t == std::string::npos) continue;
        std::string v = line.substr(t + 1);
        for (size_t p = 0; (p = v.find("\\n", p)) != std::string::npos;) v.replace(p, 2, "\n");
        prefs()[line.substr(0, t)] = v;
    }
}
void savePrefs() {
    std::ofstream f(prefsPath());
    for (auto& kv : prefs()) {
        std::string v = kv.second;
        for (size_t p = 0; (p = v.find('\n', p)) != std::string::npos;) v.replace(p, 1, "\\n");
        f << kv.first << '\t' << v << '\n';
    }
}
}  // namespace pc_internal

bool Preferences::begin(const char*, bool) {
    std::lock_guard<std::recursive_mutex> lock(prefsLock());
    loadPrefs();
    return true;
}
void Preferences::end() {
    std::lock_guard<std::recursive_mutex> lock(prefsLock());
    savePrefs();
}
String Preferences::getString(const char* key, const String& def) {
    std::lock_guard<std::recursive_mutex> lock(prefsLock());
    auto it = prefs().find(key);
    return it == prefs().end() ? def : String(it->second);
}
bool Preferences::getBool(const char* key, bool def) {
    std::lock_guard<std::recursive_mutex> lock(prefsLock());
    auto it = prefs().find(key);
    return it == prefs().end() ? def : it->second == "1";
}
int Preferences::getInt(const char* key, int def) {
    std::lock_guard<std::recursive_mutex> lock(prefsLock());
    auto it = prefs().find(key);
    return it == prefs().end() ? def : std::atoi(it->second.c_str());
}
size_t Preferences::putString(const char* key, const String& v) {
    std::lock_guard<std::recursive_mutex> lock(prefsLock());
    prefs()[key] = v.c_str();
    return v.length();
}
size_t Preferences::putBool(const char* key, bool v) {
    std::lock_guard<std::recursive_mutex> lock(prefsLock());
    prefs()[key] = v ? "1" : "0";
    return 1;
}
size_t Preferences::putInt(const char* key, int v) {
    std::lock_guard<std::recursive_mutex> lock(prefsLock());
    prefs()[key] = std::to_string(v);
    return 4;
}
bool Preferences::remove(const char* key) {
    std::lock_guard<std::recursive_mutex> lock(prefsLock());
    return prefs().erase(key) > 0;
}
bool Preferences::clear() {
    std::lock_guard<std::recursive_mutex> lock(prefsLock());
    prefs().clear();
    return true;
}
bool Preferences::isKey(const char* key) {
    std::lock_guard<std::recursive_mutex> lock(prefsLock());
    return prefs().count(key) > 0;
}

void pc::defaultSettings() {
    std::lock_guard<std::recursive_mutex> lock(prefsLock());
    loadPrefs();
    if (prefs().count("ssid")) return;
    // First start: already configured, so the app opens on the main screen (not the Tab5's Wi-Fi setup portal)
#ifdef PC_DESKTOP
    prefs()["ssid"] = "PC";  // the computer is already on the network
    prefs()["host"] = "";    // the WiiM is found on the network (SSDP), or given with --wiim
    savePrefs();
    return;
#endif
    prefs()["ssid"] = "Simulateur";
#ifdef SIM_REAL
    // Real WiiM through the relay: the app looks for it (discovery by the relay), as a new Tab5 does
    prefs()["host"] = "";
#else
    prefs()["host"] = "wiim.sim";
    prefs()["wiimname"] = "WiiM simul\xC3\xA9";
    prefs()["deezeruser"] = "123456";
    prefs()["qobuzdirect"] = "1";  // fake WiiM: the tour also covers the album search (opt-in on a real Tab5)
#endif
    prefs()["oaikey"] = "sim-cle-factice-pour-le-micro";  // shows the microphone (dictation is simulated)
    savePrefs();
}
