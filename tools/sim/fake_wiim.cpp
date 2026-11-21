// Simulator: a fake WiiM behind the real WiimWorker interface (src/net/wiim_worker.h).
//
// main.cpp talks to it exactly as to the real background task: commands, reports, events. The fake keeps a small
// music catalog, plays it in "real time" (position, next track at the end), answers presets, Qobuz/Deezer favorites,
// searches and favorites with realistic delays. Special searches for testing: « erreur » (the WiiM does not answer),
// « rien » (no result).
#include <map>
#include <mutex>
#include <thread>

#include "storage/favorites.h"
#include "i18n.h"
#include "app/log.h"
#include "pc.h"
#include "voice.h"
#include "net/wiim_worker.h"

namespace {

struct Track {
    const char* title;
    int seconds;
};
struct Album {
    const char* id;
    const char* artist;
    const char* title;
    int hue, pattern;
    std::vector<Track> tracks;  // empty: radio (no duration)
    std::string art() const { return "sim://cover/" + std::to_string(hue) + "/" + std::to_string(pattern); }
};

const std::vector<Album>& catalog() {
    static const std::vector<Album> c = {
        {"q-kob", "Miles Davis", "Kind of Blue", 215, 0,
         {{"So What", 562}, {"Freddie Freeloader", 586}, {"Blue in Green", 337}, {"All Blues", 693}, {"Flamenco Sketches", 566}}},
        {"q-dis", "Daft Punk", "Discovery", 300, 1,
         {{"One More Time", 320}, {"Aerodynamic", 212}, {"Digital Love", 301}, {"Harder, Better, Faster, Stronger", 224},
          {"Something About Us", 232}}},
        {"q-ls", "Aretha Franklin", "Lady Soul", 25, 2,
         {{"Chain of Fools", 165}, {"Money Won't Change You", 128}, {"People Get Ready", 222},
          {"(You Make Me Feel Like) A Natural Woman", 166}}},
        {"q-rc", "Stromae", "Racine carr\xC3\xA9" "e", 45, 0,
         {{"Ta f\xC3\xAAte", 183}, {"Papaoutai", 232}, {"Tous les m\xC3\xAAmes", 193}, {"Formidable", 214}}},
        {"q-ns", "Nina Simone", "I Put a Spell on You", 160, 2,
         {{"I Put a Spell on You", 156}, {"Feeling Good", 177}, {"Ne me quitte pas", 210}}},
        {"d-ram", "Daft Punk", "Random Access Memories", 35, 1,
         {{"Give Life Back to Music", 274}, {"Instant Crush", 337}, {"Get Lucky", 369}}},
        {"d-amy", "Amy Winehouse", "Back to Black", 0, 2,
         {{"Rehab", 214}, {"You Know I'm No Good", 256}, {"Back to Black", 241}}},
        {"r-fip", "", "FIP", 330, 1, {}},
        {"r-rsj", "", "Radio Swiss Jazz", 190, 0, {}},
    };
    return c;
}
int albumIndex(const std::string& id) {
    for (size_t i = 0; i < catalog().size(); ++i)
        if (id == catalog()[i].id) return (int)i;
    return -1;
}

// Presets of the fake WiiM (slot → album)
const std::vector<std::pair<int, const char*>> kPresets = {{1, "r-fip"}, {2, "r-rsj"}, {3, "q-kob"},
                                                             {4, "q-dis"}, {5, "q-ls"},  {6, "q-rc"}};

struct Player {
    int album = 0, track = 1;
    bool playing = true, muted = false;
    int volume = 32;
    uint32_t posMs = 95000;
    uint32_t loadingUntil = 0;
    uint32_t lastTick = 0;
    std::vector<std::pair<int, int>> results;  // last list shown: (album, track)
};
Player P;

uint32_t durationMs() {
    const Album& a = catalog()[P.album];
    return a.tracks.empty() ? 0 : (uint32_t)a.tracks[P.track].seconds * 1000;
}

void play(int album, int track) {
    P.album = album;
    P.track = catalog()[album].tracks.empty() ? 0 : std::max(0, std::min(track, (int)catalog()[album].tracks.size() - 1));
    P.posMs = 0;
    P.playing = true;
    P.loadingUntil = millis() + 900;  // the WiiM loads the queue first
}

void tick() {
    const uint32_t now = millis();
    const uint32_t dt = P.lastTick ? now - P.lastTick : 0;
    P.lastTick = now;
    if (!P.playing || now < P.loadingUntil) return;
    P.posMs += dt;
    const uint32_t d = durationMs();
    if (d && P.posMs >= d) {
        P.posMs = 0;
        P.track = (P.track + 1) % (int)catalog()[P.album].tracks.size();
    }
}

WiimReport report(uint32_t seq) {
    WiimReport r;
    r.online = true;
    r.cmdSeq = seq;
    r.at = millis();
    const Album& a = catalog()[P.album];
    const bool radio = a.tracks.empty();
    auto& p = r.player;
    p.ok = true;
    p.playing = P.playing;
    p.loading = millis() < P.loadingUntil;
    p.volume = P.volume;
    p.muted = P.muted;
    p.mode = radio ? 0 : 0;
    p.positionMs = radio ? 0 : P.posMs;
    p.durationMs = durationMs();
    p.title = radio ? "Morceau en direct sur " + std::string(a.title) : a.tracks[P.track].title;
    p.artist = radio ? "Artiste de la radio" : a.artist;
    p.album = radio ? "" : a.title;
    r.metaOk = true;
    r.meta.ok = true;
    r.meta.title = p.title;
    r.meta.artist = p.artist;
    r.meta.album = p.album;
    r.meta.artUrl = a.art();
    r.meta.sampleRate = radio ? 48000 : 96000;
    r.meta.bitDepth = radio ? 16 : 24;
    if (!radio) r.queueKey = wr::qobuzAlbumKey(a.id);  // same key as the favorites of this album
    return r;
}

std::vector<wr::QobuzAlbum> albumsOf(const char* prefix) {
    std::vector<wr::QobuzAlbum> out;
    for (auto& a : catalog())
        if (std::string(a.id).compare(0, 2, prefix) == 0 && !a.tracks.empty())
            out.push_back(wr::QobuzAlbum{a.id, a.title, a.artist, a.art(), (int)a.tracks.size()});
    return out;
}

}  // namespace

const char* wiimErrorName(WiimError e) {
    switch (e) {
        case WiimError::None: return "OK";
        case WiimError::Network: return "r\xC3\xA9seau";
        case WiimError::PinMismatch: return "certificat";
        case WiimError::Http: return "HTTP";
        case WiimError::Refused: return "refus";
    }
    return "?";
}

void WiimWorker::begin(const Settings& s) {
    cfg_ = s;
    lock_ = xSemaphoreCreateMutex();
    P.lastTick = millis();
    xTaskCreatePinnedToCore(taskMain, "wiim", 8192, this, 1, &task_, 0);
}

void WiimWorker::taskMain(void* self) { static_cast<WiimWorker*>(self)->run(); }

void WiimWorker::command(const String& cmd) {
    xSemaphoreTake(lock_, portMAX_DELAY);
    ++queued_;
    cmds_.push_back(cmd);
    xSemaphoreGive(lock_);
    xTaskNotifyGive(task_);
}

#define SIM_REQUEST(field, value)          \
    xSemaphoreTake(lock_, portMAX_DELAY);  \
    field = value;                         \
    xSemaphoreGive(lock_);                 \
    xTaskNotifyGive(task_)

void WiimWorker::refreshPresets() { SIM_REQUEST(presetsWanted_, true); }
void WiimWorker::pollSoon() { SIM_REQUEST(pollWanted_, true); }
void WiimWorker::checkQueue() { SIM_REQUEST(queueWanted_, true); }
void WiimWorker::refreshQobuzFavorites() { SIM_REQUEST(qobuzFavsWanted_, true); }
void WiimWorker::playFavorite(const String& id) { SIM_REQUEST(favPlay_, id); }
void WiimWorker::playSearchResult(int index) { SIM_REQUEST(searchPlay_, index + 1); }
void WiimWorker::saveFavorite(const String& title, const String& artist, const String& album, const String& artUrl) {
    SIM_REQUEST(favSave_, (FavRequest{true, title, artist, album, artUrl}));
}
void WiimWorker::saveAlbumFavorite(const String& id, const String& title, const String& artist, const String& image) {
    SIM_REQUEST(albumSave_, (AlbumFavRequest{true, id, title, artist, image}));
}
void WiimWorker::search(const String& query, SearchKind kind) {
    xSemaphoreTake(lock_, portMAX_DELAY);
    search_ = query;
    searchReq_ = kind;
    xSemaphoreGive(lock_);
    xTaskNotifyGive(task_);
}

bool WiimWorker::takeReport(WiimReport& out) {
    xSemaphoreTake(lock_, portMAX_DELAY);
    const bool has = hasReport_;
    if (has) out = report_;
    hasReport_ = false;
    xSemaphoreGive(lock_);
    return has;
}

bool WiimWorker::takeEvent(WiimEvent& out) {
    xSemaphoreTake(lock_, portMAX_DELAY);
    const bool has = !events_.empty();
    if (has) {
        out = std::move(events_.front());
        events_.pop_front();
    }
    xSemaphoreGive(lock_);
    return has;
}

void WiimWorker::push(WiimEvent&& e) {
    xSemaphoreTake(lock_, portMAX_DELAY);
    events_.push_back(std::move(e));
    xSemaphoreGive(lock_);
}

void WiimWorker::execute(const String& cmd) {
    logEvent("WiiM <- %s : OK (simul\xC3\xA9)", cmd.c_str());
    const String c = cmd.startsWith("setPlayerCmd:") ? cmd.substring(13) : cmd;
    if (c == "pause" || c == "stop") P.playing = false;
    else if (c == "resume" || c == "play") P.playing = true;
    else if (c == "onepause") P.playing = !P.playing;
    else if (c == "next" || c == "prev") {
        const auto& t = catalog()[P.album].tracks;
        if (!t.empty()) P.track = (P.track + (c == "next" ? 1 : (int)t.size() - 1)) % (int)t.size();
        P.posMs = 0;
        P.playing = true;
        P.loadingUntil = millis() + 400;
    } else if (c.startsWith("vol:")) P.volume = constrain((int)c.substring(4).toInt(), 0, 100), P.muted = false;
    else if (c.startsWith("mute:")) P.muted = c.substring(5) == "1";
    else if (c.startsWith("seek:")) P.posMs = (uint32_t)c.substring(5).toInt() * 1000;
    else if (cmd.startsWith("MCUKeyShortClick:")) {
        const int slot = (int)cmd.substring(17).toInt();
        for (auto& p : kPresets)
            if (p.first == slot) play(albumIndex(p.second), 0);
    }
    WiimEvent ev;
    ev.kind = WiimEvent::CommandDone;
    ev.text = cmd;
    ev.ok = true;
    push(std::move(ev));
}

void WiimWorker::drainCommands() {
    for (;;) {
        xSemaphoreTake(lock_, portMAX_DELAY);
        if (cmds_.empty()) {
            xSemaphoreGive(lock_);
            return;
        }
        String cmd = cmds_.front();
        cmds_.pop_front();
        xSemaphoreGive(lock_);
        execute(cmd);
        xSemaphoreTake(lock_, portMAX_DELAY);
        ++executed_;
        xSemaphoreGive(lock_);
    }
}

void WiimWorker::pollOnce() {
    xSemaphoreTake(lock_, portMAX_DELAY);
    const uint32_t seq = executed_;
    xSemaphoreGive(lock_);
    tick();
    WiimReport r = report(seq);
    xSemaphoreTake(lock_, portMAX_DELAY);
    report_ = r;
    hasReport_ = true;
    xSemaphoreGive(lock_);
    nextPoll_ = millis() + (P.playing ? 1000 : 2000);
}

void WiimWorker::pause(uint32_t ms, bool poll) {
    const uint32_t until = millis() + ms;
    while ((int32_t)(millis() - until) < 0) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
        drainCommands();
        if (poll && (int32_t)(millis() - nextPoll_) >= 0) pollOnce();
    }
}

void WiimWorker::run() {
    pollOnce();
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
        drainCommands();
        xSemaphoreTake(lock_, portMAX_DELAY);
        const bool presets = presetsWanted_, pollNow = pollWanted_, queue = queueWanted_, qfavs = qobuzFavsWanted_;
        presetsWanted_ = pollWanted_ = queueWanted_ = qobuzFavsWanted_ = false;
        FavRequest save = favSave_;
        favSave_.wanted = false;
        const String play = favPlay_, search = search_;
        favPlay_ = search_ = "";
        const SearchKind source = searchReq_;
        const int searchPlay = searchPlay_;
        searchPlay_ = 0;
        const AlbumFavRequest albumSave = albumSave_;
        albumSave_.wanted = false;
        xSemaphoreGive(lock_);

        if (presets) {
            pause(300, false);
            WiimEvent ev;
            ev.kind = WiimEvent::Presets;
            ev.ok = true;
            for (auto& p : kPresets) {
                const Album& a = catalog()[albumIndex(p.second)];
                ev.presets.push_back(wr::Preset{p.first, a.title, a.art(), ""});
            }
            logEvent("Presets du WiiM : OK, %u (simul\xC3\xA9s)", (unsigned)ev.presets.size());
            push(std::move(ev));
        }
        if (queue) logEvent("File du WiiM : simulateur (pas de service UPnP)");
        if (qfavs) runQobuzFavorites(), runDeezerFavorites();
        if (play.length()) runPlayFavorite(play);
        if (search.length()) {
            if (source == SearchKind::Deezer) runDeezerSearch(search);
            else if (source == SearchKind::QobuzFavorites) runQobuzFavoriteList(search);
            else if (source == SearchKind::DeezerFavorites) runDeezerFavoriteList(search);
            else if (source == SearchKind::QobuzAlbums) runQobuzAlbumSearch(search);
            else runSearch(search);
        }
        if (searchPlay > 0) runPlaySearchResult(searchPlay);
        if (save.wanted) runSaveFavorite(save);
        if (albumSave.wanted) runSaveAlbumFavorite(albumSave);
        if (pollNow || (int32_t)(millis() - nextPoll_) >= 0) pollOnce();
    }
}

void WiimWorker::runQobuzFavorites() {
    pause(500, true);
    WiimEvent ev;
    ev.kind = WiimEvent::QobuzFavorites;
    ev.ok = true;
    ev.albums = albumsOf("q-");
    logEvent("Qobuz : albums favoris 200, %u (simul\xC3\xA9s)", (unsigned)ev.albums.size());
    push(std::move(ev));
}

void WiimWorker::runDeezerFavorites() {
    if (!cfg_.deezerUser.length()) return;
    pause(400, true);
    WiimEvent ev;
    ev.kind = WiimEvent::DeezerFavorites;
    ev.ok = true;
    ev.albums = albumsOf("d-");
    logEvent("Deezer : albums favoris 200, %u (simul\xC3\xA9s)", (unsigned)ev.albums.size());
    push(std::move(ev));
}

// Search results, favorite track lists: a list of (album, track), shown without playing
static WiimEvent listEvent(const String& title, const std::vector<std::pair<int, int>>& list, bool ok,
                           const char* error) {
    P.results = list;
    WiimEvent ev;
    ev.kind = WiimEvent::SearchDone;
    ev.name = title;
    ev.ok = ok;
    if (!ok) ev.text = error;
    for (auto& r : list) {
        const Album& a = catalog()[r.first];
        ev.tracks.push_back(wr::QueueTrack{a.tracks[r.second].title, a.artist, a.title, a.art()});
    }
    return ev;
}

// Album search: catalog albums sharing a word with the query (« erreur » shows the error case)
void WiimWorker::runQobuzAlbumSearch(const String& query) {
    pause(700, true);
    const std::string q = wr::normalizeText(query.c_str());
    WiimEvent ev;
    ev.kind = WiimEvent::SearchDone;
    ev.name = query;
    P.results.clear();
    qobuzAlbums_.clear();
    for (size_t i = 0; i < catalog().size(); ++i) {
        const Album& a = catalog()[i];
        if (a.tracks.empty() || !wr::sharesWord(q, {a.artist, a.title})) continue;
        qobuzAlbums_.push_back(wr::QobuzAlbum{a.id, a.title, a.artist, a.art(), (int)a.tracks.size()});
        P.results.push_back({(int)i, 0});
        ev.tracks.push_back(wr::QueueTrack{a.title, a.artist, std::to_string(a.tracks.size()) + wr::L(" tracks", " pistes"), a.art()});
    }
    ev.ok = q != "erreur";
    if (!ev.ok) {
        ev.text = wr::L("Qobuz is not responding", "Qobuz ne r\xC3\xA9pond pas");
        ev.tracks.clear();
        qobuzAlbums_.clear();
    }
    ev.albums = qobuzAlbums_;
    logEvent("Recherche d'albums Qobuz (simul\xC3\xA9" "e) \xC2\xAB %s \xC2\xBB : %u album(s)", query.c_str(),
             (unsigned)qobuzAlbums_.size());
    push(std::move(ev));
}

void WiimWorker::runSaveAlbumFavorite(const AlbumFavRequest& r) {
    pause(300, true);
    WiimEvent ev;
    ev.kind = WiimEvent::FavSaved;
    wr::Favorite f;
    f.key = wr::qobuzAlbumKey(r.id.c_str());
    f.title = r.title.c_str();
    f.artist = r.artist.c_str();
    f.source = "Qobuz";
    f.artUrl = r.image.c_str();
    std::string err;
    ev.ok = fav::add(f, r.id.c_str(), err);  // simulator: the context is the album id
    ev.text = ev.ok ? r.title : String(err.c_str());
    logEvent("Favori \xC2\xAB %s \xC2\xBB (album Qobuz depuis la recherche, simul\xC3\xA9) : %s", f.title.c_str(),
             ev.ok ? "OK" : err.c_str());
    push(std::move(ev));
}

void WiimWorker::runSearch(const String& query) {
    pause(1200, true);
    const std::string q = wr::normalizeText(query.c_str());
    std::vector<std::pair<int, int>> found;
    for (size_t i = 0; i < catalog().size(); ++i) {
        const Album& a = catalog()[i];
        for (size_t k = 0; k < a.tracks.size(); ++k)
            if (wr::sharesWord(q, {a.tracks[k].title, a.artist, a.title})) found.push_back({(int)i, (int)k});
    }
    logEvent("Recherche (simul\xC3\xA9" "e) \xC2\xAB %s \xC2\xBB : %u morceau(x)", query.c_str(), (unsigned)found.size());
    const bool fail = q == "erreur";
    push(listEvent(query, fail ? std::vector<std::pair<int, int>>{} : found, !fail, wr::L("the WiiM is not responding", "le WiiM ne r\xC3\xA9pond pas")));
}

void WiimWorker::runDeezerSearch(const String& query) { runSearch(query); }

void WiimWorker::runQobuzFavoriteList(const String& title) {
    pause(800, true);
    std::vector<std::pair<int, int>> list;
    for (const char* id : {"q-kob", "q-dis", "q-ls", "q-ns"})
        for (int k = 0; k < 2; ++k) list.push_back({albumIndex(id), k});
    logEvent("Favoris Qobuz : %u morceau(x), liste sans lecture (simul\xC3\xA9s)", (unsigned)list.size());
    push(listEvent(title, list, true, ""));
}

void WiimWorker::runDeezerFavoriteList(const String& title) {
    pause(600, true);
    std::vector<std::pair<int, int>> list;
    for (const char* id : {"d-ram", "d-amy"})
        for (int k = 0; k < 3; ++k) list.push_back({albumIndex(id), k});
    logEvent("Favoris Deezer : 200, %u morceau(x) (simul\xC3\xA9s)", (unsigned)list.size());
    push(listEvent(title, list, true, ""));
}

void WiimWorker::runPlaySearchResult(int index) {
    WiimEvent ev;
    ev.kind = WiimEvent::FavPlayed;
    if (index >= 1 && index <= (int)P.results.size()) {
        pause(700, true);
        play(P.results[index - 1].first, P.results[index - 1].second);
        ev.ok = true;
    } else {
        ev.text = wr::L("track not found", "morceau introuvable");
    }
    push(std::move(ev));
    nextPoll_ = millis() + 300;
}

void WiimWorker::runPlayFavorite(const String& id) {
    WiimEvent ev;
    ev.kind = WiimEvent::FavPlayed;
    // Liste des favoris (Tout lire, Aléatoire) : la liste affichée, du premier morceau ou d'un au hasard
    if (id.startsWith("qobuz-favoris:") || id.startsWith("deezer-favoris:")) {
        const bool shuffle = id.endsWith(":shuffle");
        pause(600, true);
        if (!P.results.empty()) {
            const size_t k = shuffle ? esp_random() % P.results.size() : 0;
            play(P.results[k].first, P.results[k].second);
            ev.ok = true;
        } else {
            ev.text = wr::L("the WiiM did not start the list", "le WiiM n'a pas lanc\xC3\xA9 la liste");
        }
        logTagged("favlist.play", "deezer=%d shuffle=%d ok=%d | Liste des favoris (simul\xC3\xA9" "e)",
                  id.startsWith("deezer") ? 1 : 0, shuffle ? 1 : 0, ev.ok ? 1 : 0);
        push(std::move(ev));
        nextPoll_ = millis() + 300;
        return;
    }
    std::string albumId;
    if (id.startsWith("qobuz-album:")) albumId = id.substring(12).c_str();
    else if (id.startsWith("deezer-album:")) albumId = id.substring(13).c_str();
    else fav::context(id.c_str(), albumId);
    const int a = albumIndex(albumId);
    pause(600, true);
    if (a >= 0) {
        play(a, 0);
        ev.ok = true;
        logEvent("Favori \xE2\x96\xB6 \xC2\xAB %s \xC2\xBB (simul\xC3\xA9)", catalog()[a].title);
    } else {
        ev.text = wr::L("favorite not found", "favori introuvable");
    }
    push(std::move(ev));
    nextPoll_ = millis() + 300;
}

void WiimWorker::runSaveFavorite(const FavRequest& r) {
    pause(500, true);
    WiimEvent ev;
    ev.kind = WiimEvent::FavSaved;
    const Album& a = catalog()[P.album];
    if (a.tracks.empty()) {
        ev.text = wr::L("a radio station cannot be a favorite", "une radio ne peut pas \xC3\xAAtre un favori");
    } else {
        wr::Favorite f;
        f.key = wr::qobuzAlbumKey(a.id);  // same key as an album saved from the album search
        f.title = a.title;
        f.artist = a.artist;
        f.source = "Qobuz";
        f.artUrl = a.art();
        std::string err;
        ev.ok = fav::add(f, a.id, err);
        ev.text = ev.ok ? String(a.title) : String(err.c_str());
    }
    (void)r;
    push(std::move(ev));
}
