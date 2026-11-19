// Computer layer: the SDL window that shows the display canvas, mouse/touch input as the touch screen,
// screenshots (.ppm, F12 key; the other keys go to pc_platform_keys.cpp), window scale / fullscreen / screen size,
// and the canvas size that fits the computer's screen (fitCanvasToScreen, windowScale).
// M5.update() (called by the app in every loop and every waiting screen) reads the input, runs the next --script
// steps, ticks the key repeats and shows the canvas.
// See pc_internal.h for the other pc_platform*.cpp files.
#include <SDL2/SDL.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstring>
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
bool g_sizeGiven = false, g_scaleGiven = false;
bool g_fullscreen = false;
bool g_forcePresent = true;  // window shown, resized or uncovered: draw even if the canvas did not change
std::vector<uint8_t> g_lastCanvas;
float g_lastDim = -1.0f;
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

// Usable area of the screen that shows the window (without the taskbar or the menu bar; full screen: all of it),
// in pixels (the Windows program is DPI aware: platform/windows/wiimscreen.manifest)
static bool screenArea(SDL_Rect& r) {
    const int i = g_win ? std::max(0, SDL_GetWindowDisplayIndex(g_win)) : 0;
    const int err = g_fullscreen ? SDL_GetDisplayBounds(i, &r) : SDL_GetDisplayUsableBounds(i, &r);
    return err == 0 && r.w > 0 && r.h > 0;
}

// Room for the window: 90 % of the usable area (frame and title bar included), all the screen in full screen
static void room(const SDL_Rect& a, float& w, float& h) {
    w = g_fullscreen ? (float)a.w : a.w * 0.9f;
    h = g_fullscreen ? (float)a.h : a.h * 0.9f;
}

void fitCanvasToScreen(bool portrait) {
    if (g_sizeGiven) return;
    SDL_Rect a;
    if (!screenArea(a)) return;
    float rw, rh;
    room(a, rw, rh);
    // 1280x720 times the largest step that fits: whole or simple steps keep lines and text crisp; at most 2
    // (2560x1440: bigger canvases cost a lot to copy every frame for little gain)
    for (float k : {2.0f, 1.75f, 1.5f, 1.25f}) {
        const int lng = (int)std::lround(1280 * k), shrt = (int)std::lround(720 * k);
        if ((portrait ? shrt : lng) <= rw && (portrait ? lng : shrt) <= rh) {
            g_long = lng, g_short = shrt;
            return;
        }
    }
}

float windowScale(int w, int h) {
    if (g_scaleGiven || g_fullscreen) return g_scale;
    SDL_Rect a;
    if (!screenArea(a)) return g_scale;
    float rw, rh;
    room(a, rw, rh);
    return std::min(1.0f, std::min(rw / w, rh / h));  // the canvas as is, smaller only when it does not fit
}

void present() {
    if (!g_ren) return;
    // Nothing new on the canvas (same pixels, same brightness) and nothing to repaint: no copy, no upload
    auto& D = M5.Display;
    const uint8_t* src = static_cast<const uint8_t*>(D.getBuffer());
    const size_t n = (size_t)D.width() * D.height() * 3;
    const float dim = D.brightness / 255.0f;
    if (src && !g_forcePresent && D.width() == g_texW && D.height() == g_texH && dim == g_lastDim &&
        g_lastCanvas.size() == n && std::memcmp(src, g_lastCanvas.data(), n) == 0)
        return;
    if (src) g_lastCanvas.assign(src, src + n);
    g_lastDim = dim;
    g_forcePresent = false;
    int w, h;
    readCanvas(g_rgb, w, h);
    if (!g_tex || w != g_texW || h != g_texH) {
        if (g_tex) SDL_DestroyTexture(g_tex);
        g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, w, h);
        g_texW = w;
        g_texH = h;
        if (!g_fullscreen) {
            const float s = windowScale(w, h);
            SDL_SetWindowSize(g_win, (int)std::lround(w * s), (int)std::lround(h * s));
        }
        // SDL scales the picture into the window (bars keep the shape) and maps the mouse/touch positions back
        SDL_RenderSetLogicalSize(g_ren, w, h);
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
                if (e.type == SDL_WINDOWEVENT) g_forcePresent = true;  // shown, resized, uncovered: draw again
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
