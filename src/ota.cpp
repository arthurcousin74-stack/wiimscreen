#include "ota.h"

#include <M5Unified.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_random.h>

#include "firmware.h"
#include "i18n.h"
#include "version.h"
#include "web_style.h"

// Corps de la page (après kWebHead, feuille de style commune). Les textes sont des repères %T_xxx%,
// remplacés à l'envoi par la langue courante (kTexts) ; ceux du script sont entre apostrophes JS.
static const char kPage[] PROGMEM = R"HTML(<title>%T_TITLE%</title><style>
#code{font:600 30px ui-monospace,Menlo,monospace;letter-spacing:.35em;text-align:center;padding:14px}
.file{display:flex;align-items:center;gap:12px;background:#111317;border:1px dashed var(--line);border-radius:12px;padding:16px;cursor:pointer;color:var(--muted);font-weight:500}
.file b{color:var(--text)}.file input{display:none}.file.ok{border-style:solid;border-color:var(--accent)}
#m{margin-top:14px}#m:empty{display:none}.btn{margin-top:18px}</style></head><body>
<header><div class=logo><i></i></div><div><h1>%T_H1%</h1><p>%T_SUB% · version <b>%VERSION%</b></p></div></header>
<form id=f class=card><label for=code>%T_CODE%</label>
<input id=code name=code inputmode=numeric autocomplete=one-time-code maxlength=6 pattern='[0-9]{6}' placeholder='······' required>
<label>Firmware</label><label class=file id=z><input type=file name=fw id=fw required>
<span id=n>%T_CHOOSE% <b>wiim-remote-tab5.bin</b></span></label>
<small>%T_HELP%</small>
<button class=btn id=go>%T_INSTALL%</button>
<progress id=p max=100 value=0 hidden></progress><div id=m></div></form>
<script>
var f=document.getElementById('f'),fw=document.getElementById('fw'),p=document.getElementById('p'),
m=document.getElementById('m'),go=document.getElementById('go');
function say(t,c){m.textContent=t;m.className=c||''}
fw.onchange=function(){var x=fw.files[0];if(!x)return;document.getElementById('n').innerHTML='';
var b=document.createElement('b');b.textContent=x.name;document.getElementById('n').append(b,' · '+Math.round(x.size/1024)+' %T_KB%');
document.getElementById('z').classList.add('ok')};
f.onsubmit=function(e){e.preventDefault();var x=new XMLHttpRequest(),d=new FormData();
x.open('POST','/update?code='+encodeURIComponent(f.code.value));
x.upload.onprogress=function(v){p.hidden=false;p.value=v.loaded*100/v.total;
go.textContent=v.loaded<v.total?'%T_SENDING% '+Math.round(p.value)+' %':'%T_INSTALLING%'};
x.onload=function(){go.disabled=false;go.textContent='%T_INSTALL%';
if(x.status==200){say(x.responseText,'okbox');go.hidden=true;p.value=100}else{say(x.responseText,'err');p.hidden=true}};
x.onerror=function(){go.disabled=false;go.textContent='%T_INSTALL%';say('%T_LOST%','err')};
d.append('fw',fw.files[0]);go.disabled=true;go.textContent='%T_SENDING%';say('');x.send(d)};
</script></body></html>)HTML";

// Textes de la page : repère, anglais, français (sans apostrophe dans ceux qui vont dans les chaînes JS du script)
struct PageText {
    const char* key;
    const char* en;
    const char* fr;
};
static const PageText kTexts[] = {
    {"%T_TITLE%", "Tab5 update", "Mise \xC3\xA0 jour du Tab5"},
    {"%T_H1%", "Update", "Mise \xC3\xA0 jour"},
    {"%T_SUB%", "WiiM remote", "T\xC3\xA9l\xC3\xA9" "commande WiiM"},
    {"%T_CODE%", "Code shown on the Tab5", "Code affich\xC3\xA9 sur le Tab5"},
    {"%T_CHOOSE%", "Choose", "Choisir"},
    {"%T_HELP%", "The file of the latest release on GitHub (unzipped if it is in a .zip).",
     "Le fichier de la derni\xC3\xA8re version sur GitHub (d\xC3\xA9zipp\xC3\xA9 s'il est dans un .zip)."},
    {"%T_INSTALL%", "Install", "Installer"},
    {"%T_KB%", "KB", "Ko"},
    {"%T_SENDING%", "Sending\xE2\x80\xA6", "Envoi\xE2\x80\xA6"},
    {"%T_INSTALLING%", "Installing\xE2\x80\xA6", "Installation\xE2\x80\xA6"},
    {"%T_LOST%", "Connection lost: try again.", "Connexion perdue : r\xC3\xA9" "essaie."},
};

static String updatePage() {
    String page = FPSTR(kWebHead);
    page.replace("%LANG%", wr::langCode(wr::lang()));
    page += FPSTR(kPage);
    page.replace("%VERSION%", FW_VERSION);
    for (const auto& t : kTexts) page.replace(t.key, wr::L(t.en, t.fr));
    return page;
}

void runUpdateMode(Ui& ui) {
    if (WiFi.status() != WL_CONNECTED) {
        ui.showInfo(wr::L("Update", "Mise \xC3\xA0 jour"),
                    wr::L("The Wi-Fi is not connected: cannot receive a firmware.",
                          "Le Wi-Fi n'est pas connect\xC3\xA9 : impossible de recevoir un firmware."),
                    wr::L("Tap the screen to go back", "Touche l'\xC3\xA9" "cran pour revenir"));
        while (!M5.Touch.getDetail().wasPressed()) M5.update(), delay(20);
        return;
    }
    char codeBuf[8];
    snprintf(codeBuf, sizeof codeBuf, "%06lu", (unsigned long)(esp_random() % 1000000UL));
    const String code = codeBuf;
    const String url = "http://" + WiFi.localIP().toString();

    WebServer server(80);
    int wrongCodes = 0;
    bool accepted = false;  // code juste pour le transfert en cours
    bool done = false;
    String result;
    int lastPercent = -2;
    wr::FirmwareStream stream;
    size_t expected = 0;
    uint32_t lastDraw = 0;

    auto draw = [&](int percent, const String& status) {
        ui.showUpdate(url, code, FW_VERSION, percent, status);
        lastPercent = percent;
        lastDraw = millis();
    };

    server.on("/", HTTP_GET, [&] {
        server.send(200, "text/html; charset=utf-8", updatePage());
    });
    server.on(
        "/update", HTTP_POST,
        // Fin de la requête : réponse au navigateur
        [&] {
            if (!accepted) {
                server.send(403, "text/plain; charset=utf-8",
                            wrongCodes >= 5 ? wr::L("Wrong code: too many attempts, start again from the Tab5.",
                                                    "Code incorrect : trop d'essais, recommence depuis le Tab5.")
                                            : wr::L("Wrong code.", "Code incorrect."));
                return;
            }
            server.send(done ? 200 : 400, "text/plain; charset=utf-8", result);
        },
        // Morceaux du fichier
        [&] {
            HTTPUpload& up = server.upload();
            if (up.status == UPLOAD_FILE_START) {
                accepted = server.arg("code") == code && wrongCodes < 5;
                done = false;
                result = "";
                stream = wr::FirmwareStream();
                expected = server.clientContentLength() > 0 ? server.clientContentLength() : 0;
                if (!accepted) {
                    ++wrongCodes;
                    draw(-1, String(wr::L("Wrong code received (", "Code incorrect re\xC3\xA7u (")) + String(wrongCodes) +
                                 "/5)");
                    return;
                }
                draw(0, wr::L("Receiving the firmware\xE2\x80\xA6", "R\xC3\xA9" "ception du firmware\xE2\x80\xA6"));
            } else if (up.status == UPLOAD_FILE_WRITE && accepted && result.length() == 0) {
                const uint8_t* out;
                size_t n = stream.feed(up.buf, up.currentSize, &out);
                if (stream.kind() == wr::FirmwareStream::Invalid) {
                    if (Update.isRunning()) Update.abort();
                    result = wr::L("This file is not a Tab5 firmware (did you unzip the GitHub download?)",
                                   "Ce fichier n'est pas un firmware du Tab5 (as-tu bien d\xC3\xA9zipp\xC3\xA9 le "
                                   "t\xC3\xA9l\xC3\xA9" "chargement de GitHub ?)");
                    return;
                }
                if (n && !Update.isRunning() && !Update.begin(UPDATE_SIZE_UNKNOWN)) {
                    result = String(wr::L("Cannot prepare the update: ",
                                          "Impossible de pr\xC3\xA9parer la mise \xC3\xA0 jour : ")) +
                             Update.errorString();
                    return;
                }
                if (n && Update.write(const_cast<uint8_t*>(out), n) != n) {
                    result = String(wr::L("Write failed: ", "\xC3\x89" "criture impossible : ")) + Update.errorString();
                    Update.abort();
                    return;
                }
                int pct = expected ? (int)(stream.received() * 100 / expected) : -1;
                if (pct != lastPercent && millis() - lastDraw > 250)
                    draw(pct, wr::L("Receiving the firmware\xE2\x80\xA6", "R\xC3\xA9" "ception du firmware\xE2\x80\xA6"));
            } else if (up.status == UPLOAD_FILE_END && accepted && result.length() == 0) {
                // end(true) vérifie l'image (somme de contrôle, puce) avant de la rendre active
                if (stream.written() > 0 && Update.end(true)) {
                    done = true;
                    result = wr::L("Firmware installed: the Tab5 restarts.", "Firmware install\xC3\xA9 : le Tab5 red\xC3\xA9marre.");
                } else {
                    result = String(wr::L("Firmware rejected: ", "Firmware refus\xC3\xA9 : ")) +
                             (stream.written() ? Update.errorString()
                                               : wr::L("empty or incomplete file", "fichier vide ou incomplet"));
                    if (Update.isRunning()) Update.abort();
                }
            } else if (up.status == UPLOAD_FILE_ABORTED) {
                if (Update.isRunning()) Update.abort();
                result = wr::L("Transfer interrupted", "Transfert interrompu");
            }
        });
    server.begin();
    draw(-1, "");

    const uint32_t start = millis();
    for (;;) {
        server.handleClient();
        M5.update();
        auto t = M5.Touch.getDetail();
        if (done) {
            draw(100, result);
            delay(1500);  // laisse partir la réponse au navigateur
            ESP.restart();
        }
        if (result.length() && lastPercent != -1 && millis() - lastDraw > 400) draw(-1, result);
        if ((t.wasPressed() && ui.hitUpdateCancel(t.x, t.y)) || wrongCodes >= 5 || millis() - start > 10UL * 60 * 1000)
            break;
        delay(2);
    }
    if (Update.isRunning()) Update.abort();
    server.stop();
}
