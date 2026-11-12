#include "ota.h"

#include <M5Unified.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_random.h>

#include "firmware.h"
#include "version.h"

static const char kPage[] PROGMEM = R"HTML(<!doctype html><html lang=fr><head><meta charset=utf-8>
<meta name=viewport content='width=device-width,initial-scale=1'><title>Mise à jour du Tab5</title><style>
body{font-family:system-ui,sans-serif;max-width:520px;margin:0 auto;padding:20px;color:#111}
label{display:block;margin-top:18px;font-weight:600}input{width:100%;box-sizing:border-box;font-size:18px;
padding:10px;margin-top:6px;border:1px solid #999;border-radius:8px}button{margin-top:24px;width:100%;
padding:14px;font-size:18px;border:0;border-radius:8px;background:#111;color:#fff}progress{width:100%;
height:18px;margin-top:20px}small{color:#555}#m{font-size:18px;margin-top:16px}</style></head><body>
<h1>Mise à jour du Tab5</h1><p>Version installée : <b>%VERSION%</b></p>
<form id=f><label>Code affiché sur le Tab5<input name=code inputmode=numeric autocomplete=off maxlength=6 required></label>
<label>Firmware<input type=file name=fw accept=.bin required><small>Le fichier <b>wiim-remote-tab5.bin</b>
téléchargé depuis GitHub (dézippé).</small></label><button>Installer</button></form>
<progress id=p max=100 value=0 hidden></progress><p id=m></p>
<script>
f.onsubmit=e=>{e.preventDefault();const x=new XMLHttpRequest(),d=new FormData();
x.open('POST','/update?code='+encodeURIComponent(f.code.value));
x.upload.onprogress=v=>{p.hidden=false;p.value=v.loaded*100/v.total};
x.onload=()=>{m.textContent=x.responseText;if(x.status==200)f.hidden=true};
x.onerror=()=>{m.textContent='Connexion perdue : réessaie.'};
d.append('fw',f.fw.files[0]);m.textContent='Envoi…';x.send(d)};
</script></body></html>)HTML";

void runUpdateMode(Ui& ui) {
    if (WiFi.status() != WL_CONNECTED) {
        ui.showInfo("Mise \xC3\xA0 jour", "Le Wi-Fi n'est pas connect\xC3\xA9 : impossible de recevoir un firmware.",
                    "Touche l'\xC3\xA9" "cran pour revenir");
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
        String page = FPSTR(kPage);
        page.replace("%VERSION%", FW_VERSION);
        server.send(200, "text/html; charset=utf-8", page);
    });
    server.on(
        "/update", HTTP_POST,
        // Fin de la requête : réponse au navigateur
        [&] {
            if (!accepted) {
                server.send(403, "text/plain; charset=utf-8",
                            wrongCodes >= 5 ? "Code incorrect : trop d'essais, recommence depuis le Tab5."
                                            : "Code incorrect.");
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
                    draw(-1, "Code incorrect re\xC3\xA7u (" + String(wrongCodes) + "/5)");
                    return;
                }
                draw(0, "R\xC3\xA9" "ception du firmware\xE2\x80\xA6");
            } else if (up.status == UPLOAD_FILE_WRITE && accepted && result.length() == 0) {
                const uint8_t* out;
                size_t n = stream.feed(up.buf, up.currentSize, &out);
                if (stream.kind() == wr::FirmwareStream::Invalid) {
                    if (Update.isRunning()) Update.abort();
                    result = "Ce fichier n'est pas un firmware du Tab5 (as-tu bien d\xC3\xA9zipp\xC3\xA9 le "
                             "t\xC3\xA9l\xC3\xA9" "chargement de GitHub ?)";
                    return;
                }
                if (n && !Update.isRunning() && !Update.begin(UPDATE_SIZE_UNKNOWN)) {
                    result = String("Impossible de pr\xC3\xA9parer la mise \xC3\xA0 jour : ") + Update.errorString();
                    return;
                }
                if (n && Update.write(const_cast<uint8_t*>(out), n) != n) {
                    result = String("\xC3\x89" "criture impossible : ") + Update.errorString();
                    Update.abort();
                    return;
                }
                int pct = expected ? (int)(stream.received() * 100 / expected) : -1;
                if (pct != lastPercent && millis() - lastDraw > 250) draw(pct, "R\xC3\xA9" "ception du firmware\xE2\x80\xA6");
            } else if (up.status == UPLOAD_FILE_END && accepted && result.length() == 0) {
                // end(true) vérifie l'image (somme de contrôle, puce) avant de la rendre active
                if (stream.written() > 0 && Update.end(true)) {
                    done = true;
                    result = "Firmware install\xC3\xA9 : le Tab5 red\xC3\xA9marre.";
                } else {
                    result = String("Firmware refus\xC3\xA9 : ") +
                             (stream.written() ? Update.errorString() : "fichier vide ou incomplet");
                    if (Update.isRunning()) Update.abort();
                }
            } else if (up.status == UPLOAD_FILE_ABORTED) {
                if (Update.isRunning()) Update.abort();
                result = "Transfert interrompu";
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
