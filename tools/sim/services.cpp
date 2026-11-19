// Simulator (both modes): sound, setup portal and update screens, dictation — no hardware, no web server.
#include <cmath>
#include <map>
#include <mutex>

#include "M5Unified.h"
#include "i18n.h"
#include "device/key_input.h"
#include "device/ota.h"
#include "device/portal.h"
#include "pc.h"
#include "app/version.h"
#include "net/voice_service.h"


// ------------------------------------------------------------------ setup portal and update page

// The OK or Back key (remote, keyboard): like a tap on the screen / on Cancel
static bool okOrBack() {
    wr::KeyEvent k;
    bool hit = false;
    while (takeKey(k)) hit = hit || k.key == wr::NavKey::Ok || k.key == wr::NavKey::Back;
    return hit;
}

void runSetupPortal(Settings& s, Ui& ui, const String& reason) {
    (void)s;
    ui.showPortal("WiiM-Remote-3F2A", "simulateur", "http://192.168.4.1",
                  reason + wr::L(" Simulator: tap the screen to go back.",
                                 " Simulateur : touche l'\xC3\xA9" "cran pour revenir."));
    const uint32_t shown = millis();
    for (;;) {
        M5.update();
        const bool key = okOrBack();
        if (millis() - shown > 800 && (M5.Touch.getDetail().wasPressed() || key)) ESP.restart();
        delay(10);
    }
}

void runUpdateMode(Ui& ui) {
    ui.showUpdate("http://192.168.1.42", "481203", FW_VERSION, -1,
                  wr::L("Simulator: no update possible here.", "Simulateur : aucune mise \xC3\xA0 jour possible ici."));
    for (;;) {
        M5.update();
        auto t = M5.Touch.getDetail();
        if ((t.wasPressed() && ui.hitUpdateCancel(t.x, t.y)) || okOrBack()) return;
        delay(10);
    }
}

// ------------------------------------------------------------------ dictation: a fixed sentence

bool transcribe(const int16_t*, size_t, uint32_t, const String&, const String&, const std::string&, std::string& text,
                String& error) {
    delay(1200);  // the network call takes about this long
    const char* t = getenv("SIM_DICTEE");
    text = t && *t ? t : "Miles Davis";
    error = "";
    return true;
}

