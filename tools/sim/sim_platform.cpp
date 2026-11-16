// Simulator platform: time, threads, settings file, Serial, restart, and the SDL window.
//
// The real src/main.cpp runs unchanged: setup() once, then loop() again and again. M5.update() (called by the
// app in every loop and every waiting screen) reads the mouse as the touch screen and shows the display canvas in
// the window. With --script, taps come from a file and screenshots are written (no window needed: --headless).
#include <SDL2/SDL.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include "M5Unified.h"
#include "Preferences.h"
#include "WiFi.h"
#include "sim.h"

SimM5 M5;
SerialSim Serial;
EspSim ESP;
WiFiSim WiFi;

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
uint32_t esp_random() { return (uint32_t)std::rand() * 2654435761u; }

// ------------------------------------------------------------------ Serial, restart

void SerialSim::println(const char* s) {
    if (!sim::quiet()) printf("%s\n", s);
}
void SerialSim::print(const char* s) {
    if (!sim::quiet()) printf("%s", s);
}
int SerialSim::printf(const char* fmt, ...) {
    if (sim::quiet()) return 0;
    va_list ap;
    va_start(ap, fmt);
    int n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}

void EspSim::restart() { sim::restart(); }

// ------------------------------------------------------------------ FreeRTOS

SemaphoreHandle_t xSemaphoreCreateMutex() { return new std::recursive_mutex(); }
BaseType_t xSemaphoreTake(SemaphoreHandle_t m, TickType_t) {
    static_cast<std::recursive_mutex*>(m)->lock();
    return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t m) {
    static_cast<std::recursive_mutex*>(m)->unlock();
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

// ------------------------------------------------------------------ settings (Preferences)

namespace {
std::map<std::string, std::string>& prefs() {
    static std::map<std::string, std::string> p;
    return p;
}
std::string prefsPath() { return sim::stateDir() + "/settings.txt"; }
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
}  // namespace

bool Preferences::begin(const char*, bool) {
    loadPrefs();
    return true;
}
void Preferences::end() { savePrefs(); }
String Preferences::getString(const char* key, const String& def) {
    auto it = prefs().find(key);
    return it == prefs().end() ? def : String(it->second);
}
bool Preferences::getBool(const char* key, bool def) {
    auto it = prefs().find(key);
    return it == prefs().end() ? def : it->second == "1";
}
int Preferences::getInt(const char* key, int def) {
    auto it = prefs().find(key);
    return it == prefs().end() ? def : std::atoi(it->second.c_str());
}
size_t Preferences::putString(const char* key, const String& v) {
    prefs()[key] = v.c_str();
    return v.length();
}
size_t Preferences::putBool(const char* key, bool v) {
    prefs()[key] = v ? "1" : "0";
    return 1;
}
size_t Preferences::putInt(const char* key, int v) {
    prefs()[key] = std::to_string(v);
    return 4;
}
bool Preferences::remove(const char* key) { return prefs().erase(key) > 0; }
bool Preferences::clear() {
    prefs().clear();
    return true;
}
bool Preferences::isKey(const char* key) { return prefs().count(key) > 0; }

void sim::defaultSettings() {
    loadPrefs();
    if (prefs().count("ssid")) return;
    // First start: already configured, so the simulator opens on the main screen (not the setup portal)
    prefs()["ssid"] = "Simulateur";
    prefs()["host"] = "wiim.sim";
    prefs()["wiimname"] = "WiiM simul\xC3\xA9";
    prefs()["deezeruser"] = "123456";
    prefs()["oaikey"] = "sim-cle-factice-pour-le-micro";  // shows the microphone (dictation is simulated)
    savePrefs();
}

// ------------------------------------------------------------------ microphone, power

bool SimMic::begin() { return on = true; }
bool SimMic::record(int16_t* buf, size_t len, uint32_t) {
    std::memset(buf, 0, len * 2);  // silence: the dictation text comes from transcribe() (fake)
    return on;
}
void SimPower::powerOff() {
    printf("Simulateur : mise en veille (fin du programme)\n");
    fflush(stdout), _exit(0);
}

// ------------------------------------------------------------------ screen, touch, script

namespace {

struct Step {
    enum Kind { Wait, Tap, Press, Move, Release, Shot, Quit } kind;
    int x = 0, y = 0, ms = 0;
    std::string name;
};

SDL_Window* g_win = nullptr;
SDL_Renderer* g_ren = nullptr;
SDL_Texture* g_tex = nullptr;
int g_texW = 0, g_texH = 0;
float g_scale = 1.0f;
uint32_t g_lastFrame = 0;
bool g_mouseDown = false, g_pressQueued = false, g_releaseQueued = false;
int g_mouseX = -1, g_mouseY = -1;
std::vector<Step> g_script;
size_t g_step = 0;
uint32_t g_stepAt = 0;
std::vector<uint8_t> g_rgb;

void readCanvas(std::vector<uint8_t>& out, int& w, int& h) {
    auto& D = M5.Display;
    w = D.width();
    h = D.height();
    out.resize((size_t)w * h * 3);
    const uint8_t* src = static_cast<const uint8_t*>(D.getBuffer());
    // 24-bit M5Canvas: bytes in memory are R, G, B (checked in tools/sim/README.md test)
    if (src) std::memcpy(out.data(), src, out.size());
    const float dim = M5.Display.brightness / 255.0f;  // backlight: dim the picture like the real screen
    if (dim < 0.99f)
        for (auto& b : out) b = (uint8_t)(b * dim);
}

void shot(const std::string& name) {
    int w, h;
    readCanvas(g_rgb, w, h);
    const std::string path = sim::outDir() + "/" + name + ".ppm";
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    fwrite(g_rgb.data(), 1, g_rgb.size(), f);
    fclose(f);
    printf("capture %s\n", path.c_str());
}

void present() {
    if (!g_ren) return;
    int w, h;
    readCanvas(g_rgb, w, h);
    if (!g_tex || w != g_texW || h != g_texH) {
        if (g_tex) SDL_DestroyTexture(g_tex);
        g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, w, h);
        g_texW = w;
        g_texH = h;
        SDL_SetWindowSize(g_win, (int)(w * g_scale), (int)(h * g_scale));
    }
    SDL_UpdateTexture(g_tex, nullptr, g_rgb.data(), w * 3);
    SDL_RenderClear(g_ren);
    SDL_RenderCopy(g_ren, g_tex, nullptr, nullptr);
    SDL_RenderPresent(g_ren);
}

void runScript() {
    while (g_step < g_script.size()) {
        const Step& s = g_script[g_step];
        if (s.kind == Step::Wait) {
            if (millis() - g_stepAt < (uint32_t)s.ms) return;
        } else if (s.kind == Step::Tap) {
            g_mouseX = s.x, g_mouseY = s.y;
            g_pressQueued = g_releaseQueued = true;
        } else if (s.kind == Step::Press) {
            g_mouseX = s.x, g_mouseY = s.y;
            g_mouseDown = g_pressQueued = true;
        } else if (s.kind == Step::Move) {
            g_mouseX = s.x, g_mouseY = s.y;
        } else if (s.kind == Step::Release) {
            g_mouseDown = false;
            g_releaseQueued = true;
        } else if (s.kind == Step::Shot) {
            shot(s.name);
        } else {
            fflush(stdout), _exit(0);
        }
        ++g_step;
        g_stepAt = millis();
        if (s.kind != Step::Wait && s.kind != Step::Shot) return;  // one touch per frame
    }
}

}  // namespace

void SimM5::begin(const SimConfig&) {
    Display.setRotation(1);
    Display.fillScreen(TFT_BLACK);
}

void SimM5::update() {
    if (g_win) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) fflush(stdout), _exit(0);
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                g_mouseDown = g_pressQueued = true;
                g_mouseX = (int)(e.button.x / g_scale), g_mouseY = (int)(e.button.y / g_scale);
            } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                g_mouseDown = false;
                g_releaseQueued = true;
            } else if (e.type == SDL_MOUSEMOTION && g_mouseDown) {
                g_mouseX = (int)(e.motion.x / g_scale), g_mouseY = (int)(e.motion.y / g_scale);
            } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_s) {
                static int n = 0;
                shot("capture-" + std::to_string(++n));
            }
        }
    }
    runScript();
    auto& d = Touch.detail;
    // One change per frame: a quick click is pressed on one frame and released on the next
    const bool was = d.pressed;
    d.justPressed = d.justReleased = false;
    if (g_pressQueued && !was) {
        g_pressQueued = false;
        d.pressed = d.justPressed = true;
    } else if (was && (g_releaseQueued || !g_mouseDown)) {
        g_releaseQueued = false;
        d.pressed = false;
        d.justReleased = true;
    } else if (!was) {
        g_releaseQueued = false;
    }
    d.x = g_mouseX;
    d.y = g_mouseY;
    if (millis() - g_lastFrame >= 16) {  // ~60 images/s
        g_lastFrame = millis();
        present();
    }
}

// ------------------------------------------------------------------ options

namespace {
std::string g_state = ".pio/sim-state", g_out = ".pio/sim-out";
bool g_quiet = false;
std::vector<std::string> g_argv;
}  // namespace

const std::string& sim::stateDir() { return g_state; }
const std::string& sim::outDir() { return g_out; }
bool sim::quiet() { return g_quiet; }

void sim::restart() {
    printf("Simulateur : red\xC3\xA9marrage\n");
    fflush(stdout);
    std::vector<char*> args;
    for (auto& a : g_argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);
    execv("/proc/self/exe", args.data());
    std::exit(1);
}

static bool loadScript(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream in(line);
        std::string cmd;
        in >> cmd;
        if (cmd.empty() || cmd[0] == '#') continue;
        Step s{Step::Wait};
        if (cmd == "wait") in >> s.ms;
        else if (cmd == "tap") s.kind = Step::Tap, in >> s.x >> s.y;
        else if (cmd == "press") s.kind = Step::Press, in >> s.x >> s.y;
        else if (cmd == "move") s.kind = Step::Move, in >> s.x >> s.y;
        else if (cmd == "release") s.kind = Step::Release;
        else if (cmd == "shot") s.kind = Step::Shot, in >> s.name;
        else if (cmd == "quit") s.kind = Step::Quit;
        else {
            fprintf(stderr, "script : commande inconnue « %s »\n", cmd.c_str());
            return false;
        }
        g_script.push_back(s);
    }
    return true;
}

void setup();
void loop();

int main(int argc, char** argv) {
    bool headless = false;
    for (int i = 0; i < argc; ++i) g_argv.push_back(argv[i]);
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&] { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--headless") headless = true;
        else if (a == "--quiet") g_quiet = true;
        else if (a == "--scale") g_scale = std::max(0.25f, std::strtof(next().c_str(), nullptr));
        else if (a == "--state") g_state = next();
        else if (a == "--out") g_out = next();
        else if (a == "--script") {
            if (!loadScript(next())) return 1;
        } else if (a == "--reset") {
            std::remove((g_state + "/settings.txt").c_str());
        } else {
            fprintf(stderr,
                    "usage: %s [--scale 0.6] [--script fichier] [--headless] [--state dossier] [--out dossier] "
                    "[--reset] [--quiet]\n",
                    argv[0]);
            return a == "--help" ? 0 : 1;
        }
    }
    mkdir(g_state.c_str(), 0755);
    mkdir(g_out.c_str(), 0755);
    sim::defaultSettings();
    if (!headless) {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            fprintf(stderr, "SDL : %s (essaie --headless)\n", SDL_GetError());
            return 1;
        }
        g_win = SDL_CreateWindow("wiimscreen \xE2\x80\x94 simulateur", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                 (int)(1280 * g_scale), (int)(720 * g_scale), 0);
        g_ren = g_win ? SDL_CreateRenderer(g_win, -1, 0) : nullptr;
        if (!g_ren) {
            fprintf(stderr, "SDL : %s\n", SDL_GetError());
            return 1;
        }
    }
    g_stepAt = millis();
    setup();
    for (;;) loop();
}
