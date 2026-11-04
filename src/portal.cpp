#include "portal.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_system.h>

#include <algorithm>
#include <vector>

static String esc(const String& s) { return wr::htmlEscape(s.c_str()).c_str(); }

// Mot de passe du Wi-Fi de configuration : 10 caractères sans ambiguïté (pas de 0/o, 1/l),
// tirés du générateur matériel. Saisi via le QR code dans la plupart des cas.
static String randomPassword(int n) {
    static const char kAlphabet[] = "abcdefghijkmnpqrstuvwxyz23456789";
    String s;
    for (int i = 0; i < n; ++i) s += kAlphabet[esp_random() % (sizeof kAlphabet - 1)];
    return s;
}

static String option(const char* value, const char* label, bool selected) {
    return String("<option value=\"") + value + "\"" + (selected ? " selected" : "") + ">" + label +
           "</option>";
}

static String formPage(const Settings& s, const std::vector<String>& ssids, const String& error) {
    String h;
    h.reserve(6000);
    h += F("<!doctype html><html lang=fr><head><meta charset=utf-8>"
           "<meta name=viewport content='width=device-width,initial-scale=1'>"
           "<title>Télécommande WiiM</title><style>"
           "body{font-family:system-ui,sans-serif;max-width:560px;margin:0 auto;padding:16px;color:#111}"
           "label{display:block;margin-top:16px;font-weight:600}"
           "input,select,textarea{width:100%;box-sizing:border-box;font-size:16px;padding:10px;"
           "margin-top:6px;border:1px solid #999;border-radius:8px}"
           "textarea{height:220px;font-family:ui-monospace,monospace}"
           "small{color:#555;display:block;margin-top:4px}"
           "button{margin-top:24px;width:100%;padding:14px;font-size:18px;border:0;border-radius:8px;"
           "background:#111;color:#fff}"
           ".err{background:#fdd;border:1px solid #c66;padding:10px;border-radius:8px}"
           ".row{display:flex;gap:12px}.row>div{flex:1}"
           "</style></head><body><h1>Télécommande WiiM</h1>");
    if (error.length()) h += "<p class=err>" + esc(error) + "</p>";

    h += F("<form method=post action=/save>");
    h += F("<label>Réseau Wi-Fi<input name=ssid list=ssids required maxlength=32 value=\"");
    h += esc(s.ssid) + F("\"></label><datalist id=ssids>");
    for (auto& n : ssids) h += "<option value=\"" + esc(n) + "\">";
    h += F("</datalist>");

    h += F("<label>Mot de passe Wi-Fi<input name=pass type=password maxlength=63 autocomplete=off");
    if (s.pass.length()) h += F(" placeholder='inchangé si vide'");
    h += F("></label>");

    h += F("<label>Adresse IP du WiiM<input name=host required maxlength=64 inputmode=decimal "
           "placeholder=192.168.1.50 value=\"");
    h += esc(s.host) + F("\"><small>App WiiM Home &gt; ton appareil &gt; Paramètres &gt; Informations. "
                         "Réserve cette IP dans ta box.</small></label>");

    h += F("<label>Presets<textarea name=presets maxlength=1200 placeholder='Vide : presets du WiiM'>");
    h += esc(s.presetsText) + F("</textarea><small><b>Laisse vide</b> pour reprendre les presets de l'app "
                                "WiiM Home, avec leurs noms et leurs images. Sinon, une ligne par bouton : "
                                "numéro=nom (12 boutons max), par exemple <code>1=Radio Swiss Jazz</code>."
                                "</small></label>");

    h += F("<div class=row><div><label>Colonnes<select name=columns>");
    for (int c = 1; c <= 4; ++c) h += option(String(c).c_str(), String(c).c_str(), s.columns == c);
    h += F("</select></label></div><div><label>Orientation<select name=rotation>");
    h += option("1", "Paysage", s.rotation == 1);
    h += option("3", "Paysage inversé", s.rotation == 3);
    h += option("0", "Portrait", s.rotation == 0);
    h += option("2", "Portrait inversé", s.rotation == 2);
    h += F("</select></label></div></div>");

    h += F("<div class=row><div><label>Pas du volume<input name=volstep type=number min=1 max=20 value=");
    h += String(s.volStep) + F("></label></div><div><label>Connexion<select name=scheme>");
    h += option("https", "HTTPS (recommandé)", s.https);
    h += option("http", "HTTP", !s.https);
    h += F("</select></label></div></div>");

    h += F("<label>Bips à chaque appui<select name=beep>");
    h += option("0", "Désactivés", s.beep == 0);
    h += option("1", "Discrets", s.beep == 1);
    h += option("2", "Normaux", s.beep == 2);
    h += option("3", "Forts", s.beep == 3);
    h += F("</select><small>Un clic à chaque appui, deux notes graves si le WiiM ne répond pas."
           "</small></label>");

    h += F("<div class=row><div><label>Visualiseur<select name=visu>");
    h += option("1", "Activé", s.visualizer);
    h += option("0", "Désactivé", !s.visualizer);
    h += F("</select></label></div><div><label>Réveil par mouvement<select name=motion>");
    h += option("1", "Activé", s.motionWake);
    h += option("0", "Désactivé", !s.motionWake);
    h += F("</select></label></div></div><small>Visualiseur : en vue ambiance, le micro écoute la pièce "
           "pour animer un spectre. Le son est analysé sur place, jamais enregistré ni envoyé. "
           "Réveil par mouvement : soulever ou déplacer le Tab5 rallume l'écran.</small>");

    if (s.fingerprint.length()) {
        h += F("<label><input type=checkbox name=forgetfp value=1 style='width:auto'> "
               "Oublier le certificat enregistré du WiiM</label><small>Empreinte actuelle : ");
        h += esc(s.fingerprint.substring(0, 16)) +
             F("… À cocher seulement après une réinitialisation ou un changement de WiiM.</small>");
    }
    h += F("<button type=submit>Enregistrer et redémarrer</button></form></body></html>");
    return h;
}

static const char* kDonePage =
    "<!doctype html><html lang=fr><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'></head>"
    "<body style='font-family:system-ui;padding:24px'><h1>Enregistré</h1>"
    "<p>La télécommande redémarre et se connecte à ton Wi-Fi. Tu peux fermer cette page.</p></body></html>";

void runSetupPortal(Settings& s, Ui& ui, const String& reason) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP_STA);

    // Réseaux visibles, proposés dans le formulaire
    std::vector<String> ssids;
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n && ssids.size() < 20; ++i) {
        String name = WiFi.SSID(i);
        if (name.length() && std::find(ssids.begin(), ssids.end(), name) == ssids.end()) ssids.push_back(name);
    }
    WiFi.scanDelete();

    char suffix[5];
    snprintf(suffix, sizeof suffix, "%04X", (unsigned)(ESP.getEfuseMac() & 0xFFFF));
    const String apName = String("WiiM-Remote-") + suffix;
    const String apPass = randomPassword(10);
    WiFi.softAP(apName.c_str(), apPass.c_str());
    delay(200);
    const IPAddress ip = WiFi.softAPIP();
    const String url = "http://" + ip.toString();

    // Toute résolution DNS pointe vers le portail : la page s'ouvre toute seule sur la plupart des téléphones
    DNSServer dns;
    dns.setErrorReplyCode(DNSReplyCode::NoError);
    dns.start(53, "*", ip);

    WebServer server(80);
    String error;

    server.on("/", HTTP_GET, [&] { server.send(200, "text/html; charset=utf-8", formPage(s, ssids, error)); });

    server.on("/save", HTTP_POST, [&] {
        Settings next = s;
        next.ssid = server.arg("ssid");
        next.ssid.trim();
        String pass = server.arg("pass");
        if (pass.length()) next.pass = pass;
        next.host = server.arg("host");
        next.host.trim();
        next.presetsText = server.arg("presets");
        next.presetsText.replace("\r", "");
        next.columns = constrain(server.arg("columns").toInt(), 1, 4);
        next.rotation = constrain(server.arg("rotation").toInt(), 0, 3);
        next.volStep = constrain(server.arg("volstep").toInt(), 1, 20);
        next.beep = constrain(server.arg("beep").toInt(), 0, 3);
        next.visualizer = server.arg("visu") != "0";
        next.motionWake = server.arg("motion") != "0";
        bool https = server.arg("scheme") != "http";
        if (https != s.https || next.host != s.host || server.hasArg("forgetfp")) next.fingerprint = "";
        next.https = https;

        std::string perr;
        String err;
        if (next.ssid.length() == 0 || next.ssid.length() > 32) err = "Nom de réseau Wi-Fi invalide.";
        else if (next.pass.length() && (next.pass.length() < 8 || next.pass.length() > 63))
            err = "Le mot de passe Wi-Fi doit faire entre 8 et 63 caractères.";
        else if (!wr::isValidHost(next.host.c_str())) err = "Adresse du WiiM invalide (exemple : 192.168.1.50).";
        else if (!presetsTextIsAuto(next.presetsText) &&
                 !wr::parsePresets(next.presetsText.c_str(), next.presets, perr))
            err = String("Presets : ") + perr.c_str();

        if (err.length()) {
            error = err;
            Settings shown = next;
            shown.pass = s.pass;  // ne jamais renvoyer le mot de passe dans la page
            server.send(400, "text/html; charset=utf-8", formPage(shown, ssids, error));
            return;
        }
        saveSettings(next);
        server.send(200, "text/html; charset=utf-8", kDonePage);
        delay(1500);
        ESP.restart();
    });

    server.onNotFound([&] {
        server.sendHeader("Location", url + "/", true);
        server.send(302, "text/plain", "");
    });
    server.begin();

    ui.showPortal(apName, apPass, url, reason);

    for (;;) {
        dns.processNextRequest();
        server.handleClient();
        delay(2);
    }
}
