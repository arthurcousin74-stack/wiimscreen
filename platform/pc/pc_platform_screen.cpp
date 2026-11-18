// Computer layer: the SDL window that shows the display canvas, mouse/touch input as the touch screen,
// screenshots (.ppm, F12 key; the other keys go to pc_platform_keys.cpp), window scale / fullscreen / screen size.
// M5.update() (called by the app in every loop and every waiting screen) reads the input, runs the next --script
// steps, ticks the key repeats and shows the canvas.
// See pc_internal.h for the other pc_platform*.cpp files.
#include <SDL2/SDL.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "M5Unified.h"
#include "pc.h"
#include "pc_internal.h"

using namespace pc_internal;

// ------------------------------------------------------------------ screen, touch

namespace pc_internal {

SDL_Window* g_win = nullptr;
SDL_Renderer* g_ren = nullptr;
SDL_Texture* g_tex = nullptr;
int g_texW = 0, g_texH = 0;
float g_scale = 1.0f;
int g_long = 1280, g_short = 720;  // --size
bool g_fullscreen = false;
uint32_t g_lastFrame = 0;
bool g_mouseDown = false, g_pressQueued = false, g_releaseQueued = false;
int g_mouseX = -1, g_mouseY = -1;
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
    const std::string path = pc::outDir() + "/" + name + ".ppm";
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
        if (!g_fullscreen) SDL_SetWindowSize(g_win, (int)(w * g_scale), (int)(h * g_scale));
        SDL_RenderSetLogicalSize(g_ren, w, h);  // SDL scales the picture and the mouse/touch positions
    }
    SDL_UpdateTexture(g_tex, nullptr, g_rgb.data(), w * 3);
    SDL_RenderClear(g_ren);
    SDL_RenderCopy(g_ren, g_tex, nullptr, nullptr);
    SDL_RenderPresent(g_ren);
}

}  // namespace pc_internal

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
                g_mouseX = e.button.x, g_mouseY = e.button.y;
            } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                g_mouseDown = false;
                g_releaseQueued = true;
            } else if (e.type == SDL_MOUSEMOTION && g_mouseDown) {
                g_mouseX = e.motion.x, g_mouseY = e.motion.y;
            } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F12) {
                if (!e.key.repeat) {
                    static int n = 0;
                    shot("capture-" + std::to_string(++n));
                }
            } else {
                keyEvent(e);  // keys, text, controllers (pc_platform_keys.cpp)
            }
        }
    }
    runScript();
    keyTick();
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

int pcScreenLong() { return g_long; }
int pcScreenShort() { return g_short; }
