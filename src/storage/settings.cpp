#include "storage/settings.h"

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

// Tous les réglages enregistrés, une ligne chacun, dans l'ordre de la NVS et de la copie de secours (LittleFS).
// La clé sert aux deux (NVS : 15 caractères au plus) : ne jamais la changer, des appareils et des copies l'ont
// déjà écrite. Valeur par défaut : celle de struct Settings. Un membre de Settings sans sa ligne ne compile pas.
using Field = wr::SettingField<Settings, String>;
static constexpr Field kFields[] = {
    Field::text("ssid", &Settings::ssid),
    Field::text("pass", &Settings::pass),
    Field::text("host", &Settings::host),
    Field::text("wiimuuid", &Settings::wiimUuid),
    Field::text("wiimname", &Settings::wiimName),
    Field::flag("https", &Settings::https),
    Field::text("fp", &Settings::fingerprint),
    Field::text("presets", &Settings::presetsText),
    Field::number("rotation", &Settings::rotation, 0, 3),
    Field::number("volstep", &Settings::volStep, 1, 20),
    Field::number("beep", &Settings::beep, 0, 3),
    Field::flag("visu2", &Settings::visualizer),  // expérimental : désactivé par défaut
    Field::flag("motion", &Settings::motionWake),
    Field::number("sleep", &Settings::sleepMin, 0, 240, false),  // bornes : copie de secours seulement (historique)
    Field::text("oaikey", &Settings::openaiKey),
    Field::text("sttmodel", &Settings::sttModel, true),  // vide dans la copie de secours : modèle par défaut
    Field::text("deezeruser", &Settings::deezerUser),
    Field::text("lang", &Settings::lang),
    Field::flag("qobuzdirect", &Settings::qobuzDirect),
};
// + 1 : Settings::presets, déduit de presetsText
static_assert(wr::aggregateFieldCount<Settings>() == sizeof(kFields) / sizeof(kFields[0]) + 1,
              "un membre de Settings n'a pas sa ligne dans kFields (src/storage/settings.cpp)");

bool loadSettings(Settings& s) {
    const Settings d;  // valeurs par défaut
    Preferences p;
    p.begin(kNamespace, true);
    for (const Field& f : kFields) {
        switch (f.kind) {
            case wr::FieldKind::Text: s.*f.textMember = p.getString(f.key, d.*f.textMember); break;
            case wr::FieldKind::Flag: s.*f.flagMember = p.getBool(f.key, d.*f.flagMember); break;
            case wr::FieldKind::Number: {
                const int v = p.getInt(f.key, d.*f.numberMember);
                s.*f.numberMember = f.clampNvs ? constrain(v, f.lo, f.hi) : v;
                break;
            }
        }
    }
    p.end();

    std::string err;
    s.presets.clear();
    if (!s.autoPresets() && !wr::parsePresets(s.presetsText.c_str(), s.presets, err)) {
        s.presets.clear();  // presets repris du WiiM en attendant une liste valide
        return false;
    }
    return true;
}

std::string settingsToBackup(const Settings& s) { return wr::encodeSettings(s, kFields); }

bool settingsFromBackup(const std::string& text, Settings& s) {
    Settings r = s;
    wr::decodeSettings(text, r, Settings(), kFields);
    if (!r.configured()) return false;
    s = r;
    return true;
}

void saveSettings(const Settings& s) {
    Preferences p;
    p.begin(kNamespace, false);
    for (const Field& f : kFields) {
        switch (f.kind) {
            case wr::FieldKind::Text: p.putString(f.key, s.*f.textMember); break;
            case wr::FieldKind::Flag: p.putBool(f.key, s.*f.flagMember); break;
            case wr::FieldKind::Number: p.putInt(f.key, s.*f.numberMember); break;
        }
    }
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
