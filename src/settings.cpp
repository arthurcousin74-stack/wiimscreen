#include "settings.h"

#include <Preferences.h>

const char* kDefaultPresets =
    "1=Preset 1\n"
    "2=Preset 2\n"
    "3=Preset 3\n"
    "4=Preset 4\n"
    "5=Preset 5\n"
    "6=Preset 6\n";

static const char* kNamespace = "wiimremote";

bool loadSettings(Settings& s) {
    Preferences p;
    p.begin(kNamespace, true);
    s.ssid = p.getString("ssid", "");
    s.pass = p.getString("pass", "");
    s.host = p.getString("host", "");
    s.https = p.getBool("https", true);
    s.fingerprint = p.getString("fp", "");
    s.presetsText = p.getString("presets", kDefaultPresets);
    s.columns = constrain(p.getInt("columns", 3), 1, 4);
    s.rotation = constrain(p.getInt("rotation", 1), 0, 3);
    s.volStep = constrain(p.getInt("volstep", 5), 1, 20);
    s.beep = constrain(p.getInt("beep", 2), 0, 3);
    p.end();

    std::string err;
    if (!wr::parsePresets(s.presetsText.c_str(), s.presets, err)) {
        wr::parsePresets(kDefaultPresets, s.presets, err);
        return false;
    }
    return true;
}

void saveSettings(const Settings& s) {
    Preferences p;
    p.begin(kNamespace, false);
    p.putString("ssid", s.ssid);
    p.putString("pass", s.pass);
    p.putString("host", s.host);
    p.putBool("https", s.https);
    p.putString("fp", s.fingerprint);
    p.putString("presets", s.presetsText);
    p.putInt("columns", s.columns);
    p.putInt("rotation", s.rotation);
    p.putInt("volstep", s.volStep);
    p.putInt("beep", s.beep);
    p.end();
}

void saveFingerprint(const String& hex) {
    Preferences p;
    p.begin(kNamespace, false);
    p.putString("fp", hex);
    p.end();
}
