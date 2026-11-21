#include "device/portal.h"

#include <DNSServer.h>
#include <M5Unified.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_system.h>

#include <algorithm>
#include <vector>

#include "storage/favorites.h"
#include "i18n.h"
#include "voice.h"
#include "device/web_style.h"

static String esc(const String& s) { return wr::htmlEscape(s.c_str()).c_str(); }

// Mot de passe du Wi-Fi de configuration : 10 caractères sans ambiguïté (pas de 0/o, 1/l),
// tirés du générateur matériel. Saisi via le QR code dans la plupart des cas.
static String randomPassword(int n) {
    static const char kAlphabet[] = "abcdefghijkmnpqrstuvwxyz23456789";
    String s;
    for (int i = 0; i < n; ++i) s += kAlphabet[esp_random() % (sizeof kAlphabet - 1)];
    return s;
}

// Jeton anti-CSRF tiré à chaque démarrage du portail : une page d'un autre site (le téléphone garde souvent ses
// données mobiles) ne peut pas le lire, donc ne peut pas poster /save à notre place.
static String g_formToken;

static String randomToken() {
    static const char kHex[] = "0123456789abcdef";
    String t;
    for (int i = 0; i < 4; ++i) {
        uint32_t r = esp_random();
        for (int j = 0; j < 4; ++j, r >>= 8) {
            t += kHex[(r >> 4) & 0xF];
            t += kHex[r & 0xF];
        }
    }
    return t;  // 32 caractères hexadécimaux (128 bits)
}

// Comparaison en temps constant
static bool sameToken(const String& a, const String& b) {
    if (a.length() != b.length() || a.length() == 0) return false;
    uint8_t d = 0;
    for (unsigned i = 0; i < a.length(); ++i) d |= (uint8_t)(a[i] ^ b[i]);
    return d == 0;
}

// En-tête Host = adresse du point d'accès (avec ou sans « :80 ») : refuse le DNS rebinding et les noms captifs
static bool hostIsPortal(const String& host, const IPAddress& ip) {
    const String a = ip.toString();
    return host == a || host == a + ":80";
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

// En-tête commun (kWebHead) : %LANG% remplacé par la langue courante
static String webHead() {
    String h = FPSTR(kWebHead);
    h.replace("%LANG%", wr::langCode(wr::lang()));
    return h;
}

static String formPage(const Settings& s, const std::vector<Net>& nets, const String& error) {
    String h;
    h.reserve(9000);
    h += webHead();
    h += F("<title>");
    h += wr::L("Tab5 setup", "Configuration du Tab5");
    h += F("</title></head><body><header><div class=logo><i></i></div><div><h1>");
    h += wr::L("WiiM remote", "T\xC3\xA9l\xC3\xA9" "commande WiiM");
    h += F("</h1><p>");
    h += wr::L("Tab5 setup", "Configuration du Tab5");
    h += F("</p></div></header>");
    if (error.length()) h += "<p class=err>" + esc(error) + "</p>";
    h += F("<form method=post action=/save id=f><input type=hidden name=t value=");
    h += g_formToken;  // hexadécimal : pas besoin d'échappement
    h += F(">");

    // --- Langue : changer la liste recharge la page dans cette langue (/?lang=xx). Les champs déjà tapés
    // sont perdus, mais la liste est en tête du formulaire, donc choisie avant de taper : plus simple et plus
    // sûr que de recopier les champs (le mot de passe ne doit jamais revenir dans l'adresse).
    h += F("<section class=card><label for=lang>Language / Langue</label><select id=lang name=lang>");
    h += option("en", "English", wr::lang() == wr::Lang::En);
    h += option("fr", "Fran\xC3\xA7" "ais", wr::lang() == wr::Lang::Fr);
    h += F("</select></section>");

    // --- 1. Wi-Fi
    h += F("<section class=card><h2><b>1</b>");
    h += wr::L("Your Wi-Fi", "Ton Wi-Fi");
    h += F("</h2>");
    if (!nets.empty()) {
        h += F("<div class=nets>");
        for (auto& n : nets) {
            h += "<button type=button class=net data-s=\"" + esc(n.name) + "\"><span>" + esc(n.name) +
                 (n.open ? "" : " &#128274;") + "</span>" + bars(n.rssi) + "</button>";
        }
        h += F("</div>");
    }
    h += F("<label for=ssid>");
    h += wr::L("Network name", "Nom du r\xC3\xA9seau");
    h += F("</label><input id=ssid name=ssid required maxlength=32 autocomplete=off placeholder='");
    h += wr::L("Tap a network or type its name", "Touche un r\xC3\xA9seau ou tape son nom");
    h += F("' value=\"");
    h += esc(s.ssid);
    h += F("\"><small>");
    h += wr::L("Your network is not listed? The Tab5 only uses 2.4 GHz.",
               "Ton r\xC3\xA9seau n'appara\xC3\xAEt pas ? Le Tab5 n'utilise que le 2,4 GHz.");
    h += F("</small><label for=pass>");
    h += wr::L("Password", "Mot de passe");
    h += F("</label><div class=pw><input id=pass name=pass type=password maxlength=63 autocomplete=off");
    if (s.pass.length()) {
        h += F(" placeholder='");
        h += wr::L("Unchanged if empty", "Inchang\xC3\xA9 si vide");
        h += F("'");
    }
    h += F("><button type=button id=eye>");
    h += wr::L("Show", "Afficher");
    h += F("</button></div><small>");
    h += wr::L("On Android: <b>Settings \xE2\x80\xBA Wi-Fi \xE2\x80\xBA your network \xE2\x80\xBA Share</b> "
               "shows the password.",
               "Sur Android : <b>Param\xC3\xA8tres \xE2\x80\xBA Wi-Fi \xE2\x80\xBA ton r\xC3\xA9seau \xE2\x80\xBA Partager</b> "
               "affiche le mot de passe.");
    h += F("</small></section>");

    // --- 2. WiiM
    h += F("<section class=card><h2><b>2</b>");
    h += wr::L("Your WiiM", "Ton WiiM");
    h += F("</h2><label for=host>");
    h += wr::L("IP address (optional)", "Adresse IP (facultatif)");
    h += F("</label><input id=host name=host maxlength=64 inputmode=decimal autocomplete=off placeholder='");
    h += wr::L("Empty: automatic search", "Vide : recherche automatique");
    h += F("' value=\"");
    h += esc(s.host);
    h += F("\"><small>");
    h += wr::L("<b>Leave empty</b>: the Tab5 finds your WiiM on the Wi-Fi, and finds it again if its address "
               "changes.",
               "<b>Laisse vide</b> : le Tab5 trouve ton WiiM sur le Wi-Fi, et le retrouve si "
               "son adresse change.");
    h += F("</small></section>");

    // --- Presets
    h += F("<details class=card><summary>Presets</summary><div class=in>"
           "<textarea name=presets maxlength=1200 placeholder='");
    h += wr::L("Empty: presets of the WiiM Home app", "Vide : presets de l&#39;app WiiM Home");
    h += F("'>");
    h += esc(s.presetsText);
    h += F("</textarea><small>");
    h += wr::L("<b>Leave empty</b> to use the favorites of the WiiM Home app, with their images. Otherwise, one "
               "line per button: <code>1=Radio Swiss Jazz</code> (12 at most).",
               "<b>Laisse vide</b> pour reprendre les favoris de l'app WiiM Home, "
               "avec leurs images. Sinon, une ligne par bouton : <code>1=Radio Swiss Jazz</code> "
               "(12 au plus).");
    h += F("</small></div></details>");

    // --- Options
    const char* on = wr::L("On", "Activ\xC3\xA9");
    const char* off = wr::L("Off", "D\xC3\xA9sactiv\xC3\xA9");
    h += F("<details class=card><summary>Options</summary><div class=in><div class=row><div><label>");
    h += wr::L("Orientation", "Orientation");
    h += F("</label><select name=rotation>");
    h += option("1", wr::L("Landscape", "Paysage"), s.rotation == 1);
    h += option("3", wr::L("Landscape, upside down", "Paysage invers\xC3\xA9"), s.rotation == 3);
    h += option("0", wr::L("Portrait", "Portrait"), s.rotation == 0);
    h += option("2", wr::L("Portrait, upside down", "Portrait invers\xC3\xA9"), s.rotation == 2);
    h += F("</select></div><div><label>");
    h += wr::L("Volume step", "Pas du volume");
    h += F("</label><input name=volstep type=number min=1 max=20 value=");
    h += String(s.volStep);
    h += F("></div></div><div class=row><div><label>");
    h += wr::L("Beeps", "Bips");
    h += F("</label><select name=beep>");
    h += option("0", wr::L("Off", "D\xC3\xA9sactiv\xC3\xA9s"), s.beep == 0);
    h += option("1", wr::L("Quiet", "Discrets"), s.beep == 1);
    h += option("2", wr::L("Normal", "Normaux"), s.beep == 2);
    h += option("3", wr::L("Loud", "Forts"), s.beep == 3);
    h += F("</select></div><div><label>");
    h += wr::L("Connection to the WiiM", "Connexion au WiiM");
    h += F("</label><select name=scheme>");
    h += option("https", "HTTPS", s.https);
    h += option("http", "HTTP", !s.https);
    h += F("</select></div></div><div class=row><div><label>");
    h += wr::L("Wake on motion", "R\xC3\xA9veil au mouvement");
    h += F("</label><select name=motion>");
    h += option("1", on, s.motionWake);
    h += option("0", off, !s.motionWake);
    h += F("</select></div><div><label>");
    h += wr::L("Visualizer", "Visualiseur");
    h += F("</label><select name=visu>");
    h += option("1", on, s.visualizer);
    h += option("0", off, !s.visualizer);
    h += F("</select></div></div><div class=row><div><label>");
    h += wr::L("Sleep", "Mise en veille");
    h += F("</label><select name=sleep>");
    h += option("0", wr::L("Never", "Jamais"), s.sleepMin == 0);
    h += option("30", wr::L("After 30 min", "Apr\xC3\xA8s 30 min"), s.sleepMin == 30);
    h += option("60", wr::L("After 1 h", "Apr\xC3\xA8s 1 h"), s.sleepMin == 60);
    h += option("120", wr::L("After 2 h", "Apr\xC3\xA8s 2 h"), s.sleepMin == 120);
    h += F("</select></div><div><label>");
    h += wr::L("Tile order", "Ordre des tuiles");
    h += F("</label><select name=tileorder>");
    h += option("recent", wr::L("Last played first", "Derni\xC3\xA8res lanc\xC3\xA9" "es d'abord"), s.recentFirst);
    h += option("fixed", wr::L("As saved", "Comme enregistr\xC3\xA9" "es"), !s.recentFirst);
    h += F("</select></div></div><small>");
    h += wr::L("Sleep: with no tap and no music, the Tab5 turns off; the power button turns it on again. HTTPS "
               "is recommended. Visualizer (experimental): in the ambient view, the microphone animates a "
               "spectrum. The sound is analyzed on the device, never recorded or sent. Tile order: pinned tiles "
               "first (hold a tile to pin it), then the last played.",
               "Mise en veille : sans appui et sans musique, le Tab5 s'\xC3\xA9teint ; le "
               "bouton d'alimentation le rallume. HTTPS est conseill\xC3\xA9. Visualiseur (exp\xC3\xA9rimental) : en vue ambiance, le micro anime un spectre. Le son est "
               "analys\xC3\xA9 sur place, jamais enregistr\xC3\xA9 ni envoy\xC3\xA9. Ordre des tuiles : les tuiles "
               "\xC3\xA9pingl\xC3\xA9" "es d'abord (doigt tenu sur une tuile pour l'\xC3\xA9pingler), puis les derni\xC3\xA8res lanc\xC3\xA9" "es.");
    h += F("</small></div></details>");

    // --- Dictée (recherche)
    h += F("<details class=card><summary>");
    h += wr::L("Search dictation", "Dict\xC3\xA9" "e de la recherche");
    h += F("</summary><div class=in><label for=oaikey>");
    h += wr::L("OpenAI API key", "Cl\xC3\xA9 API OpenAI");
    h += F("</label><div class=pw><input id=oaikey name=oaikey type=password maxlength=200 autocomplete=off "
           "placeholder='");
    h += s.openaiKey.length() ? wr::L("Saved \xE2\x80\x94 unchanged if empty",
                                      "Enregistr\xC3\xA9" "e \xE2\x80\x94 inchang\xC3\xA9" "e si vide")
                              : "sk-\xE2\x80\xA6";
    h += F("'></div><small>");
    h += wr::L("Microphone button in the search: say an artist or a title. Create the key on "
               "platform.openai.com (a few cents per month). The sound is sent only while listening. Empty: no "
               "microphone button.",
               "Bouton micro dans la recherche : dis un artiste ou un titre. "
               "Cl\xC3\xA9 \xC3\xA0 cr\xC3\xA9" "er sur platform.openai.com (quelques centimes par mois). Le son n'est "
               "envoy\xC3\xA9 que pendant l'\xC3\xA9" "coute. Vide : pas de bouton micro.");
    h += F("</small>");
    if (s.openaiKey.length()) {
        h += F("<label class=chk><input type=checkbox name=oaidel value=1>");
        h += wr::L("Delete the saved key", "Supprimer la cl\xC3\xA9 enregistr\xC3\xA9" "e");
        h += F("</label>");
    }
    h += F("<label for=sttmodel>");
    h += wr::L("Transcription model", "Mod\xC3\xA8le de transcription");
    h += F("</label><input id=sttmodel name=sttmodel maxlength=60 value=\"");
    h += esc(s.sttModel);
    h += F("\"><small>");
    h += wr::L("Change it only if OpenAI renames this model.",
               "\xC3\x80 changer seulement si OpenAI renomme ce mod\xC3\xA8le.");
    h += F("</small></div></details>");

    // --- Deezer : favoris d'un profil public (api.deezer.com/user/<numéro>, sans clé)
    h += F("<details class=card><summary>");
    h += wr::L("Deezer favorites", "Favoris Deezer");
    h += F("</summary><div class=in><label for=deezer>");
    h += wr::L("Deezer profile", "Profil Deezer");
    h += F("</label><input id=deezer name=deezer maxlength=120 inputmode=url placeholder='");
    h += wr::L("123456789 or https://www.deezer.com/en/profile/123456789",
               "123456789 ou https://www.deezer.com/fr/profile/123456789");
    h += F("' value=\"");
    h += esc(s.deezerUser);
    h += F("\"><small>");
    h += wr::L("On deezer.com, open your profile: its number is at the end of the address. Paste the number or "
               "the address. Your favorite albums and tracks then show on the Tab5 (the profile must be public). "
               "Empty: no Deezer favorites.",
               "Sur deezer.com, ouvre ton profil : son num\xC3\xA9ro est \xC3\xA0 la fin de l'adresse. Colle le "
               "num\xC3\xA9ro ou l'adresse. Tes albums et morceaux favoris s'affichent alors sur le Tab5 (le profil doit "
               "\xC3\xAAtre public). Vide : pas de favoris Deezer.");
    h += F("</small></div></details>");

    // --- Avancé
    h += F("<details class=card><summary>");
    h += wr::L("Advanced", "Avanc\xC3\xA9");
    h += F("</summary><div class=in>");
    if (s.fingerprint.length()) {
        h += F("<label class=chk><input type=checkbox name=forgetfp value=1>");
        h += wr::L("Forget the saved WiiM certificate", "Oublier le certificat enregistr\xC3\xA9 du WiiM");
        h += F("</label><small>");
        h += wr::L("Fingerprint: <code>", "Empreinte : <code>");
        h += esc(s.fingerprint.substring(0, 16));
        h += F("\xE2\x80\xA6</code>. ");
        h += wr::L("Only after a reset or a change of WiiM.",
                   "Seulement apr\xC3\xA8s une r\xC3\xA9initialisation ou un changement de WiiM.");
        h += F("</small>");
    }
    // Recherche Qobuz directe : non officielle, désactivée par défaut, expliquée telle quelle
    h += F("<label class=chk><input type=checkbox name=qobuzdirect value=1");
    if (s.qobuzDirect) h += F(" checked");
    h += F(">");
    h += wr::L("Direct Qobuz search (unofficial)", "Recherche Qobuz directe (non officielle)");
    h += F("</label><small>");
    h += wr::L("Faster search, no music interruption, and album search. The Tab5 then asks Qobuz with the Qobuz "
               "access that your WiiM shares on your network: this is not an official Qobuz API. Off (default): "
               "your WiiM searches itself, and the music stops a few seconds during a search.",
               "Recherche plus rapide, sans couper la musique, et recherche d'albums. Le Tab5 interroge alors Qobuz "
               "avec l'acc\xC3\xA8s Qobuz que ton WiiM partage sur ton r\xC3\xA9seau : ce n'est pas une API "
               "officielle de Qobuz. D\xC3\xA9sactiv\xC3\xA9" "e (par d\xC3\xA9" "faut) : ton WiiM cherche "
               "lui-m\xC3\xAAme, et la musique s'arr\xC3\xAAte quelques secondes pendant une recherche.");
    h += F("</small>");
    h += F("<label class=chk><input type=checkbox name=wipe value=1 id=wipe>");
    h += wr::L("Erase everything: settings, OpenAI key and favorites",
               "Tout effacer : r\xC3\xA9glages, cl\xC3\xA9 OpenAI et favoris");
    h += F("</label><small>");
    h += wr::L("Resets the Tab5 (as new). The other fields are then ignored.",
               "Remet le Tab5 \xC3\xA0 z\xC3\xA9ro (comme neuf). Les autres champs sont alors ignor\xC3\xA9s.");
    h += F("</small></div></details>");

    // Textes du script : chaînes JS entre apostrophes, donc sans apostrophe dans les traductions
    h += F("<div class=bar><button class=btn type=submit id=go>");
    h += wr::L("Save and restart", "Enregistrer et red\xC3\xA9marrer");
    h += F("</button></div></form><script>var T={show:'");
    h += wr::L("Show", "Afficher");
    h += F("',hide:'");
    h += wr::L("Hide", "Masquer");
    h += F("',wipe:'");
    h += wr::L("Erase everything? Settings and favorites will be lost.",
               "Tout effacer ? R\xC3\xA9glages et favoris seront perdus.");
    h += F("',saving:'");
    h += wr::L("Saving\xE2\x80\xA6", "Enregistrement\xE2\x80\xA6");
    h += F("'};"
           "var s=document.getElementById('ssid'),p=document.getElementById('pass');"
           "document.getElementById('lang').onchange=function(){location.href='/?lang='+this.value};"
           "function mark(){document.querySelectorAll('.net').forEach(function(b){"
           "b.classList.toggle('on',b.dataset.s===s.value)})}"
           "document.querySelectorAll('.net').forEach(function(b){b.onclick=function(){s.value=b.dataset.s;mark();p.focus()}});"
           "s.oninput=mark;mark();"
           "document.getElementById('eye').onclick=function(){var t=p.type=='password';p.type=t?'text':'password';"
           "this.textContent=t?T.hide:T.show};"
           "document.getElementById('f').onsubmit=function(){var w=document.getElementById('wipe');"
           "if(w.checked&&!confirm(T.wipe))return false;"
           "var g=document.getElementById('go');"
           "g.disabled=true;g.textContent=T.saving};"
           "</script></body></html>");
    return h;
}

static String donePage(const String& ssid) {
    String h = webHead();
    h += F("<title>");
    h += wr::L("Saved", "Enregistr\xC3\xA9");
    h += F("</title></head><body><header><div class=logo><i></i></div><div><h1>");
    h += wr::L("Saved", "C'est enregistr\xC3\xA9");
    h += F("</h1><p>");
    h += wr::L("WiiM remote", "T\xC3\xA9l\xC3\xA9" "commande WiiM");
    h += F("</p></div></header><div class='card okbox'>");
    h += wr::L("The Tab5 restarts and connects to <b>", "Le Tab5 red\xC3\xA9marre et se connecte \xC3\xA0 <b>");
    h += esc(ssid);
    h += F("</b>.</div><section class=card><h2>");
    h += wr::L("What next?", "Et ensuite ?");
    h += F("</h2><small>");
    h += wr::L("Your phone will leave the Wi-Fi of the Tab5: you can close this page. If there is only one WiiM "
               "on your network, it is selected automatically; otherwise, tap yours on the Tab5 screen.",
               "Ton t\xC3\xA9l\xC3\xA9phone va "
               "quitter le Wi-Fi du Tab5 : tu peux fermer cette page. Si un seul WiiM est sur ton r\xC3\xA9seau, "
               "il est choisi tout seul ; sinon, touche le tien sur l'\xC3\xA9" "cran du Tab5.");
    h += F("</small></section></body></html>");
    return h;
}

static String wipedPage() {
    String h = webHead();
    h += F("<title>");
    h += wr::L("Erased", "Effac\xC3\xA9");
    h += F("</title></head><body><header><div class=logo><i></i></div><div><h1>");
    h += wr::L("Everything is erased", "Tout est effac\xC3\xA9");
    h += F("</h1><p>");
    h += wr::L("WiiM remote", "T\xC3\xA9l\xC3\xA9" "commande WiiM");
    h += F("</p></div></header><div class='card okbox'>");
    h += wr::L("The Tab5 restarts in setup mode, as new.",
               "Le Tab5 red\xC3\xA9marre en mode configuration, comme neuf.");
    h += F("</div><section class=card><small>");
    h += wr::L("Connect again to its Wi-Fi (new password shown on the screen) to set it up.",
               "Reconnecte-toi \xC3\xA0 son Wi-Fi (nouveau mot de passe affich\xC3\xA9 "
               "sur l'\xC3\xA9" "cran) pour le configurer.");
    h += F("</small></section></body></html>");
    return h;
}

void runSetupPortal(Settings& s, Ui& ui, const String& reason) {
    ui.showInfo(wr::L("Setup", "Configuration"),
                wr::L("Preparing: searching for Wi-Fi networks\xE2\x80\xA6",
                      "Pr\xC3\xA9paration : recherche des r\xC3\xA9seaux Wi-Fi\xE2\x80\xA6"));
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
    g_formToken = randomToken();
    static const char* kHeaders[] = {"Host"};
    server.collectHeaders(kHeaders, 1);  // remis à zéro à chaque requête (hostHeader() garde l'ancienne valeur)

    server.on("/", HTTP_GET, [&] {
        if (!hostIsPortal(server.header("Host"), ip)) {  // autre nom (portail captif) : vers l'adresse du Tab5
            server.sendHeader("Location", url + "/", true);
            server.send(302, "text/plain", "");
            return;
        }
        // /?lang=fr ou /?lang=en : la page change de langue tout de suite (choix enregistré seulement avec le formulaire)
        if (server.hasArg("lang")) wr::setLang(wr::langFromCode(server.arg("lang").c_str()));
        server.send(200, "text/html; charset=utf-8", formPage(s, ssids, error));
    });

    server.on("/save", HTTP_POST, [&] {
        // Seulement depuis la page du portail : bonne adresse et jeton du formulaire
        if (!hostIsPortal(server.header("Host"), ip) || !sameToken(server.arg("t"), g_formToken)) {
            server.send(403, "text/plain; charset=utf-8",
                        wr::L("Refused: open the setup page again at the Tab5 address and try again.",
                              "Refus\xC3\xA9 : rouvre la page de configuration \xC3\xA0 l'adresse du Tab5 et r\xC3\xA9" "essaie."));
            return;
        }
        if (server.hasArg("wipe")) {
            // Remise à zéro : NVS + LittleFS (favoris, copie des réglages), puis portail au redémarrage
            eraseSettings();
            fav::eraseAll();
            server.send(200, "text/html; charset=utf-8", wipedPage());
            delay(1500);
            ESP.restart();
            return;
        }
        Settings next = s;
        next.lang = wr::langCode(wr::langFromCode(server.arg("lang").c_str()));
        wr::setLang(wr::langFromCode(next.lang.c_str()));  // messages et page de fin dans la langue choisie
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
        next.qobuzDirect = server.hasArg("qobuzdirect");  // case non cochée : absente du formulaire
        next.sleepMin = constrain(server.arg("sleep").toInt(), 0, 240);
        next.recentFirst = server.arg("tileorder") != "fixed";
        String key = server.arg("oaikey");
        key.trim();
        if (key.length()) next.openaiKey = key;
        if (server.hasArg("oaidel")) next.openaiKey = "";
        const String deezerIn = server.arg("deezer");
        next.deezerUser = wr::deezerUserId(deezerIn.c_str()).c_str();
        next.sttModel = server.arg("sttmodel");
        next.sttModel.trim();
        if (!next.sttModel.length()) next.sttModel = "gpt-4o-mini-transcribe";
        bool https = server.arg("scheme") != "http";
        if (https != s.https || next.host != s.host || server.hasArg("forgetfp")) next.fingerprint = "";
        if (next.host != s.host) next.wiimUuid = next.wiimName = "";  // autre WiiM : identité à relire
        next.https = https;

        std::string perr;
        String err;
        // Bornes côté serveur (les maxlength du formulaire ne protègent de rien)
        if (next.presetsText.length() > 1200)
            err = wr::L("Presets: the text is too long (1200 characters at most).",
                        "Presets : le texte est trop long (1200 caract\xC3\xA8res au plus).");
        else if (server.arg("pass").length() > 63 || server.arg("oaikey").length() > 200 || next.host.length() > 64 ||
                 deezerIn.length() > 120 || server.arg("sttmodel").length() > 60)
            err = wr::L("A field is too long.", "Un champ est trop long.");
        else if (next.ssid.length() == 0 || next.ssid.length() > 32) err = wr::L("Invalid Wi-Fi network name.", "Nom de réseau Wi-Fi invalide.");
        else if (next.pass.length() && (next.pass.length() < 8 || next.pass.length() > 63))
            err = wr::L("The Wi-Fi password must have 8 to 63 characters.",
                        "Le mot de passe Wi-Fi doit faire entre 8 et 63 caractères.");
        else if (next.host.length() && !wr::isValidHost(next.host.c_str())) err = wr::L("Invalid WiiM address (example: 192.168.1.50).",
                        "Adresse du WiiM invalide (exemple : 192.168.1.50).");
        else if (next.openaiKey.length() && !tokenOk(next.openaiKey, 20, 200))
            err = wr::L("Invalid OpenAI key: paste it with no space and no line break.",
                        "Clé OpenAI invalide : colle-la sans espace ni retour à la ligne.");
        else if (!tokenOk(next.sttModel, 1, 60)) err = wr::L("Invalid transcription model name.", "Nom de modèle de transcription invalide.");
        else if (deezerIn.length() && !next.deezerUser.length())
            err = wr::L("Deezer profile: paste the number or the address deezer.com/…/profile/<number> (not a short link).",
                        "Profil Deezer : colle le numéro ou l'adresse deezer.com/…/profile/<numéro> (pas un lien court).");
        else if (!presetsTextIsAuto(next.presetsText) &&
                 !wr::parsePresets(next.presetsText.c_str(), next.presets, perr))
            err = String(wr::L("Presets: ", "Presets : ")) + perr.c_str();

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
                  canCancel ? reason + wr::L(" Tap the Tab5 screen to cancel.", " Touche l'\xC3\xA9" "cran du Tab5 pour annuler.")
                            : reason);

    const uint32_t shownAt = millis();
    for (;;) {
        dns.processNextRequest();
        server.handleClient();
        M5.update();
        if (canCancel && millis() - shownAt > 1500 && M5.Touch.getDetail().wasPressed()) {
            ui.showInfo(wr::L("Setup", "Configuration"),
                        wr::L("Cancelled: restarting\xE2\x80\xA6", "Annul\xC3\xA9" "e : red\xC3\xA9marrage\xE2\x80\xA6"));
            delay(400);
            ESP.restart();
        }
        delay(2);
    }
}
