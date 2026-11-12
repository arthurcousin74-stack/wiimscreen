#include "wiim_worker.h"

#include "discovery.h"

static constexpr uint32_t kPollPlayingMs = 2500;
static constexpr uint32_t kPollIdleMs = 4000;
static constexpr uint32_t kAfterCommandMs = 400;  // relire l'état peu après une commande
static const char* kVolPrefix = "setPlayerCmd:vol:";

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
    WiimError e = client_.command(cmd);
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
        bool presets = presetsWanted_, pollNow = pollWanted_;
        presetsWanted_ = pollWanted_ = false;
        xSemaphoreGive(lock_);
        if (presets) {
            WiimEvent ev;
            ev.kind = WiimEvent::Presets;
            ev.error = client_.presets(ev.presets);
            ev.ok = ev.error == WiimError::None;
            notePinning(ev.error);
            push(std::move(ev));
        }
        if (pollNow || (int32_t)(millis() - nextPoll_) >= 0) pollOnce();
    }
}
