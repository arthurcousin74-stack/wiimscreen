#include "wiim_worker.h"

#include <HTTPClient.h>
#include <WiFi.h>

#include <algorithm>

#include "discovery.h"
#include "log.h"
#include "upnp.h"

// Connexion gardée ouverte (WiimClient) : un relevé coûte ~0,1 s, on peut relever souvent
static constexpr uint32_t kPollPlayingMs = 1500;
static constexpr uint32_t kPollIdleMs = 3000;
static constexpr uint32_t kAfterCommandMs = 300;  // relire l'état peu après une commande
static const char* kVolPrefix = "setPlayerCmd:vol:";

const char* wiimErrorName(WiimError e) {
    switch (e) {
        case WiimError::None: return "OK";
        case WiimError::Network: return "injoignable";
        case WiimError::PinMismatch: return "certificat inattendu";
        case WiimError::Http: return "r\xC3\xA9ponse inattendue";
        case WiimError::Refused: return "refus\xC3\xA9";
    }
    return "?";
}

void WiimWorker::begin(const Settings& s) {
    cfg_ = s;
    client_.configure(cfg_);
    lock_ = xSemaphoreCreateMutex();
    // Cœur 0, celui du Wi-Fi ; la boucle Arduino (écran, tactile) tourne sur le cœur 1
    xTaskCreatePinnedToCore(taskMain, "wiim", 16 * 1024, this, 2, &task_, 0);
}

void WiimWorker::command(const String& cmd) {
    xSemaphoreTake(lock_, portMAX_DELAY);
    ++queued_;
    if (cmd.startsWith(kVolPrefix)) {
        // Volume : seul le dernier compte ; les précédents en attente sont comptés comme faits
        for (auto it = cmds_.begin(); it != cmds_.end();)
            if (it->startsWith(kVolPrefix)) it = cmds_.erase(it), ++executed_;
            else ++it;
    }
    cmds_.push_back(cmd);
    xSemaphoreGive(lock_);
    xTaskNotifyGive(task_);
}

void WiimWorker::refreshPresets() {
    xSemaphoreTake(lock_, portMAX_DELAY);
    presetsWanted_ = true;
    xSemaphoreGive(lock_);
    xTaskNotifyGive(task_);
}

void WiimWorker::pollSoon() {
    xSemaphoreTake(lock_, portMAX_DELAY);
    pollWanted_ = true;
    xSemaphoreGive(lock_);
    xTaskNotifyGive(task_);
}

void WiimWorker::checkQueue() {
    xSemaphoreTake(lock_, portMAX_DELAY);
    queueWanted_ = true;
    xSemaphoreGive(lock_);
    xTaskNotifyGive(task_);
}

bool WiimWorker::takeReport(WiimReport& out) {
    bool got = false;
    xSemaphoreTake(lock_, portMAX_DELAY);
    if (hasReport_) {
        out = report_;
        hasReport_ = false;
        got = true;
    }
    xSemaphoreGive(lock_);
    return got;
}

bool WiimWorker::takeEvent(WiimEvent& out) {
    bool got = false;
    xSemaphoreTake(lock_, portMAX_DELAY);
    if (!events_.empty()) {
        out = std::move(events_.front());
        events_.pop_front();
        got = true;
    }
    xSemaphoreGive(lock_);
    return got;
}

void WiimWorker::push(WiimEvent&& e) {
    xSemaphoreTake(lock_, portMAX_DELAY);
    events_.push_back(std::move(e));
    while (events_.size() > 20) events_.pop_front();
    xSemaphoreGive(lock_);
}

bool WiimWorker::commandsWaiting() {
    xSemaphoreTake(lock_, portMAX_DELAY);
    bool w = !cmds_.empty();
    xSemaphoreGive(lock_);
    return w;
}

// Certificat enregistré au premier échange, ou différent de celui enregistré
void WiimWorker::notePinning(WiimError e) {
    if (client_.takeJustPinned()) {
        WiimEvent ev;
        ev.kind = WiimEvent::Pinned;
        push(std::move(ev));
    }
    if (e == WiimError::PinMismatch) {
        WiimEvent ev;
        ev.kind = WiimEvent::PinMismatch;
        ev.error = e;
        ev.text = client_.lastSeenFingerprint();
        push(std::move(ev));
    }
}

void WiimWorker::execute(const String& cmd) {
    const uint32_t t0 = millis();
    WiimError e = client_.command(cmd);
    logEvent("WiiM <- %s : %s (%lu ms)", cmd.c_str(), wiimErrorName(e), (unsigned long)(millis() - t0));
    notePinning(e);
    WiimEvent ev;
    ev.kind = WiimEvent::CommandDone;
    ev.error = e;
    ev.text = cmd;
    push(std::move(ev));
    xSemaphoreTake(lock_, portMAX_DELAY);
    ++executed_;
    xSemaphoreGive(lock_);
    nextPoll_ = millis() + kAfterCommandMs;
}

// Le WiiM ne répond plus : il a peut-être changé d'adresse. On cherche le même (uuid),
// jamais un autre lecteur ; l'empreinte enregistrée doit toujours correspondre.
bool WiimWorker::rediscover() {
    if (!cfg_.wiimUuid.length() || millis() - lastRediscover_ < 120000) return false;
    lastRediscover_ = millis();
    for (auto& d : discoverWiims(2000))
        if (d.uuid == cfg_.wiimUuid.c_str() && d.ip != cfg_.host.c_str()) {
            cfg_.host = d.ip.c_str();
            logEvent("WiiM retrouv\xC3\xA9 \xC3\xA0 %s", cfg_.host.c_str());
            saveWiim(cfg_.host, cfg_.wiimUuid, cfg_.wiimName);
            client_.configure(cfg_);
            WiimEvent ev;
            ev.kind = WiimEvent::Moved;
            ev.text = cfg_.host;
            push(std::move(ev));
            return true;
        }
    return false;
}

// Adresse saisie à la main : on lit une fois l'identité du WiiM pour pouvoir le suivre
void WiimWorker::learnIdentity() {
    if (identityChecked_ || cfg_.wiimUuid.length()) return;
    identityChecked_ = true;
    std::string body;
    if (client_.command("getStatusEx", &body) != WiimError::None) return;
    wr::WiimIdentity id = wr::parseStatusEx(body);
    if (!id.ok || id.uuid.empty()) return;
    cfg_.wiimUuid = id.uuid.c_str();
    cfg_.wiimName = id.name.c_str();
    saveWiim(cfg_.host, cfg_.wiimUuid, cfg_.wiimName);
    WiimEvent ev;
    ev.kind = WiimEvent::Identity;
    ev.uuid = cfg_.wiimUuid;
    ev.name = cfg_.wiimName;
    push(std::move(ev));
}

void WiimWorker::pollOnce() {
    WiimReport r;
    xSemaphoreTake(lock_, portMAX_DELAY);
    r.cmdSeq = executed_;
    xSemaphoreGive(lock_);

    wr::PlayerStatus st;
    WiimError e = client_.status(st);
    notePinning(e);
    netFailures_ = e == WiimError::Network ? netFailures_ + 1 : 0;
    if (netFailures_ >= 3 && rediscover()) {
        netFailures_ = 0;
        e = client_.status(st);
        notePinning(e);
    }
    r.error = e;
    r.online = e == WiimError::None;
    if (!polledOnce_ || e != lastPollError_)  // seulement les changements : pas de journal inondé
        logEvent("Relev\xC3\xA9 du WiiM (%s) : %s%s", cfg_.host.c_str(), wiimErrorName(e),
                 r.online ? (st.playing ? ", lecture" : ", arr\xC3\xAAt") : "");
    polledOnce_ = true;
    lastPollError_ = e;
    if (r.online) {
        learnIdentity();
        // Titre, artiste, album, pochette, qualité : getMetaInfo (absents de getPlayerStatus
        // sur les WiiM). Sauté si une commande attend : elle passe d'abord.
        if (!commandsWaiting()) {
            wr::TrackMeta m;
            if (client_.metaInfo(m) == WiimError::None) {
                r.meta = m;
                r.metaOk = true;
                if (!m.title.empty()) st.title = m.title;
                if (!m.artist.empty()) st.artist = m.artist;
                if (!m.album.empty()) st.album = m.album;
            }
        }
        r.player = st;
    }
    r.at = millis();
    xSemaphoreTake(lock_, portMAX_DELAY);
    report_ = r;
    hasReport_ = true;
    xSemaphoreGive(lock_);
    nextPoll_ = millis() + (r.online && st.playing ? kPollPlayingMs : kPollIdleMs);
}

void WiimWorker::taskMain(void* self) { static_cast<WiimWorker*>(self)->run(); }

void WiimWorker::run() {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
        // Les commandes passent toujours en premier
        for (;;) {
            xSemaphoreTake(lock_, portMAX_DELAY);
            bool has = !cmds_.empty();
            String cmd;
            if (has) {
                cmd = cmds_.front();
                cmds_.pop_front();
            }
            xSemaphoreGive(lock_);
            if (!has) break;
            execute(cmd);
        }
        xSemaphoreTake(lock_, portMAX_DELAY);
        bool presets = presetsWanted_, pollNow = pollWanted_, queue = queueWanted_;
        presetsWanted_ = pollWanted_ = queueWanted_ = false;
        xSemaphoreGive(lock_);
        if (presets) {
            WiimEvent ev;
            ev.kind = WiimEvent::Presets;
            ev.error = client_.presets(ev.presets);
            ev.ok = ev.error == WiimError::None;
            logEvent("Presets du WiiM : %s, %u", wiimErrorName(ev.error), (unsigned)ev.presets.size());
            notePinning(ev.error);
            push(std::move(ev));
        }
        if (queue) runQueueCheck();
        if (pollNow || (int32_t)(millis() - nextPoll_) >= 0) pollOnce();
    }
}

// ---------------------------------------------------------------------------
// Diagnostic de la file de lecture (UPnP PlayQueue)

namespace {

constexpr const char* kPlayQueue = "urn:schemas-wiimu-com:service:PlayQueue:1";
constexpr size_t kMaxBody = 192 * 1024;

// Flux qui garde au plus `cap` octets (HTTPClient gère « chunked » et Content-Length)
class CapStream : public Stream {
public:
    explicit CapStream(size_t cap) : cap_(cap) {}
    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t* b, size_t n) override {
        size_t room = data.size() < cap_ ? cap_ - data.size() : 0;
        data.append(reinterpret_cast<const char*>(b), std::min(room, n));
        truncated |= n > room;
        return n;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
    std::string data;
    bool truncated = false;

private:
    size_t cap_;
};

// GET ou POST SOAP en HTTP simple (l'UPnP du WiiM n'est pas chiffré)
int upnpRequest(const std::string& url, const char* soapAction, const std::string& body, std::string& out,
                bool* truncated = nullptr) {
    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(2500);
    http.setTimeout(6000);
    http.setReuse(false);
    if (!http.begin(client, url.c_str())) return -1;
    int code;
    if (soapAction) {
        http.addHeader("Content-Type", "text/xml; charset=\"utf-8\"");
        http.addHeader("SOAPACTION", (String("\"") + kPlayQueue + "#" + soapAction + "\"").c_str());
        code = http.POST(reinterpret_cast<uint8_t*>(const_cast<char*>(body.data())), body.size());
    } else {
        code = http.GET();
    }
    CapStream s(kMaxBody);
    if (code > 0) http.writeToStream(&s);
    http.end();
    out.swap(s.data);
    if (truncated) *truncated = s.truncated;
    return code;
}

// Journal : lignes courtes (l'écran coupe au-delà), découpées si besoin
void logLong(const char* label, const std::string& text) {
    const size_t kChunk = 56;  // une ligne de l'écran du journal
    if (text.size() <= kChunk) {
        logEvent("  %s%s", label, text.c_str());
        return;
    }
    for (size_t i = 0; i < text.size() && i < kChunk * 6; i += kChunk)
        logEvent("  %s%s", i == 0 ? label : "  ", text.substr(i, kChunk).c_str());
}

// Copie complète sur le port série, sans jeton ni compte en clair
void serialDump(const char* title, const std::string& xml, const std::string& user) {
    std::string safe = wr::maskSecrets(xml);
    if (!user.empty()) {
        size_t p = 0;
        const std::string masked = wr::maskAccount(user);
        while ((p = safe.find(user, p)) != std::string::npos) safe.replace(p, user.size(), masked), p += masked.size();
    }
    Serial.printf("----- %s (%u octets) -----\n", title, (unsigned)safe.size());
    for (size_t i = 0; i < safe.size(); i += 256) Serial.write(safe.data() + i, std::min<size_t>(256, safe.size() - i));
    Serial.println("\n----- fin -----");
}

}  // namespace

void WiimWorker::runQueueCheck() {
    std::string host = cfg_.host.c_str();
    if (host.empty()) {
        logEvent("File : aucun WiiM choisi");
        return;
    }
    size_t colon = host.find(':');
    if (colon != std::string::npos) host.resize(colon);
    logEvent("File du WiiM : recherche du service UPnP\xE2\x80\xA6");

    // 1. Description de l'appareil : 49152 (WiiM) ou 59152 (autres LinkPlay)
    std::string base, desc;
    for (int port : {49152, 59152}) {
        std::string b = "http://" + host + ":" + std::to_string(port);
        if (upnpRequest(b + "/description.xml", nullptr, "", desc) == 200 && !desc.empty()) {
            base = b;
            break;
        }
    }
    if (base.empty()) {
        logEvent("File : description UPnP introuvable (ports 49152, 59152)");
        return;
    }
    auto services = wr::parseUpnpServices(desc);
    std::string types;
    const wr::UpnpService* pq = nullptr;
    for (auto& s : services) {
        size_t c = s.type.rfind(":service:");
        types += (types.empty() ? "" : ", ") + (c == std::string::npos ? s.type : s.type.substr(c + 9));
        if (s.type == kPlayQueue) pq = &s;
    }
    logEvent("File : %s, %u services", base.c_str(), (unsigned)services.size());
    logLong("", types);
    if (!pq) {
        logEvent("File : pas de service PlayQueue sur ce lecteur");
        return;
    }
    const std::string control = wr::upnpUrl(base, pq->controlUrl);

    // 2. Actions offertes (SCPD)
    std::string scpd;
    if (upnpRequest(wr::upnpUrl(base, pq->scpdUrl), nullptr, "", scpd) == 200) {
        std::string names;
        for (auto& a : wr::parseScpdActions(scpd)) names += (names.empty() ? "" : " ") + a;
        logEvent("File : actions PlayQueue");
        logLong("", names);
    }

    // 3. Vue d'ensemble (« TotalQueue »), puis chaque file nommée
    auto browse = [&](const std::string& name, std::string& ctx) -> int {
        std::string resp;
        bool cut = false;
        int code = upnpRequest(control, "BrowseQueue", wr::soapBody(kPlayQueue, "BrowseQueue", {{"QueueName", name}}),
                               resp, &cut);
        if (cut) logEvent("File : r\xC3\xA9ponse tronqu\xC3\xA9" "e \xC3\xA0 %u Ko", (unsigned)(kMaxBody / 1024));
        std::string raw;
        ctx = code == 200 && wr::xmlElement(resp, "QueueContext", raw) ? wr::xmlUnescape(raw) : std::string();
        return code;
    };
    std::string total;
    int code = browse("TotalQueue", total);
    std::vector<std::string> names;
    if (code == 200 && !total.empty()) {
        serialDump("BrowseQueue TotalQueue", total, "");
        for (auto& n : wr::xmlElements(total, "ListName")) {
            std::string v = wr::xmlUnescape(n);
            if (!v.empty() && v != "TotalQueue" && std::find(names.begin(), names.end(), v) == names.end())
                names.push_back(v);
        }
        std::string list;
        for (auto& n : names) list += (list.empty() ? "" : " | ") + n;
        logEvent("File : %u file(s) sur le WiiM", (unsigned)names.size());
        if (!list.empty()) logLong("", list);
    } else {
        logEvent("File : BrowseQueue(TotalQueue) -> %d", code);
    }
    // Noms vus ailleurs (app WiiM, autres projets) si la vue d'ensemble n'a rien donné
    if (names.empty()) names = {"CurrentQueue", "USBDiskQueue", "Qobuz", "TIDAL", "Amazon"};

    int shown = 0;
    for (auto& n : names) {
        if (shown >= 4) break;
        std::string ctx;
        int c = browse(n, ctx);
        if (c != 200 || ctx.empty()) continue;
        ++shown;
        wr::QueueInfo q = wr::parseQueueContext(ctx);
        serialDump(("BrowseQueue " + n).c_str(), ctx, q.user);
        logEvent("File \xC2\xAB %s \xC2\xBB : source %s, %d piste(s)%s%s", q.listName.empty() ? n.c_str() : q.listName.c_str(),
                 q.sourceName.empty() ? "?" : q.sourceName.c_str(), q.declaredTracks >= 0 ? q.declaredTracks : q.tracks,
                 q.quality.empty() ? "" : ", qualit\xC3\xA9 ", q.quality.c_str());
        if (!q.searchUrl.empty()) logLong("SearchUrl ", wr::maskSecrets(q.searchUrl));
        if (!q.user.empty()) logEvent("  compte %s", wr::maskAccount(q.user).c_str());
        for (auto& t : q.titles) logLong("- ", t);
    }
    if (!shown) logEvent("File : aucune file lisible (lance un album depuis l'app, puis r\xC3\xA9" "essaie)");
    logEvent("File du WiiM : termin\xC3\xA9 (XML complet sur le port s\xC3\xA9rie)");
}
