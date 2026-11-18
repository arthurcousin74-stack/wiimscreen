#include "wiim_worker.h"

#include <WiFi.h>

#include "discovery.h"
#include "log.h"
#include "upnp.h"
#include "wiim_worker_internal.h"

using namespace wiimq;

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

// Valeur ASCII stable pour les lignes étiquetées du journal (logTagged, src/log.h)
const char* wiimq::errorKey(WiimError e) {
    switch (e) {
        case WiimError::None: return "ok";
        case WiimError::Network: return "network";
        case WiimError::PinMismatch: return "pin";
        case WiimError::Http: return "http";
        case WiimError::Refused: return "refused";
    }
    return "unknown";
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
    if (!cfg_.wiimUuid.length() || (lastRediscover_ && millis() - lastRediscover_ < 120000)) return false;
    // HTTPS sans certificat épinglé encore (aucun échange réussi) : rien ne vérifierait la nouvelle adresse, et
    // l'uuid se lit sans authentification. On ne suit personne tant que le WiiM choisi n'a pas répondu une fois.
    if (cfg_.https && !client_.pinnedFingerprint().length()) return false;
    lastRediscover_ = millis() | 1;  // 0 = jamais essayé (permis dès le démarrage)
    for (auto& d : discoverWiims(2000))
        if (d.uuid == cfg_.wiimUuid.c_str() && d.ip != cfg_.host.c_str()) {
            // L'uuid se lit sans authentification : n'importe quel appareil peut s'en servir. On garde l'empreinte
            // épinglée (sinon le prochain certificat serait accepté et enregistré) et on ne suit la nouvelle adresse
            // qu'après une connexion qui la vérifie.
            const String oldHost = cfg_.host;
            cfg_.fingerprint = client_.pinnedFingerprint();
            cfg_.host = d.ip.c_str();
            client_.configure(cfg_);
            wr::PlayerStatus check;
            const WiimError e = client_.status(check);
            if (e != WiimError::None) {
                logEvent("WiiM vu \xC3\xA0 %s mais non suivi (%s)", cfg_.host.c_str(), wiimErrorName(e));
                cfg_.host = oldHost;
                client_.configure(cfg_);
                continue;
            }
            logEvent("WiiM retrouv\xC3\xA9 \xC3\xA0 %s", cfg_.host.c_str());
            saveWiim(cfg_.host, cfg_.wiimUuid, cfg_.wiimName);
            pqControl_.clear();  // nouvelle adresse : chercher à nouveau le service PlayQueue
            pqFailedAt_ = 0;
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
    if (cfg_.wiimUuid.length() || (identityTriedAt_ && millis() - identityTriedAt_ < 300000)) return;
    identityTriedAt_ = millis() | 1;  // nouvel essai dans 5 min en cas d'échec (sinon plus de suivi du WiiM)
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
        logTagged("wiim.status", "result=%s playing=%d | Relev\xC3\xA9 du WiiM (%s) : %s%s", errorKey(e),
                  r.online && st.playing ? 1 : 0, cfg_.host.c_str(), wiimErrorName(e),
                 r.online ? (st.playing ? ", lecture" : ", arr\xC3\xAAt") : "");
    polledOnce_ = true;
    lastPollError_ = e;
    if (r.online) {
        learnIdentity();
        if (!undoChecked_) undoInterruptedSearch(st);
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
            // File qui joue (clé du ♥) : une requête UPnP locale par changement de morceau, pas à chaque relevé (une
            // file peut peser des Mo). Même morceau : relue seulement si sa clé n'a pas pu être lue, toutes les 60 s.
            if (r.metaOk && (st.title != queueTitle_ || (queueKey_.empty() && millis() - queueReadAt_ > 60000))) {
                queueTitle_ = st.title;
                queueReadAt_ = millis();
                std::string control, ctx;
                queueKey_.clear();
                if (!st.title.empty() && findPlayQueue(control, false) && browseQueue(control, "CurrentQueue", ctx) == 200 &&
                    !ctx.empty()) {
                    const wr::QueueInfo q = wr::parseQueueContext(ctx);
                    queueKey_ = wr::queueKey(q);
                    noteModel(q, ctx);  // modèle de file Qobuz/Deezer pris au passage : pas de lecture de toutes les files
                    if (wr::isLiveQueue(q) && q.listName == "Flow") autoFlowTile(q, ctx);
                }
            }
        }
        r.queueKey = queueKey_;
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

void WiimWorker::drainCommands() {
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
}

// Attente dans une tâche longue (album qui se charge, recherche…) : les appuis (pause, volume…) passent
// pendant ce temps, et le relevé d'état continue si `poll` (pas pendant une recherche à son coupé, dont
// l'état transitoire ne doit pas s'afficher).
void WiimWorker::pause(uint32_t ms, bool poll) {
    const uint32_t until = millis() + ms;
    while ((int32_t)(millis() - until) < 0) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
        drainCommands();
        if (poll && (int32_t)(millis() - nextPoll_) >= 0) pollOnce();
    }
}

void WiimWorker::run() {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
        drainCommands();  // les commandes passent toujours en premier
        xSemaphoreTake(lock_, portMAX_DELAY);
        bool presets = presetsWanted_, pollNow = pollWanted_, queue = queueWanted_, qfavs = qobuzFavsWanted_;
        presetsWanted_ = pollWanted_ = queueWanted_ = qobuzFavsWanted_ = false;
        FavRequest save = favSave_;
        favSave_.wanted = false;
        String play = favPlay_;
        favPlay_ = "";
        String search = search_;
        search_ = "";
        const SearchKind searchKind = searchReq_;
        int searchPlay = searchPlay_;
        searchPlay_ = 0;
        AlbumFavRequest albumSave = albumSave_;
        albumSave_.wanted = false;
        xSemaphoreGive(lock_);
        if (presets) {
            WiimEvent ev;
            ev.kind = WiimEvent::Presets;
            ev.error = client_.presets(ev.presets);
            ev.ok = ev.error == WiimError::None;
            logTagged("wiim.presets", "result=%s count=%u | Presets du WiiM : %s, %u", errorKey(ev.error),
                      (unsigned)ev.presets.size(), wiimErrorName(ev.error), (unsigned)ev.presets.size());
            notePinning(ev.error);
            push(std::move(ev));
        }
        if (queue) runQueueCheck();
        if (qfavs) {
            runQobuzFavorites();
            runDeezerFavorites();
        }
        if (play.length()) runPlayFavorite(play);
        // Appui sur un résultat avant une nouvelle recherche : il vise la liste affichée, pas la suivante
        if (searchPlay > 0) runPlaySearchResult(searchPlay);
        if (search.length()) {
            switch (searchKind) {
                case SearchKind::QobuzTracks: runSearch(search); break;
                case SearchKind::Deezer: runDeezerSearch(search); break;
                case SearchKind::QobuzFavorites: runQobuzFavoriteList(search); break;
                case SearchKind::DeezerFavorites: runDeezerFavoriteList(search); break;
                case SearchKind::QobuzAlbums: runQobuzAlbumSearch(search); break;
            }
        }
        if (save.wanted) runSaveFavorite(save);
        if (albumSave.wanted) runSaveAlbumFavorite(albumSave);
        if (pollNow || (int32_t)(millis() - nextPoll_) >= 0) pollOnce();
    }
}

// Demandes de la boucle principale (favoris, recherche) : relevées par run()

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

void WiimWorker::search(const String& query, SearchKind kind) {
    xSemaphoreTake(lock_, portMAX_DELAY);
    search_ = query;
    searchReq_ = kind;
    xSemaphoreGive(lock_);
    xTaskNotifyGive(task_);
}

void WiimWorker::saveAlbumFavorite(const String& id, const String& title, const String& artist, const String& image) {
    xSemaphoreTake(lock_, portMAX_DELAY);
    albumSave_ = AlbumFavRequest{true, id, title, artist, image};
    xSemaphoreGive(lock_);
    xTaskNotifyGive(task_);
}

void WiimWorker::playSearchResult(int index) {
    xSemaphoreTake(lock_, portMAX_DELAY);
    searchPlay_ = index + 1;  // 1 pour le premier résultat
    xSemaphoreGive(lock_);
    xTaskNotifyGive(task_);
}

void WiimWorker::refreshQobuzFavorites() {
    xSemaphoreTake(lock_, portMAX_DELAY);
    qobuzFavsWanted_ = true;
    xSemaphoreGive(lock_);
    xTaskNotifyGive(task_);
}
