#include "settings.h"

#include <Preferences.h>

bool presetsTextIsAuto(const String& text) {
    int start = 0;
    while (start <= (int)text.length()) {
        int end = text.indexOf('\n', start);
        if (end < 0) end = text.length();
        String line = text.substring(start, end);
        line.trim();
        if (line.length() && line[0] != '#') return false;
        start = end + 1;
    }
    return true;
}

bool Settings::autoPresets() const { return presetsTextIsAuto(presetsText); }

static const char* kNamespace = "wiimremote";

bool loadSettings(Settings& s) {
    Preferences p;
    p.begin(kNamespace, true);
    s.ssid = p.getString("ssid", "");
    s.pass = p.getString("pass", "");
    s.host = p.getString("host", "");
    s.https = p.getBool("https", true);
    s.fingerprint = p.getString("fp", "");
    s.presetsText = p.getString("presets", "");
    s.rotation = constrain(p.getInt("rotation", 1), 0, 3);
    s.volStep = constrain(p.getInt("volstep", 5), 1, 20);
    s.beep = constrain(p.getInt("beep", 2), 0, 3);
    s.visualizer = p.getBool("visu", true);
    s.motionWake = p.getBool("motion", true);
    p.end();

    std::string err;
    s.presets.clear();
    if (!s.autoPresets() && !wr::parsePresets(s.presetsText.c_str(), s.presets, err)) {
        s.presets.clear();  // presets repris du WiiM en attendant une liste valide
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
    p.putInt("rotation", s.rotation);
    p.putInt("volstep", s.volStep);
    p.putInt("beep", s.beep);
    p.putBool("visu", s.visualizer);
    p.putBool("motion", s.motionWake);
    p.end();
}

void saveFingerprint(const String& hex) {
    Preferences p;
    p.begin(kNamespace, false);
    p.putString("fp", hex);
    p.end();
}
