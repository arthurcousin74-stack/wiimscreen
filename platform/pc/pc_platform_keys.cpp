// Computer layer: key input (keyboard, media keys, game controllers and joysticks) → wr::KeyTracker → takeKey().
// SDL events arrive in pc_platform_screen.cpp (M5.update()), which passes them to keyEvent(); the --script runner
// calls keyDown/keyUp/keyText. M5.update() then calls keyTick() for repeats and long presses.
// Threads: SDL_PollEvent, the script runner and the app's loop() all run on the main thread (M5.update() is only
// called from loop() and the app's waiting screens); the mutex keeps the queue safe if that ever changes.
// With --log-keys, each event the app takes is printed: "[key] key=<name> kind=<press|long|repeat>" (terminal only).
// See pc_internal.h for the other pc_platform*.cpp files.
#include <SDL2/SDL.h>

#include <cstdio>
#include <map>
#include <mutex>
#include <string>

#include "Arduino.h"
#include "device/key_input.h"
#include "pc_internal.h"

namespace pc_internal {

bool g_logKeys = false;

namespace {

std::mutex g_keyMutex;
wr::KeyTracker g_keys;                                // guarded by g_keyMutex
std::map<SDL_JoystickID, SDL_GameController*> g_pads;  // devices SDL knows as game controllers
std::map<SDL_JoystickID, SDL_Joystick*> g_sticks;      // other joysticks (some TV remotes: D-pad as a hat)
std::map<std::pair<SDL_JoystickID, int>, uint8_t> g_hats;  // last value of each hat

wr::NavKey fromKeycode(SDL_Keycode k) {
    using wr::NavKey;
    switch (k) {
        case SDLK_UP: return NavKey::Up;
        case SDLK_DOWN: return NavKey::Down;
        case SDLK_LEFT: return NavKey::Left;
        case SDLK_RIGHT: return NavKey::Right;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SELECT: return NavKey::Ok;
        case SDLK_ESCAPE:
        case SDLK_AC_BACK: return NavKey::Back;
        case SDLK_SPACE:
        case SDLK_AUDIOPLAY: return NavKey::PlayPause;
        case SDLK_AUDIOSTOP: return NavKey::Pause;
        case SDLK_AUDIONEXT: return NavKey::Next;
        case SDLK_AUDIOPREV: return NavKey::Prev;
        case SDLK_AUDIOFASTFORWARD: return NavKey::FastFwd;
        case SDLK_AUDIOREWIND: return NavKey::Rewind;
        case SDLK_VOLUMEUP: return NavKey::VolUp;
        case SDLK_VOLUMEDOWN: return NavKey::VolDown;
        case SDLK_AUDIOMUTE:
        case SDLK_MUTE: return NavKey::Mute;
        case SDLK_MENU:
        case SDLK_F1: return NavKey::Menu;
        case SDLK_AC_SEARCH:
        case SDLK_F3: return NavKey::Search;
        case SDLK_BACKSPACE: return NavKey::Backspace;
        default: return NavKey::None;
    }
}

wr::NavKey fromPadButton(uint8_t b) {
    using wr::NavKey;
    switch (b) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP: return NavKey::Up;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return NavKey::Down;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return NavKey::Left;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return NavKey::Right;
        case SDL_CONTROLLER_BUTTON_A: return NavKey::Ok;
        case SDL_CONTROLLER_BUTTON_B: return NavKey::Back;
        case SDL_CONTROLLER_BUTTON_START: return NavKey::Menu;
        default: return NavKey::None;
    }
}

void hatMotion(SDL_JoystickID id, int hat, uint8_t value, uint32_t now) {
    uint8_t& last = g_hats[{id, hat}];
    static const struct {
        uint8_t bit;
        wr::NavKey key;
    } kDirs[] = {{SDL_HAT_UP, wr::NavKey::Up},
                 {SDL_HAT_DOWN, wr::NavKey::Down},
                 {SDL_HAT_LEFT, wr::NavKey::Left},
                 {SDL_HAT_RIGHT, wr::NavKey::Right}};
    for (const auto& d : kDirs) {
        const bool was = last & d.bit, is = value & d.bit;
        if (is && !was) g_keys.down(d.key, now);
        if (was && !is) g_keys.up(d.key, now);
    }
    last = value;
}

}  // namespace

void initKeyInput() {
    // Keyboard text input (SDL_TEXTINPUT) is on by default once the video subsystem runs on a desktop (SDL 2.x
    // calls SDL_StartTextInput itself where there is no on-screen keyboard): nothing to start here.
    // Controllers: with no device (or the dummy video driver) this only initialises the subsystem.
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
        fprintf(stderr, "SDL : manettes indisponibles (%s)\n", SDL_GetError());
}

bool keyEvent(const SDL_Event& e) {
    const uint32_t now = millis();
    std::lock_guard<std::mutex> lock(g_keyMutex);
    switch (e.type) {
        case SDL_KEYDOWN:
            g_keys.down(fromKeycode(e.key.keysym.sym), now);  // OS auto-repeat: ignored by the tracker
            return true;
        case SDL_KEYUP:
            g_keys.up(fromKeycode(e.key.keysym.sym), now);
            return true;
        case SDL_TEXTINPUT:
            g_keys.text(e.text.text);
            return true;
        case SDL_WINDOWEVENT:
            // A key held while the window loses the focus never sends its key-up: no endless repeat
            if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) g_keys.cancelAll();
            return false;
        case SDL_CONTROLLERDEVICEADDED:
            if (g_pads.count(SDL_JoystickGetDeviceInstanceID(e.cdevice.which))) return true;  // already open
            if (SDL_GameController* c = SDL_GameControllerOpen(e.cdevice.which))
                g_pads[SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(c))] = c;
            return true;
        case SDL_CONTROLLERDEVICEREMOVED: {
            auto it = g_pads.find(e.cdevice.which);
            if (it != g_pads.end()) SDL_GameControllerClose(it->second), g_pads.erase(it);
            g_keys.cancelAll();
            return true;
        }
        case SDL_CONTROLLERBUTTONDOWN:
            g_keys.down(fromPadButton(e.cbutton.button), now);
            return true;
        case SDL_CONTROLLERBUTTONUP:
            g_keys.up(fromPadButton(e.cbutton.button), now);
            return true;
        case SDL_JOYDEVICEADDED:
            // A game controller also arrives as a joystick: open it once, as a controller (event above)
            if (!SDL_IsGameController(e.jdevice.which) && !g_sticks.count(SDL_JoystickGetDeviceInstanceID(e.jdevice.which)))
                if (SDL_Joystick* j = SDL_JoystickOpen(e.jdevice.which)) g_sticks[SDL_JoystickInstanceID(j)] = j;
            return true;
        case SDL_JOYDEVICEREMOVED: {
            auto it = g_sticks.find(e.jdevice.which);
            if (it != g_sticks.end()) SDL_JoystickClose(it->second), g_sticks.erase(it);
            for (auto h = g_hats.begin(); h != g_hats.end();) h = h->first.first == e.jdevice.which ? g_hats.erase(h) : ++h;
            g_keys.cancelAll();
            return true;
        }
        case SDL_JOYHATMOTION:
            // Only joysticks that are not controllers: a controller's D-pad comes as buttons
            if (g_sticks.count(e.jhat.which)) hatMotion(e.jhat.which, e.jhat.hat, e.jhat.value, now);
            return true;
        default:
            return false;
    }
}

void keyDown(wr::NavKey key) {
    std::lock_guard<std::mutex> lock(g_keyMutex);
    g_keys.down(key, millis());
}

void keyUp(wr::NavKey key) {
    std::lock_guard<std::mutex> lock(g_keyMutex);
    g_keys.up(key, millis());
}

void keyText(const std::string& utf8) {
    std::lock_guard<std::mutex> lock(g_keyMutex);
    g_keys.text(utf8);
}

void keyTick() {
    std::lock_guard<std::mutex> lock(g_keyMutex);
    g_keys.tick(millis());
}

}  // namespace pc_internal

bool takeKey(wr::KeyEvent& out) {
    {
        std::lock_guard<std::mutex> lock(pc_internal::g_keyMutex);
        if (!pc_internal::g_keys.take(out)) return false;
    }
    if (pc_internal::g_logKeys) {
        if (out.key == wr::NavKey::Text)
            printf("[key] key=text kind=%s | %s\n", wr::keyKindName(out.kind), out.text.c_str());
        else
            printf("[key] key=%s kind=%s\n", wr::navKeyName(out.key), wr::keyKindName(out.kind));
    }
    return true;
}
