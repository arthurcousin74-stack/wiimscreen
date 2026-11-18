// PC desktop build: the Tab5 services that have no meaning on a computer.
// - Setup portal (Wi-Fi access point for a phone): the computer is already on the network; settings are a text
//   file (shown on the screen). - Update over Wi-Fi: a computer updates with git pull and a new build.
// - Sound: platform/pc/pc_sound.cpp (silent). - Dictation: no microphone support yet (the microphone button is not shown without
//   an OpenAI key; with one, « Micro indisponible »).
#include "M5Unified.h"
#include "device/ota.h"
#include "pc.h"
#include "device/portal.h"
#include "i18n.h"
#include "net/voice_service.h"


static void waitForTap(uint32_t afterMs) {
    const uint32_t shown = millis();
    for (;;) {
        M5.update();
        if (millis() - shown > afterMs && M5.Touch.getDetail().wasPressed()) return;
        delay(10);
    }
}

void runSetupPortal(Settings&, Ui& ui, const String& reason) {
    const String file = String(pc::stateDir().c_str()) + "/settings.txt";
    ui.showInfo(wr::L("Settings", "R\xC3\xA9glages"),
                reason + " " +
                    wr::L("On a computer, the settings are in this file (one line per setting, name and value "
                          "separated by a tab). Edit it, then start wiimscreen again: ",
                          "Sur un ordinateur, les r\xC3\xA9glages sont dans ce fichier (une ligne par r\xC3\xA9glage, nom et "
                          "valeur s\xC3\xA9par\xC3\xA9s par une tabulation). Modifie-le, puis relance wiimscreen : ") +
                    file,
                wr::L("Tap the screen to go back", "Touche l'\xC3\xA9" "cran pour revenir"));
    waitForTap(800);
    ESP.restart();
}

void runUpdateMode(Ui& ui) {
    ui.showInfo(wr::L("Update", "Mise \xC3\xA0 jour"),
                wr::L("On a computer: in the wiimscreen folder, run git pull, then pio run -e desktop.",
                      "Sur un ordinateur : dans le dossier wiimscreen, lance git pull, puis pio run -e desktop."),
                wr::L("Tap the screen to go back", "Touche l'\xC3\xA9" "cran pour revenir"));
    waitForTap(500);
}

bool transcribe(const int16_t*, size_t, uint32_t, const String&, const String&, const std::string&, std::string&,
                String& error) {
    error = wr::L("No microphone on this computer", "Pas de micro sur cet ordinateur");
    return false;
}
