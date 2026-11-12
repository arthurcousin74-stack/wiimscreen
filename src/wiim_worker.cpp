#include "wiim_worker.h"

#include <HTTPClient.h>
#include <WiFi.h>

#include <algorithm>

#include "discovery.h"
#include "favorites.h"
#include "log.h"
#include "upnp.h"
#include "voice.h"

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
            pqControl_.clear();  // nouvelle adresse : chercher à nouveau le service PlayQueue
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
        FavRequest save = favSave_;
        favSave_.wanted = false;
        String play = favPlay_;
        favPlay_ = "";
        String searchTest = searchTest_;
        searchTest_ = "";
        String search = search_;
        search_ = "";
        int searchPlay = searchPlay_;
        searchPlay_ = 0;
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
        if (play.length()) runPlayFavorite(play);
        if (searchTest.length()) runSearchTest(searchTest);
        if (search.length()) runSearch(search);
        if (searchPlay > 0) runPlaySearchResult(searchPlay);
        if (save.wanted) runSaveFavorite(save);
        if (pollNow || (int32_t)(millis() - nextPoll_) >= 0) pollOnce();
    }
}

// ---------------------------------------------------------------------------
// Diagnostic de la file de lecture (UPnP PlayQueue)

namespace {

constexpr const char* kPlayQueue = "urn:schemas-wiimu-com:service:PlayQueue:1";
constexpr size_t kMaxBody = 1024 * 1024;  // en PSRAM ; certaines files du WiiM dépassent 192 Ko

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

// Service PlayQueue du WiiM : URL de contrôle (gardée), actions offertes (journal)
bool WiimWorker::findPlayQueue(std::string& control, bool verbose) {
    if (!pqControl_.empty() && !verbose) {
        control = pqControl_;
        return true;
    }
    std::string host = cfg_.host.c_str();
    if (host.empty()) {
        logEvent("File : aucun WiiM choisi");
        return false;
    }
    size_t colon = host.find(':');
    if (colon != std::string::npos) host.resize(colon);
    // Description de l'appareil : 49152 (WiiM) ou 59152 (autres LinkPlay)
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
        return false;
    }
    auto services = wr::parseUpnpServices(desc);
    const wr::UpnpService* pq = nullptr;
    std::string types;
    for (auto& s : services) {
        size_t c = s.type.rfind(":service:");
        types += (types.empty() ? "" : ", ") + (c == std::string::npos ? s.type : s.type.substr(c + 9));
        if (s.type == kPlayQueue) pq = &s;
    }
    if (verbose) {
        logEvent("File : %s, %u services", base.c_str(), (unsigned)services.size());
        logLong("", types);
    }
    if (!pq) {
        logEvent("File : pas de service PlayQueue sur ce lecteur");
        return false;
    }
    pqControl_ = control = wr::upnpUrl(base, pq->controlUrl);
    if (verbose) {
        std::string scpd;
        if (upnpRequest(wr::upnpUrl(base, pq->scpdUrl), nullptr, "", scpd) == 200) {
            std::string names;
            for (auto& a : wr::parseScpdActions(scpd)) names += (names.empty() ? "" : " ") + a;
            logEvent("File : actions PlayQueue");
            logLong("", names);
        }
    }
    return true;
}

static int browseQueue(const std::string& control, const std::string& name, std::string& ctx) {
    std::string resp;
    bool cut = false;
    int code = upnpRequest(control, "BrowseQueue", wr::soapBody(kPlayQueue, "BrowseQueue", {{"QueueName", name}}), resp,
                           &cut);
    if (cut) logEvent("File : r\xC3\xA9ponse tronqu\xC3\xA9" "e \xC3\xA0 %u Ko", (unsigned)(kMaxBody / 1024));
    std::string raw;
    ctx = code == 200 && !cut && wr::xmlElement(resp, "QueueContext", raw) ? wr::xmlUnescape(raw) : std::string();
    return code;
}

// Files présentes sur le WiiM : vue d'ensemble « TotalQueue », sinon noms connus
static std::vector<std::string> queueNames(const std::string& control, bool verbose) {
    std::vector<std::string> names;
    std::string total;
    int code = browseQueue(control, "TotalQueue", total);
    if (code == 200 && !total.empty()) {
        if (verbose) serialDump("BrowseQueue TotalQueue", total, "");
        for (auto& n : wr::xmlElements(total, "ListName")) {
            std::string v = wr::xmlUnescape(n);
            if (!v.empty() && v != "TotalQueue" && std::find(names.begin(), names.end(), v) == names.end())
                names.push_back(v);
        }
    } else if (verbose) {
        logEvent("File : BrowseQueue(TotalQueue) -> %d", code);
    }
    for (const char* n : {"CurrentQueue", "USBDiskQueue", "MyFavouriteQueue", "LocalPlayList"})
        if (std::find(names.begin(), names.end(), n) == names.end()) names.push_back(n);
    return names;
}

void WiimWorker::runQueueCheck() {
    logEvent("File du WiiM : recherche du service UPnP\xE2\x80\xA6");
    std::string control;
    if (!findPlayQueue(control, true)) return;
    auto names = queueNames(control, true);
    std::vector<std::string> seen;  // une même file peut répondre sous plusieurs noms
    for (auto& n : names) {
        if (seen.size() >= 4) break;
        std::string ctx;
        if (browseQueue(control, n, ctx) != 200 || ctx.empty()) continue;
        wr::QueueInfo q = wr::parseQueueContext(ctx);
        const std::string key = wr::queueKey(q);
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
        seen.push_back(key);
        serialDump(("BrowseQueue " + n).c_str(), ctx, q.user);
        if (q.sourceName == "Qobuz") fav::saveQobuzTemplate(ctx);
        logEvent("File \xC2\xAB %s \xC2\xBB (nom %s) : source %s, %d piste(s)%s%s",
                 q.listName.empty() ? "?" : q.listName.c_str(), n.c_str(),
                 q.sourceName.empty() ? "?" : q.sourceName.c_str(), q.declaredTracks >= 0 ? q.declaredTracks : q.tracks,
                 q.quality.empty() ? "" : ", qualit\xC3\xA9 ", q.quality.c_str());
        if (!q.searchUrl.empty()) logLong("SearchUrl ", wr::maskSecrets(q.searchUrl));
        if (!q.user.empty()) logEvent("  compte %s", wr::maskAccount(q.user).c_str());
        for (size_t i = 0; i < q.titles.size() && i < 3; ++i) logLong("- ", q.titles[i]);
    }
    if (seen.empty()) logEvent("File : aucune file lisible (lance un album depuis l'app, puis r\xC3\xA9" "essaie)");
    logEvent("File du WiiM : termin\xC3\xA9 (XML complet sur le port s\xC3\xA9rie)");
}

// ---------------------------------------------------------------------------
// Favoris : copie de la file en cours, rejouée plus tard

void WiimWorker::saveFavorite(const String& title, const String& artist, const String& album, const String& artUrl) {
    xSemaphoreTake(lock_, portMAX_DELAY);
    favSave_ = FavRequest{true, title, artist, album, artUrl};
    xSemaphoreGive(lock_);
    xTaskNotifyGive(task_);
}

void WiimWorker::playFavorite(const String& id) {
    xSemaphoreTake(lock_, portMAX_DELAY);
    favPlay_ = id;
    xSemaphoreGive(lock_);
    xTaskNotifyGive(task_);
}

static bool sameText(const std::string& a, const std::string& b) {
    return !a.empty() && wr::normalizeText(a) == wr::normalizeText(b);
}

void WiimWorker::runSaveFavorite(const FavRequest& r) {
    WiimEvent ev;
    ev.kind = WiimEvent::FavSaved;
    std::string control;
    if (!findPlayQueue(control, false)) {
        ev.text = "le WiiM ne donne pas sa file de lecture";
        push(std::move(ev));
        return;
    }
    // La file qui contient le morceau en cours ; à défaut, la première trouvée
    std::string best, fallback;
    for (auto& n : queueNames(control, false)) {
        std::string ctx;
        if (browseQueue(control, n, ctx) != 200 || ctx.empty()) continue;
        wr::QueueInfo q = wr::parseQueueContext(ctx);
        if (!q.ok || (q.tracks == 0 && q.searchUrl.empty())) continue;
        if (fallback.empty()) fallback = ctx;
        for (auto& t : q.titles)
            if (sameText(t, r.title.c_str())) {
                best = ctx;
                break;
            }
        if (!best.empty()) break;
    }
    if (best.empty()) best = fallback;
    if (best.empty()) {
        ev.text = "aucune file sur le WiiM (lance l'album depuis l'app WiiM Home)";
        push(std::move(ev));
        return;
    }
    wr::QueueInfo q = wr::parseQueueContext(best);
    if (q.sourceName == "Qobuz") fav::saveQobuzTemplate(best);
    wr::Favorite f;
    f.key = wr::queueKey(q);
    f.title = r.album.length() ? r.album.c_str() : q.listName;
    f.artist = r.artist.c_str();
    f.source = q.sourceName;
    f.artUrl = r.artUrl.c_str();
    std::string err;
    ev.ok = fav::add(f, best, err);
    ev.text = ev.ok ? f.title.c_str() : err.c_str();
    logEvent("Favori \xC2\xAB %s \xC2\xBB (%s, %d piste(s), %u octets) : %s", f.title.c_str(), q.sourceName.c_str(),
             q.declaredTracks >= 0 ? q.declaredTracks : q.tracks, (unsigned)best.size(), ev.ok ? "OK" : err.c_str());
    push(std::move(ev));
}

// Envoie une file au WiiM puis la lance ; renvoie le code HTTP de PlayQueueWithIndex (-1 si non tenté)
static int sendQueue(const std::string& control, const std::string& ctx, const std::string& listName, int& create) {
    std::string resp;
    // QueuePolicy 1 comme dans l'exemple connu ; sans l'argument si le WiiM le refuse
    create = upnpRequest(control, "CreateQueue",
                         wr::soapBody(kPlayQueue, "CreateQueue", {{"QueueContext", ctx}, {"QueuePolicy", "1"}}), resp);
    if (create != 200)
        create = upnpRequest(control, "CreateQueue", wr::soapBody(kPlayQueue, "CreateQueue", {{"QueueContext", ctx}}),
                             resp);
    if (create != 200) return -1;
    return upnpRequest(control, "PlayQueueWithIndex",
                       wr::soapBody(kPlayQueue, "PlayQueueWithIndex", {{"QueueName", listName}, {"Index", "1"}}), resp);
}

void WiimWorker::testQobuzSearch(const String& query) {
    xSemaphoreTake(lock_, portMAX_DELAY);
    searchTest_ = query.length() ? query : String("Miles Davis");
    xSemaphoreGive(lock_);
    xTaskNotifyGive(task_);
}

// Essai sans clé : le WiiM lit-il lui-même les pistes d'une file Qobuz qui n'a qu'une adresse
// (album, puis recherche de morceaux) ? Fait jouer de la musique : lancé seulement depuis le journal.
void WiimWorker::runSearchTest(const String& query) {
    logEvent("Essai Qobuz sans cl\xC3\xA9 (\xC2\xAB %s \xC2\xBB) : la musique va changer", query.c_str());
    std::string control;
    if (!findPlayQueue(control, false)) return;
    // Modèle : une vraie file Qobuz (en cours sur le WiiM, sinon un favori)
    std::string templ;
    for (auto& n : queueNames(control, false)) {
        std::string ctx;
        if (browseQueue(control, n, ctx) == 200 && wr::parseQueueContext(ctx).sourceName == "Qobuz") {
            templ = ctx;
            break;
        }
    }
    if (templ.empty())
        for (auto& f : fav::list())
            if (f.source == "Qobuz" && fav::context(f.id, templ)) break;
    if (templ.empty()) {
        logEvent("Essai : aucune file Qobuz comme mod\xC3\xA8le (lance un album Qobuz depuis l'app, puis r\xC3\xA9" "essaie)");
        return;
    }
    fav::saveQobuzTemplate(templ);
    const wr::QueueInfo model = wr::parseQueueContext(templ);
    logEvent("Essai : mod\xC3\xA8le \xC2\xAB %s \xC2\xBB, compte %s, qualit\xC3\xA9 %s", model.listName.c_str(),
             wr::maskAccount(model.user).c_str(), model.quality.c_str());

    struct Variant {
        const char* label;
        std::string name, url;
    };
    std::vector<Variant> variants;
    if (model.searchUrl.find("/get?") != std::string::npos)
        variants.push_back({"A (album sans pistes)", "wiimscreen-album", model.searchUrl});
    variants.push_back({"B (recherche de morceaux)", "wiimscreen-recherche",
                        "https://www.qobuz.com/api.json/0.2/track/search?query=" + wr::urlQueryEncode(query.c_str()) +
                            "&offset=0&limit=50"});
    for (auto& v : variants) {
        const std::string ctx = wr::queueFromTemplate(templ, v.name, v.url);
        int create = 0;
        const int play = sendQueue(control, ctx, v.name, create);
        logEvent("Essai %s : CreateQueue %d, PlayQueueWithIndex %d", v.label, create, play);
        if (play != 200) continue;
        vTaskDelay(pdMS_TO_TICKS(6000));  // le temps que le WiiM lise les pistes et démarre
        std::string back;
        if (browseQueue(control, v.name, back) == 200 && !back.empty()) {
            wr::QueueInfo q = wr::parseQueueContext(back);
            logEvent("  file relue : %d piste(s), source %s", q.tracks, q.sourceName.c_str());
            for (size_t i = 0; i < q.titles.size() && i < 3; ++i) logLong("  - ", q.titles[i]);
        } else {
            logEvent("  file relue : rien");
        }
        wr::PlayerStatus st;
        wr::TrackMeta m;
        WiimError e = client_.status(st);
        if (e == WiimError::None && client_.metaInfo(m) == WiimError::None)
            logEvent("  le WiiM %s : %s \xE2\x80\x94 %s", st.playing ? "joue" : "ne joue pas", m.title.c_str(),
                     m.artist.c_str());
        else
            logEvent("  \xC3\xA9tat du WiiM : %s", wiimErrorName(e));
    }
    logEvent("Essai Qobuz sans cl\xC3\xA9 : termin\xC3\xA9");
    nextPoll_ = millis();
}

// ---------------------------------------------------------------------------
// Recherche Qobuz sans clé : le WiiM lit lui-même les résultats d'une file dont l'adresse est une
// recherche de morceaux (vérifié sur un WiiM, 2026-11). La file est relue pour afficher les résultats.

static const char* kSearchQueue = "wiimscreen-recherche";

void WiimWorker::searchQobuz(const String& query) {
    xSemaphoreTake(lock_, portMAX_DELAY);
    search_ = query;
    xSemaphoreGive(lock_);
    xTaskNotifyGive(task_);
}

void WiimWorker::playSearchResult(int index) {
    xSemaphoreTake(lock_, portMAX_DELAY);
    searchPlay_ = index + 1;  // 1 pour le premier résultat
    xSemaphoreGive(lock_);
    xTaskNotifyGive(task_);
}

// Modèle de file Qobuz : celui gardé, sinon une file Qobuz du WiiM, sinon un favori Qobuz
bool WiimWorker::qobuzModel(const std::string& control, std::string& templ) {
    if (fav::qobuzTemplate(templ)) return true;
    for (auto& n : queueNames(control, false)) {
        std::string ctx;
        if (browseQueue(control, n, ctx) == 200 && wr::parseQueueContext(ctx).sourceName == "Qobuz") {
            fav::saveQobuzTemplate(ctx);
            templ = ctx;
            return true;
        }
    }
    for (auto& f : fav::list())
        if (f.source == "Qobuz" && fav::context(f.id, templ)) return true;
    return false;
}

void WiimWorker::runSearch(const String& query) {
    WiimEvent ev;
    ev.kind = WiimEvent::SearchDone;
    ev.name = query;
    std::string control, templ;
    if (!findPlayQueue(control, false)) {
        ev.text = "le WiiM ne r\xC3\xA9pond pas";
        push(std::move(ev));
        return;
    }
    if (!qobuzModel(control, templ)) {
        ev.text = "lance d'abord un album Qobuz depuis l'app WiiM Home (une seule fois)";
        push(std::move(ev));
        return;
    }
    // Résultats relus ; d'abord sans lancer la lecture, puis en la lançant si la file reste vide
    std::string resp;
    // Le WiiM ajoute les résultats peu à peu (1 ou 2 après 1 s, 7 après 6 s) : on relit jusqu'à ce que
    // leur nombre ne bouge plus (deux lectures égales), dans la limite de `seconds`
    auto readBack = [&](const std::string& name, int seconds, wr::QueueInfo& out) {
        int last = -1, stable = 0;
        const uint32_t until = millis() + seconds * 1000UL;
        while ((int32_t)(millis() - until) < 0) {
            vTaskDelay(pdMS_TO_TICKS(700));
            std::string back;
            if (browseQueue(control, name, back) != 200 || back.empty()) continue;
            wr::QueueInfo q = wr::parseQueueContext(back);
            if (q.tracks > 0 && q.tracks == last) {
                if (++stable >= 2) break;
            } else {
                stable = 0;
            }
            last = q.tracks;
            out = q;
        }
        return out.tracks > 0;
    };
    // Façons d'écrire les espaces dans l'adresse : on passe à la suivante si les résultats n'ont
    // aucun mot de la recherche (le WiiM a pu mal lire la requête)
    const std::string words = query.c_str();
    std::string plus = wr::urlQueryEncode(words);
    for (size_t p; (p = plus.find("%20")) != std::string::npos;) plus.replace(p, 3, "+");
    const std::string enc = wr::urlQueryEncode(words), base = "https://www.qobuz.com/api.json/0.2/";
    const std::pair<const char*, std::string> variants[] = {
        {"track/search, %20", base + "track/search?query=" + enc + "&offset=0&limit=50"},
        {"track/search, +", base + "track/search?query=" + plus + "&offset=0&limit=50"},
        {"catalog/search tracks", base + "catalog/search?query=" + enc + "&type=tracks&offset=0&limit=50"},
        {"query en dernier", base + "track/search?offset=0&limit=50&query=" + enc}};
    const uint32_t t0 = millis();
    wr::QueueInfo best;
    for (auto& v : variants) {
        // Nom nouveau à chaque essai (deux en alternance) : pas de risque de relire l'ancienne file
        const std::string name = std::string(kSearchQueue) + "-" + std::to_string(++searchCount_ % 2);
        upnpRequest(control, "DeleteQueue", wr::soapBody(kPlayQueue, "DeleteQueue", {{"QueueName", name}}), resp);
        const std::string& url = v.second;
        const std::string ctx = wr::queueFromTemplate(templ, name, url);
        int create = upnpRequest(control, "CreateQueue",
                                 wr::soapBody(kPlayQueue, "CreateQueue", {{"QueueContext", ctx}, {"QueuePolicy", "1"}}), resp);
        if (create != 200)
            create = upnpRequest(control, "CreateQueue", wr::soapBody(kPlayQueue, "CreateQueue", {{"QueueContext", ctx}}),
                                 resp);
        logEvent("Recherche \xC2\xAB %s \xC2\xBB (%s) : CreateQueue %d", query.c_str(), v.first, create);
        logLong("envoy\xC3\xA9 ", url);
        if (create != 200) continue;
        wr::QueueInfo q;
        bool played = false;
        bool found = readBack(name, 8, q);
        if (!found) {
            played = upnpRequest(control, "PlayQueueWithIndex",
                                 wr::soapBody(kPlayQueue, "PlayQueueWithIndex", {{"QueueName", name}, {"Index", "1"}}),
                                 resp) == 200;
            found = played && readBack(name, 8, q);
        }
        if (found) logLong(q.searchUrl == url ? "relu (identique) " : "relu ", wr::maskSecrets(q.searchUrl));
        std::vector<std::string> texts;
        for (auto& t : q.items) texts.insert(texts.end(), {t.title, t.artist, t.album});
        const bool relevant = found && wr::sharesWord(words, texts);
        logEvent("  %d r\xC3\xA9sultat(s)%s%s", found ? q.tracks : 0, played ? ", lecture lanc\xC3\xA9" "e" : ", sans lecture",
                 found ? (relevant ? ", en rapport" : ", SANS RAPPORT avec la recherche") : "");
        for (size_t i = 0; i < q.items.size() && i < 2; ++i)
            logLong("  - ", q.items[i].title + " \xE2\x80\x94 " + q.items[i].artist);
        if (found && (best.tracks == 0 || relevant)) {
            best = q;
            ev.played = played;
            searchQueue_ = name;
        }
        if (relevant) break;  // en rapport : terminé ; sinon, écriture suivante
    }
    logEvent("Recherche : termin\xC3\xA9" "e en %lu ms", (unsigned long)(millis() - t0));
    ev.ok = true;
    ev.tracks = best.items;
    const bool played = ev.played;
    push(std::move(ev));
    if (played) nextPoll_ = millis();
}

void WiimWorker::runPlaySearchResult(int index) {
    std::string control, resp;
    int code = -1;
    if (findPlayQueue(control, false))
        code = upnpRequest(control, "PlayQueueWithIndex",
                           wr::soapBody(kPlayQueue, "PlayQueueWithIndex",
                                        {{"QueueName", searchQueue_}, {"Index", std::to_string(index)}}),
                           resp);
    logEvent("Recherche : r\xC3\xA9sultat %d -> %d", index, code);
    WiimEvent ev;
    ev.kind = WiimEvent::FavPlayed;
    ev.ok = code == 200;
    if (!ev.ok) ev.text = "le WiiM n'a pas lanc\xC3\xA9 ce morceau";
    push(std::move(ev));
    nextPoll_ = millis() + 800;
}

void WiimWorker::runPlayFavorite(const String& id) {
    WiimEvent ev;
    ev.kind = WiimEvent::FavPlayed;
    std::string ctx, control;
    if (!fav::context(id.c_str(), ctx)) {
        ev.text = "favori introuvable";
    } else if (!findPlayQueue(control, false)) {
        ev.text = "le WiiM n'accepte pas de file de lecture";
    } else {
        wr::QueueInfo q = wr::parseQueueContext(ctx);
        const uint32_t t0 = millis();
        int c = 0;
        const int p = sendQueue(control, ctx, q.listName, c);
        logEvent("Favori \xE2\x96\xB6 \xC2\xAB %s \xC2\xBB : CreateQueue %d, PlayQueueWithIndex %d (%lu ms)", q.listName.c_str(), c,
                 p, (unsigned long)(millis() - t0));
        ev.ok = c == 200 && p == 200;
        if (!ev.ok) ev.text = "le WiiM a refus\xC3\xA9 la file";
    }
    push(std::move(ev));
    nextPoll_ = millis() + 800;  // relire l'état peu après
}
