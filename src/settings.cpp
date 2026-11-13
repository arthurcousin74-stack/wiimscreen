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
    s.wiimUuid = p.getString("wiimuuid", "");
    s.wiimName = p.getString("wiimname", "");
    s.https = p.getBool("https", true);
    s.fingerprint = p.getString("fp", "");
    s.presetsText = p.getString("presets", "");
    s.rotation = constrain(p.getInt("rotation", 1), 0, 3);
    s.volStep = constrain(p.getInt("volstep", 5), 1, 20);
    s.beep = constrain(p.getInt("beep", 2), 0, 3);
    s.visualizer = p.getBool("visu2", false);  // expérimental : désactivé par défaut
    s.motionWake = p.getBool("motion", true);
    s.sleepMin = p.getInt("sleep", 0);
    s.openaiKey = p.getString("oaikey", "");
    s.sttModel = p.getString("sttmodel", "gpt-4o-mini-transcribe");
    p.end();

    std::string err;
    s.presets.clear();
    if (!s.autoPresets() && !wr::parsePresets(s.presetsText.c_str(), s.presets, err)) {
        s.presets.clear();  // presets repris du WiiM en attendant une liste valide
        return false;
    }
    return true;
}

std::string settingsToBackup(const Settings& s) {
    std::vector<std::pair<std::string, std::string>> f = {
        {"ssid", s.ssid.c_str()},         {"pass", s.pass.c_str()},       {"host", s.host.c_str()},
        {"wiimuuid", s.wiimUuid.c_str()}, {"wiimname", s.wiimName.c_str()}, {"https", s.https ? "1" : "0"},
        {"fp", s.fingerprint.c_str()},    {"presets", s.presetsText.c_str()}, {"rotation", std::to_string(s.rotation)},
        {"volstep", std::to_string(s.volStep)}, {"beep", std::to_string(s.beep)}, {"visu2", s.visualizer ? "1" : "0"},
        {"motion", s.motionWake ? "1" : "0"},   {"sleep", std::to_string(s.sleepMin)},
        {"oaikey", s.openaiKey.c_str()},  {"sttmodel", s.sttModel.c_str()}};
    return wr::encodeFields(f);
}

bool settingsFromBackup(const std::string& text, Settings& s) {
    Settings r = s;
    for (auto& kv : wr::decodeFields(text)) {
        const std::string& k = kv.first;
        const String v = kv.second.c_str();
        if (k == "ssid") r.ssid = v;
        else if (k == "pass") r.pass = v;
        else if (k == "host") r.host = v;
        else if (k == "wiimuuid") r.wiimUuid = v;
        else if (k == "wiimname") r.wiimName = v;
        else if (k == "https") r.https = v != "0";
        else if (k == "fp") r.fingerprint = v;
        else if (k == "presets") r.presetsText = v;
        else if (k == "rotation") r.rotation = constrain(v.toInt(), 0, 3);
        else if (k == "volstep") r.volStep = constrain(v.toInt(), 1, 20);
        else if (k == "beep") r.beep = constrain(v.toInt(), 0, 3);
        else if (k == "visu2") r.visualizer = v == "1";
        else if (k == "motion") r.motionWake = v != "0";
        else if (k == "sleep") r.sleepMin = constrain(v.toInt(), 0, 240);
        else if (k == "oaikey") r.openaiKey = v;
        else if (k == "sttmodel") r.sttModel = v.length() ? v : String("gpt-4o-mini-transcribe");
    }
    if (!r.configured()) return false;
    s = r;
    return true;
}

void saveSettings(const Settings& s) {
    Preferences p;
    p.begin(kNamespace, false);
    p.putString("ssid", s.ssid);
    p.putString("pass", s.pass);
    p.putString("host", s.host);
    p.putString("wiimuuid", s.wiimUuid);
    p.putString("wiimname", s.wiimName);
    p.putBool("https", s.https);
    p.putString("fp", s.fingerprint);
    p.putString("presets", s.presetsText);
    p.putInt("rotation", s.rotation);
    p.putInt("volstep", s.volStep);
    p.putInt("beep", s.beep);
    p.putBool("visu2", s.visualizer);
    p.putBool("motion", s.motionWake);
    p.putInt("sleep", s.sleepMin);
    p.putString("oaikey", s.openaiKey);
    p.putString("sttmodel", s.sttModel);
    p.end();
}

void saveFingerprint(const String& hex) {
    Preferences p;
    p.begin(kNamespace, false);
    p.putString("fp", hex);
    p.end();
}

void eraseSettings() {
    Preferences p;
    p.begin(kNamespace, false);
    p.clear();
    p.end();
}

void requestPortal(const String& reason) {
    Preferences p;
    p.begin(kNamespace, false);
    p.putString("portal", reason.length() ? reason : String("Reconfiguration"));
    p.end();
}

bool takePortalRequest(String& reason) {
    Preferences p;
    p.begin(kNamespace, false);
    reason = p.getString("portal", "");
    if (reason.length()) p.remove("portal");
    p.end();
    return reason.length() > 0;
}

void saveWiim(const String& host, const String& uuid, const String& name) {
    Preferences p;
    p.begin(kNamespace, false);
    p.putString("host", host);
    p.putString("wiimuuid", uuid);
    p.putString("wiimname", name);
    p.end();
}
