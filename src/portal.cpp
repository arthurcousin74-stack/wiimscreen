#include "portal.h"

#include <DNSServer.h>
#include <M5Unified.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_system.h>

#include <algorithm>
#include <vector>

#include "web_style.h"

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

// Clé API / nom de modèle : caractères visibles, sans espace (ils vont dans un en-tête HTTP)
static bool tokenOk(const String& v, unsigned minLen, unsigned maxLen) {
    if (v.length() < minLen || v.length() > maxLen) return false;
    for (unsigned i = 0; i < v.length(); ++i)
        if (v[i] <= 0x20 || v[i] >= 0x7f) return false;
    return true;
}

// Réseau Wi-Fi vu pendant la recherche (le plus fort d'abord)
struct Net {
    String name;
    int rssi;
    bool open;
};

static String bars(int rssi) {
    int n = rssi > -60 ? 3 : rssi > -72 ? 2 : 1;
    String b = "<span class=bars>";
    for (int i = 1; i <= 3; ++i) b += i <= n ? "<i class=f></i>" : "<i></i>";
    return b + "</span>";
}

static String formPage(const Settings& s, const std::vector<Net>& nets, const String& error) {
    String h;
    h.reserve(9000);
    h += FPSTR(kWebHead);
    h += F("<title>Configuration du Tab5</title></head><body>"
           "<header><div class=logo><i></i></div><div><h1>T\xC3\xA9l\xC3\xA9" "commande WiiM</h1>"
           "<p>Configuration du Tab5</p></div></header>");
    if (error.length()) h += "<p class=err>" + esc(error) + "</p>";
    h += F("<form method=post action=/save id=f>");

    // --- 1. Wi-Fi
    h += F("<section class=card><h2><b>1</b>Ton Wi-Fi</h2>");
    if (!nets.empty()) {
        h += F("<div class=nets>");
        for (auto& n : nets) {
            h += "<button type=button class=net data-s=\"" + esc(n.name) + "\"><span>" + esc(n.name) +
                 (n.open ? "" : " &#128274;") + "</span>" + bars(n.rssi) + "</button>";
        }
        h += F("</div>");
    }
    h += F("<label for=ssid>Nom du r\xC3\xA9seau</label><input id=ssid name=ssid required maxlength=32 "
           "autocomplete=off placeholder='Touche un r\xC3\xA9seau ou tape son nom' value=\"");
    h += esc(s.ssid) + F("\"><small>Ton r\xC3\xA9seau n'appara\xC3\xAEt pas ? Le Tab5 n'utilise que le 2,4 GHz.</small>");
    h += F("<label for=pass>Mot de passe</label><div class=pw><input id=pass name=pass type=password "
           "maxlength=63 autocomplete=off");
    if (s.pass.length()) h += F(" placeholder='Inchang\xC3\xA9 si vide'");
    h += F("><button type=button id=eye>Afficher</button></div>"
           "<small>Sur Android : <b>Param\xC3\xA8tres \xE2\x80\xBA Wi-Fi \xE2\x80\xBA ton r\xC3\xA9seau \xE2\x80\xBA Partager</b> "
           "affiche le mot de passe.</small></section>");

    // --- 2. WiiM
    h += F("<section class=card><h2><b>2</b>Ton WiiM</h2><label for=host>Adresse IP (facultatif)</label>"
           "<input id=host name=host maxlength=64 inputmode=decimal autocomplete=off "
           "placeholder='Vide : recherche automatique' value=\"");
    h += esc(s.host) + F("\"><small><b>Laisse vide</b> : le Tab5 trouve ton WiiM sur le Wi-Fi, et le retrouve si "
                         "son adresse change.</small></section>");

    // --- Presets
    h += F("<details class=card><summary>Presets</summary><div class=in>"
           "<textarea name=presets maxlength=1200 placeholder='Vide : presets de l&#39;app WiiM Home'>");
    h += esc(s.presetsText) + F("</textarea><small><b>Laisse vide</b> pour reprendre les favoris de l'app WiiM Home, "
                                "avec leurs images. Sinon, une ligne par bouton : <code>1=Radio Swiss Jazz</code> "
                                "(12 au plus).</small></div></details>");

    // --- Options
    h += F("<details class=card><summary>Options</summary><div class=in>"
           "<div class=row><div><label>Orientation</label><select name=rotation>");
    h += option("1", "Paysage", s.rotation == 1);
    h += option("3", "Paysage invers\xC3\xA9", s.rotation == 3);
    h += option("0", "Portrait", s.rotation == 0);
    h += option("2", "Portrait invers\xC3\xA9", s.rotation == 2);
    h += F("</select></div><div><label>Pas du volume</label><input name=volstep type=number min=1 max=20 value=");
    h += String(s.volStep) + F("></div></div><div class=row><div><label>Bips</label><select name=beep>");
    h += option("0", "D\xC3\xA9sactiv\xC3\xA9s", s.beep == 0);
    h += option("1", "Discrets", s.beep == 1);
    h += option("2", "Normaux", s.beep == 2);
    h += option("3", "Forts", s.beep == 3);
    h += F("</select></div><div><label>Connexion au WiiM</label><select name=scheme>");
    h += option("https", "HTTPS", s.https);
    h += option("http", "HTTP", !s.https);
    h += F("</select></div></div><div class=row><div><label>R\xC3\xA9veil au mouvement</label><select name=motion>");
    h += option("1", "Activ\xC3\xA9", s.motionWake);
    h += option("0", "D\xC3\xA9sactiv\xC3\xA9", !s.motionWake);
    h += F("</select></div><div><label>Visualiseur</label><select name=visu>");
    h += option("1", "Activ\xC3\xA9", s.visualizer);
    h += option("0", "D\xC3\xA9sactiv\xC3\xA9", !s.visualizer);
    h += F("</select></div></div><div class=row><div><label>Mise en veille</label><select name=sleep>");
    h += option("0", "Jamais", s.sleepMin == 0);
    h += option("30", "Apr\xC3\xA8s 30 min", s.sleepMin == 30);
    h += option("60", "Apr\xC3\xA8s 1 h", s.sleepMin == 60);
    h += option("120", "Apr\xC3\xA8s 2 h", s.sleepMin == 120);
    h += F("</select></div><div></div></div><small>Mise en veille : sans appui et sans musique, le Tab5 s'\xC3\xA9teint ; le "
           "bouton d'alimentation le rallume. HTTPS est conseill\xC3\xA9. Visualiseur (exp\xC3\xA9rimental) : en vue ambiance, le micro anime un spectre. Le son est "
           "analys\xC3\xA9 sur place, jamais enregistr\xC3\xA9 ni envoy\xC3\xA9.</small></div></details>");

    // --- Commande vocale
    h += F("<details class=card><summary>Commande vocale</summary><div class=in>"
           "<label for=oaikey>Cl\xC3\xA9 API OpenAI</label><div class=pw><input id=oaikey name=oaikey type=password "
           "maxlength=200 autocomplete=off");
    h += s.openaiKey.length() ? F(" placeholder='Enregistr\xC3\xA9" "e \xE2\x80\x94 inchang\xC3\xA9" "e si vide'")
                              : F(" placeholder='sk-\xE2\x80\xA6'");
    h += F("></div><small>\xC2\xAB Joue FIP \xC2\xBB, \xC2\xAB Mets France Culture \xC2\xBB, \xC2\xAB Plus fort \xC2\xBB\xE2\x80\xA6 "
           "Cl\xC3\xA9 \xC3\xA0 cr\xC3\xA9" "er sur platform.openai.com (quelques centimes par mois). Le son n'est "
           "envoy\xC3\xA9 que pendant l'\xC3\xA9" "coute. Vide : pas de bouton micro.</small>");
    if (s.openaiKey.length())
        h += F("<label class=chk><input type=checkbox name=oaidel value=1>Supprimer la cl\xC3\xA9 enregistr\xC3\xA9" "e</label>");
    h += F("<label for=sttmodel>Mod\xC3\xA8le de transcription</label><input id=sttmodel name=sttmodel maxlength=60 value=\"");
    h += esc(s.sttModel) + F("\"><small>\xC3\x80 changer seulement si OpenAI renomme ce mod\xC3\xA8le.</small></div></details>");

    // --- Avancé
    if (s.fingerprint.length()) {
        h += F("<details class=card><summary>Avanc\xC3\xA9</summary><div class=in><label class=chk>"
               "<input type=checkbox name=forgetfp value=1>Oublier le certificat enregistr\xC3\xA9 du WiiM</label>"
               "<small>Empreinte : <code>");
        h += esc(s.fingerprint.substring(0, 16)) +
             F("\xE2\x80\xA6</code>. Seulement apr\xC3\xA8s une r\xC3\xA9initialisation ou un changement de WiiM."
               "</small></div></details>");
    }

    h += F("<div class=bar><button class=btn type=submit id=go>Enregistrer et red\xC3\xA9marrer</button></div></form>"
           "<script>"
           "var s=document.getElementById('ssid'),p=document.getElementById('pass');"
           "function mark(){document.querySelectorAll('.net').forEach(function(b){"
           "b.classList.toggle('on',b.dataset.s===s.value)})}"
           "document.querySelectorAll('.net').forEach(function(b){b.onclick=function(){s.value=b.dataset.s;mark();p.focus()}});"
           "s.oninput=mark;mark();"
           "document.getElementById('eye').onclick=function(){var t=p.type=='password';p.type=t?'text':'password';"
           "this.textContent=t?'Masquer':'Afficher'};"
           "document.getElementById('f').onsubmit=function(){var g=document.getElementById('go');"
           "g.disabled=true;g.textContent='Enregistrement\xE2\x80\xA6'};"
           "</script></body></html>");
    return h;
}

static String donePage(const String& ssid) {
    String h = FPSTR(kWebHead);
    h += F("<title>Enregistr\xC3\xA9</title></head><body><header><div class=logo><i></i></div><div>"
           "<h1>C'est enregistr\xC3\xA9</h1><p>T\xC3\xA9l\xC3\xA9" "commande WiiM</p></div></header>"
           "<div class='card okbox'>Le Tab5 red\xC3\xA9marre et se connecte \xC3\xA0 <b>");
    h += esc(ssid) + F("</b>.</div><section class=card><h2>Et ensuite ?</h2><small>Ton t\xC3\xA9l\xC3\xA9phone va "
                       "quitter le Wi-Fi du Tab5 : tu peux fermer cette page. Si un seul WiiM est sur ton r\xC3\xA9seau, "
                       "il est choisi tout seul ; sinon, touche le tien sur l'\xC3\xA9" "cran du Tab5.</small>"
                       "</section></body></html>");
    return h;
}

void runSetupPortal(Settings& s, Ui& ui, const String& reason) {
    ui.showInfo("Configuration", "Pr\xC3\xA9paration : recherche des r\xC3\xA9seaux Wi-Fi\xE2\x80\xA6");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP_STA);

    // Réseaux visibles, proposés dans le formulaire
    std::vector<Net> ssids;
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; ++i) {
        String name = WiFi.SSID(i);
        if (!name.length()) continue;
        auto it = std::find_if(ssids.begin(), ssids.end(), [&](const Net& x) { return x.name == name; });
        if (it == ssids.end()) ssids.push_back(Net{name, WiFi.RSSI(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN});
        else it->rssi = std::max(it->rssi, (int)WiFi.RSSI(i));
    }
    WiFi.scanDelete();
    std::sort(ssids.begin(), ssids.end(), [](const Net& a, const Net& b) { return a.rssi > b.rssi; });
    if (ssids.size() > 12) ssids.resize(12);

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
        next.rotation = constrain(server.arg("rotation").toInt(), 0, 3);
        next.volStep = constrain(server.arg("volstep").toInt(), 1, 20);
        next.beep = constrain(server.arg("beep").toInt(), 0, 3);
        next.visualizer = server.arg("visu") != "0";
        next.motionWake = server.arg("motion") != "0";
        next.sleepMin = constrain(server.arg("sleep").toInt(), 0, 240);
        String key = server.arg("oaikey");
        key.trim();
        if (key.length()) next.openaiKey = key;
        if (server.hasArg("oaidel")) next.openaiKey = "";
        next.sttModel = server.arg("sttmodel");
        next.sttModel.trim();
        if (!next.sttModel.length()) next.sttModel = "gpt-4o-mini-transcribe";
        bool https = server.arg("scheme") != "http";
        if (https != s.https || next.host != s.host || server.hasArg("forgetfp")) next.fingerprint = "";
        if (next.host != s.host) next.wiimUuid = next.wiimName = "";  // autre WiiM : identité à relire
        next.https = https;

        std::string perr;
        String err;
        if (next.ssid.length() == 0 || next.ssid.length() > 32) err = "Nom de réseau Wi-Fi invalide.";
        else if (next.pass.length() && (next.pass.length() < 8 || next.pass.length() > 63))
            err = "Le mot de passe Wi-Fi doit faire entre 8 et 63 caractères.";
        else if (next.host.length() && !wr::isValidHost(next.host.c_str())) err = "Adresse du WiiM invalide (exemple : 192.168.1.50).";
        else if (next.openaiKey.length() && !tokenOk(next.openaiKey, 20, 200))
            err = "Clé OpenAI invalide : colle-la sans espace ni retour à la ligne.";
        else if (!tokenOk(next.sttModel, 1, 60)) err = "Nom de modèle de transcription invalide.";
        else if (!presetsTextIsAuto(next.presetsText) &&
                 !wr::parsePresets(next.presetsText.c_str(), next.presets, perr))
            err = String("Presets : ") + perr.c_str();

        if (err.length()) {
            error = err;
            Settings shown = next;
            shown.pass = s.pass;  // ne jamais renvoyer le mot de passe dans la page
            shown.openaiKey = s.openaiKey;  // ni la clé API (seulement « inchangée si vide »)
            server.send(400, "text/html; charset=utf-8", formPage(shown, ssids, error));
            return;
        }
        saveSettings(next);
        server.send(200, "text/html; charset=utf-8", donePage(next.ssid));
        delay(1500);
        ESP.restart();
    });

    server.onNotFound([&] {
        server.sendHeader("Location", url + "/", true);
        server.send(302, "text/plain", "");
    });
    server.begin();

    // Déjà configuré : un appui sur l'écran annule (redémarrage normal, rien n'est modifié)
    const bool canCancel = s.configured();
    ui.showPortal(apName, apPass, url,
                  canCancel ? reason + " Touche l'\xC3\xA9" "cran du Tab5 pour annuler." : reason);

    const uint32_t shownAt = millis();
    for (;;) {
        dns.processNextRequest();
        server.handleClient();
        M5.update();
        if (canCancel && millis() - shownAt > 1500 && M5.Touch.getDetail().wasPressed()) {
            ui.showInfo("Configuration", "Annul\xC3\xA9" "e : red\xC3\xA9marrage\xE2\x80\xA6");
            delay(400);
            ESP.restart();
        }
        delay(2);
    }
}
