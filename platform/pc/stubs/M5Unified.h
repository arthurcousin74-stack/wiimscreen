// PC (simulator and desktop): M5Unified for a computer. The display is an M5Canvas in memory (shown in an SDL
// window by pc_platform_screen.cpp), touch comes from the mouse or touch screen; microphone: silence (sim) or none.
#pragma once

#include <M5GFX.h>

#include "Arduino.h"

// No « using namespace lgfx »: lgfx has its own millis()/delay() for PC builds (ambiguous with Arduino's)
using lgfx::textdatum_t;

int pcScreenLong();
int pcScreenShort();

class SimDisplay : public M5Canvas {
public:
    // Rotation 1/3: landscape 1280×720, 0/2: portrait 720×1280 (like the Tab5 screen)
    // Screen size of the PC window (--size, default 1280×720 like the Tab5): rotation 1/3 landscape, 0/2 portrait
    void setRotation(int r) {
        const int w = r % 2 ? pcScreenLong() : pcScreenShort(), h = r % 2 ? pcScreenShort() : pcScreenLong();
        if (width() == w && height() == h) return;
        deleteSprite();
        setColorDepth(24);
        createSprite(w, h);
    }
    void setBrightness(uint8_t b) { brightness = b; }
    void setAutoDisplay(bool) {}
    void waitDisplay() {}
    void display() {}
    uint8_t brightness = 255;
};

struct SimTouchDetail {
    int x = -1, y = -1;
    bool pressed = false, justPressed = false, justReleased = false;
    bool wasPressed() const { return justPressed; }
    bool isPressed() const { return pressed; }
    bool wasReleased() const { return justReleased; }
    bool wasClicked() const { return justReleased; }
};

namespace m5 {
using touch_detail_t = SimTouchDetail;
}

struct SimTouch {
    SimTouchDetail getDetail() const { return detail; }
    SimTouchDetail detail;
};

struct SimMic {
    bool begin();
    void end() { on = false; }
    bool record(int16_t* buf, size_t len, uint32_t rate);
    bool isRecording() const { return false; }
    bool on = false;
};

struct SimImu {
    bool update() { return true; }
    bool getAccel(float* x, float* y, float* z) {
        *x = 0, *y = 0, *z = 1;
        return true;
    }
};

struct SimPower {
    int getBatteryLevel() const;  // 85 % in the simulator; none (-1) on a desktop
    [[noreturn]] void powerOff();
};

struct SimConfig {};

struct SimM5 {
    SimConfig config() const { return {}; }
    void begin(const SimConfig&);
    void update();
    SimDisplay Display;
    SimTouch Touch;
    SimMic Mic;
    SimImu Imu;
    SimPower Power;
};
extern SimM5 M5;
