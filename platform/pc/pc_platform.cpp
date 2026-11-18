// Computer layer (simulator and desktop build): command-line options, state and output folders, and main().
//
// The real src/main.cpp runs unchanged: setup() once, then loop() again and again. M5.update() (called by the
// app in every loop and every waiting screen) reads the mouse as the touch screen and shows the display canvas in
// the window. With --script, taps come from a file and screenshots are written (no window needed: --headless).
// The other concerns are in pc_platform_*.cpp (list in pc_internal.h).
#include <SDL2/SDL.h>
#include <sys/stat.h>

#include <csignal>
#include <filesystem>
#include <string>
#include <vector>

#include "Arduino.h"
#include "pc.h"
#include "pc_internal.h"

using namespace pc_internal;

// ------------------------------------------------------------------ options

namespace pc_internal {
#if defined(PC_DESKTOP)
std::string configDir() {
    const char* x = getenv("XDG_CONFIG_HOME");
    const char* h = getenv("HOME");
    return x && *x ? std::string(x) + "/wiimscreen" : std::string(h ? h : ".") + "/.config/wiimscreen";
}
std::string g_state = configDir(), g_out = configDir() + "/captures";
#elif defined(SIM_REAL)
std::string g_state = ".pio/simreal-state", g_out = ".pio/sim-out";
#else
std::string g_state = ".pio/sim-state", g_out = ".pio/sim-out";
#endif
std::string g_relay = getenv("WIIM_RELAY") ? getenv("WIIM_RELAY") : "127.0.0.1:8787";
bool g_quiet = false;
std::vector<std::string> g_argv;
}  // namespace pc_internal

const std::string& pc::stateDir() { return g_state; }
const std::string& pc::outDir() { return g_out; }
const std::string& pc::relay() { return g_relay; }
bool pc::quiet() { return g_quiet; }

void setup();
void loop();

int main(int argc, char** argv) {
    signal(SIGPIPE, SIG_IGN);
    umask(077);  // settings (OpenAI key, Wi-Fi password copy) and favorites: readable by this user only  // a closed connection must not end the program (send() returns an error instead)
    bool headless = false, portrait = false;
    std::string wiim;
    for (int i = 0; i < argc; ++i) g_argv.push_back(argv[i]);
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&] { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--headless") headless = true;
        else if (a == "--quiet") g_quiet = true;
        else if (a == "--scale") g_scale = std::max(0.25f, std::strtof(next().c_str(), nullptr));
        else if (a == "--state") g_state = next();
        else if (a == "--out") g_out = next();
        else if (a == "--relay") g_relay = next();
        else if (a == "--fullscreen") g_fullscreen = true;
        else if (a == "--size") {
            int w = 0, h = 0;
            if (sscanf(next().c_str(), "%dx%d", &w, &h) != 2 || w < 320 || h < 240 || w > 7680 || h > 7680) {
                fprintf(stderr, "--size : largeur x hauteur, par exemple 800x480\n");
                return 1;
            }
            g_long = std::max(w, h), g_short = std::min(w, h);
            portrait = h > w;
        } else if (a == "--wiim") wiim = next();
        else if (a == "--script") {
            if (!loadScript(next())) return 1;
        } else if (a == "--reset") {
            std::remove((g_state + "/settings.txt").c_str());
        } else {
            fprintf(stderr,
                    "usage: %s [--scale 0.6] [--script fichier] [--headless] [--state dossier] [--out dossier] "
                    "[--reset] [--quiet] [--relay h\xC3\xB4te:port] [--size 800x480] [--fullscreen] [--wiim adresse]\n",
                    argv[0]);
            return a == "--help" ? 0 : 1;
        }
    }
    std::error_code ec;
    std::filesystem::create_directories(g_state, ec);
    std::filesystem::create_directories(g_out, ec);
    pc::defaultSettings();
    // --wiim: this WiiM (also when discovery cannot work, e.g. WSL); --size taller than wide: portrait
    if (!wiim.empty() || portrait) {
        loadPrefs();
        auto get = [](const char* k) {  // (operator[] would add an empty value: read as rotation 0)
            auto it = prefs().find(k);
            return it == prefs().end() ? std::string() : it->second;
        };
        if (!wiim.empty() && get("host") != wiim) prefs()["host"] = wiim, prefs().erase("fp");
        const std::string rot = get("rotation");
        const bool isPortrait = rot == "0" || rot == "2";
        if (portrait && !isPortrait) prefs()["rotation"] = "0";
        if (!portrait && isPortrait) prefs()["rotation"] = "1";
        savePrefs();
    }
#ifdef PC_DESKTOP
    if (!g_quiet) printf("wiimscreen : r\xC3\xA9glages et favoris dans %s\n", g_state.c_str());
#endif
    if (!headless) {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            fprintf(stderr, "SDL : %s (essaie --headless)\n", SDL_GetError());
            return 1;
        }
#ifdef PC_DESKTOP
        const char* title = "wiimscreen";
#else
        const char* title = "wiimscreen \xE2\x80\x94 simulateur";
#endif
        const int w0 = portrait ? g_short : g_long, h0 = portrait ? g_long : g_short;
        g_win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, (int)(w0 * g_scale),
                                 (int)(h0 * g_scale),
                                 g_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_RESIZABLE);
        if (g_fullscreen) SDL_ShowCursor(SDL_DISABLE);  // touch screen: no mouse pointer
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
