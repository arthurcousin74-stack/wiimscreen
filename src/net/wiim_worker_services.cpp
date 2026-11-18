// Qobuz (accès direct facultatif, modèle de file) et Deezer (API publique, modèle de file airable) :
// recherches, favoris, lecture d'un album. Partie de la classe WiimWorker (wiim_worker.h).
#include "net/wiim_worker.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <algorithm>

#include "net/ca_bundle.h"
#include "storage/favorites.h"
#include "i18n.h"
#include "app/log.h"
#include "qobuz.h"
#include "upnp.h"
#include "voice.h"
#include "net/wiim_worker_internal.h"

using namespace wiimq;

static int deezerGet(const std::string& path, std::string& out, std::string& error);

// Recherche publique Deezer (api.deezer.com, sans clé) : morceaux avec leur album
static std::vector<wr::DeezerTrack> deezerSearch(const std::string& query, int limit, int& code) {
    std::string body, error;
    code = deezerGet("search?q=" + wr::urlQueryEncode(query) + "&limit=" + std::to_string(limit), body, error);
    return code == 200 && error.empty() ? wr::parseDeezerTracks(body) : std::vector<wr::DeezerTrack>();
}

// Modèle de file Deezer (« Deezer2 », passerelle airable) : celui gardé, sinon une file Deezer du WiiM
bool WiimWorker::deezerModel(const std::string& control, std::string& templ) {
    if (fav::sourceModel("Deezer2", templ) || fav::sourceModel("Deezer", templ)) return true;
    scanModels(control);
    return fav::sourceModel("Deezer2", templ) || fav::sourceModel("Deezer", templ);
}

// File lue sur le WiiM : gardée comme modèle Qobuz ou Deezer s'il en manque un (sans ses pistes)
void WiimWorker::noteModel(const wr::QueueInfo& q, const std::string& ctx) {
    if (q.sourceName == "Qobuz" && !fav::hasQobuzModel()) {
        if (fav::saveQobuzTemplate(ctx)) logEvent("Qobuz : mod\xC3\xA8le de file enregistr\xC3\xA9 (tuile Favoris Qobuz)");
    } else if ((q.sourceName == "Deezer2" || q.sourceName == "Deezer") && !fav::hasDeezerModel() &&
               !wr::urlOrigin(q.searchUrl).empty()) {
        if (fav::saveSourceModel(q.sourceName, ctx)) logEvent("Deezer : mod\xC3\xA8le de file copi\xC3\xA9 depuis le WiiM");
    }
}

// Une seule lecture de toutes les files du WiiM (lente : une requête par file) pour les deux modèles à la fois
void WiimWorker::scanModels(const std::string& control) {
    if (fav::hasQobuzModel() && fav::hasDeezerModel()) return;
    for (auto& n : queueNames(control, false)) {
        drainCommands();  // chaque lecture de file peut prendre des secondes : les appuis passent entre deux
        std::string ctx;
        if (browseQueue(control, n, ctx) != 200 || ctx.empty()) continue;
        noteModel(wr::parseQueueContext(ctx), ctx);
        if (fav::hasQobuzModel() && fav::hasDeezerModel()) break;
    }
}

// Recherche Deezer : le Tab5 cherche (api.deezer.com), le WiiM jouera l'album du morceau choisi
void WiimWorker::runDeezerSearch(const String& query) {
    WiimEvent ev;
    ev.kind = WiimEvent::SearchDone;
    ev.name = query;
    std::string control, templ;
    if (!findPlayQueue(control, false) || !deezerModel(control, templ)) {
        ev.text = wr::L("first start a Deezer album from the WiiM Home app (only once)", "lance d'abord un album Deezer depuis l'app WiiM Home (une seule fois)");
        push(std::move(ev));
        return;
    }
    int code = -1;
    const uint32_t t0 = millis();
    deezerResults_ = deezerSearch(query.c_str(), 40, code);
    logEvent("Recherche Deezer \xC2\xAB %s \xC2\xBB : %d, %u morceau(x) (%lu ms)", query.c_str(), code,
             (unsigned)deezerResults_.size(), (unsigned long)(millis() - t0));
    if (code != 200) {
        ev.text = wr::L("Deezer is not responding", "Deezer ne r\xC3\xA9pond pas");
        push(std::move(ev));
        return;
    }
    resultsFrom_ = ResultsFrom::Deezer;
    ev.ok = true;
    for (auto& t : deezerResults_) ev.tracks.push_back(wr::QueueTrack{t.title, t.artist, t.album, t.cover});
    push(std::move(ev));
}

void WiimWorker::runPlayDeezer(int i) {
    WiimEvent ev;
    ev.kind = WiimEvent::FavPlayed;
    std::string control, templ;
    if (i < 0 || i >= (int)deezerResults_.size() || !findPlayQueue(control, false) || !deezerModel(control, templ)) {
        ev.text = wr::L("Deezer track not found", "morceau Deezer introuvable");
        push(std::move(ev));
        return;
    }
    const wr::DeezerTrack& t = deezerResults_[i];
    const std::string base = wr::urlOrigin(wr::parseQueueContext(templ).searchUrl);
    ev.ok = playAlbumAt(control, templ, base + "/deezer/album/" + std::to_string(t.albumId), t.title, "Deezer");
    if (!ev.ok) ev.text = wr::L("the WiiM did not start this Deezer track", "le WiiM n'a pas lanc\xC3\xA9 ce morceau Deezer");
    push(std::move(ev));
}

// ---------------------------------------------------------------------------
// Qobuz en direct : le WiiM donne l'identifiant d'application et le jeton du compte lié (UPnP
// GetUserInfo, vérifié sur un WiiM Pro, 2026-11). Le Tab5 interroge alors Qobuz (recherche, favoris)
// sans toucher à la lecture. Le jeton reste en mémoire : jamais journalisé, affiché ni enregistré.

bool WiimWorker::qobuzAccess(const std::string& control, bool refresh) {
    // Accès direct non officiel (identifiant d'application et jeton Qobuz du WiiM) : seulement si on l'a choisi
    // (configuration › Avancé). Sinon, GetUserInfo n'est jamais appelé : le Tab5 ne détient aucun accès Qobuz.
    if (!cfg_.qobuzDirect) return false;
    if (haveQobuz_ && !refresh) return true;
    haveQobuz_ = false;
    std::string resp, result;
    const int c = upnpRequest(control, "GetUserInfo",
                              wr::soapBody(kPlayQueue, "GetUserInfo", {{"AccountSource", "Qobuz"}, {"RefreshToken", "0"}}),
                              resp);
    if (c == 200 && wr::xmlElement(resp, "Result", result)) haveQobuz_ = wr::parseQobuzAccess(wr::xmlUnescape(result), qobuz_);
    logEvent("Qobuz : acc\xC3\xA8s par le WiiM %s (GetUserInfo %d)",
             haveQobuz_ ? (refresh ? "renouvel\xC3\xA9" : "obtenu") : "indisponible", c);
    return haveQobuz_;
}

// GET https://www.qobuz.com/api.json/0.2/<path> ; nouvel accès demandé au WiiM une fois si Qobuz le refuse
int WiimWorker::qobuzGet(const std::string& control, const std::string& path, std::string& out) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!qobuzAccess(control, attempt > 0)) return -1;
        WiFiClientSecure tls;
        tls.setCACertBundle(ca_bundle_start, caBundleSize());
        tls.setHandshakeTimeout(8);
        HTTPClient http;
        http.setConnectTimeout(6000);
        http.setTimeout(10000);
        http.setReuse(false);
        if (!http.begin(tls, ("https://www.qobuz.com/api.json/0.2/" + path).c_str())) return -1;
        http.addHeader("X-App-Id", qobuz_.appId.c_str());
        http.addHeader("X-User-Auth-Token", qobuz_.token.c_str());
        const int code = http.GET();
        CapStream s(kMaxBody);
        if (code == 200) http.writeToStream(&s);
        http.end();
        out.swap(s.data);
        if (code != 401 && code != 403) return code;
    }
    return 401;
}


void WiimWorker::runQobuzFavorites() {
    std::string control, body, templ;
    if (!findPlayQueue(control, false)) return;
    WiimEvent ev;
    ev.kind = WiimEvent::QobuzFavorites;
    // Modèle de file Qobuz (tuile « Favoris Qobuz », lecture des résultats) : pris dans les files du WiiM
    const bool hadModel = fav::hasQobuzModel();
    // Modèle absent : lecture de toutes les files du WiiM (lente). Une fois après le démarrage, puis à intervalle
    // croissant (30 min, 1 h… 24 h) : un compte sans Deezer (ou sans Qobuz) n'en aura jamais. Un modèle vient aussi
    // de la file qui joue (relevé) et d'une recherche ou d'une tuile (qobuzModel, deezerModel).
    const bool missing = !hadModel || !fav::hasDeezerModel();
    const bool scan = missing && (int32_t)(millis() - modelScanNext_) >= 0;
    if (scan) {
        modelScanGap_ = modelScanGap_ ? std::min<uint32_t>(modelScanGap_ * 2, 24UL * 3600 * 1000) : 30UL * 60 * 1000;
        modelScanNext_ = millis() + modelScanGap_;
        scanModels(control);
    }
    // Sans file Qobuz sur le WiiM : favoris enregistrés, compte du WiiM (accès direct seulement)
    const bool model = fav::hasQobuzModel() || (scan && qobuzModel(control, templ, false));
    if (qobuzFavDenied_ || !cfg_.qobuzDirect) {  // refusé par Qobuz, ou accès direct non choisi : pas d'essai
        ev.ok = model && !hadModel;  // seulement pour afficher la tuile « Favoris Qobuz »
        if (ev.ok) push(std::move(ev));
        return;
    }
    const int code = qobuzGet(control, "favorite/getUserFavorites?type=albums&limit=50", body);
    if (code == 401 || code == 403) qobuzFavDenied_ = true;
    ev.ok = code == 200 || (model && !hadModel);
    if (code == 200) ev.albums = wr::parseQobuzAlbums(body);
    logEvent("Qobuz : albums favoris %d, %u%s", code, (unsigned)ev.albums.size(),
             qobuzFavDenied_ ? " (Qobuz refuse l'acc\xC3\xA8s du WiiM aux favoris : pas de tuiles d'album)" : "");
    push(std::move(ev));
}

// GET https://api.deezer.com/<path> (API publique, HTTPS vérifié). Deezer répond 200 avec {"error":…} quand il
// refuse (profil privé, jeton refusé) : `error` reçoit alors son message.
static int deezerGet(const std::string& path, std::string& out, std::string& error) {
    WiFiClientSecure tls;
    tls.setCACertBundle(ca_bundle_start, caBundleSize());
    tls.setHandshakeTimeout(8);
    HTTPClient http;
    http.setConnectTimeout(6000);
    http.setTimeout(10000);
    http.setReuse(false);
    int code = -1;
    out.clear();
    error.clear();
    if (http.begin(tls, ("https://api.deezer.com/" + path).c_str())) {
        code = http.GET();
        CapStream s(kMaxBody);
        if (code == 200) http.writeToStream(&s);
        http.end();
        out.swap(s.data);
        const std::string e = wr::jsonTop(out, "error");
        if (!e.empty()) error = wr::jsonText(wr::jsonTop(e, "message"));
        if (!e.empty() && error.empty()) error = wr::L("refused by Deezer", "refus de Deezer");
    }
    return code;
}

// Favoris Deezer : profil public donné dans la configuration (api.deezer.com/user/<numéro>, sans clé), sinon rien.
// (Le WiiM ne donne pas de jeton Deezer : GetUserInfo(Deezer2) répond avec l'accès airable du WiiM, qu'on ne lit pas.)
std::string WiimWorker::deezerUserPath() const {
    return cfg_.deezerUser.length() ? "user/" + std::string(cfg_.deezerUser.c_str()) : "";
}

void WiimWorker::runDeezerFavorites() {
    const std::string user = deezerUserPath();
    WiimEvent ev;
    ev.kind = WiimEvent::DeezerFavorites;
    if (!user.empty()) {
        std::string body, err;
        const int code = deezerGet(user + "/albums?limit=50", body, err);
        ev.albums = wr::parseDeezerAlbums(body);
        ev.ok = code == 200 && err.empty();
        logEvent("Deezer : albums favoris %d, %u%s%s", code, (unsigned)ev.albums.size(), err.empty() ? "" : " : ",
                 err.c_str());
    }
    push(std::move(ev));
}

// Morceaux favoris Deezer, en liste (rien ne joue avant le choix) ; un appui joue l'album du morceau, à ce morceau
void WiimWorker::runDeezerFavoriteList(const String& title) {
    WiimEvent ev;
    ev.kind = WiimEvent::SearchDone;
    ev.name = title;
    const std::string user = deezerUserPath();
    if (user.empty()) {
        ev.text = wr::L("enter your Deezer profile on the setup page", "indique ton profil Deezer dans la page de configuration");
        push(std::move(ev));
        return;
    }
    std::string body, err;
    const int code = deezerGet(user + "/tracks?limit=100", body, err);
    deezerResults_ = wr::parseDeezerTracks(body);
    resultsFrom_ = ResultsFrom::Deezer;
    logEvent("Favoris Deezer : %d, %u morceau(x)%s%s", code, (unsigned)deezerResults_.size(), err.empty() ? "" : " : ",
             err.c_str());
    ev.ok = code == 200 && err.empty();
    if (!ev.ok) ev.text = err.empty() ? wr::L("Deezer is not responding", "Deezer ne r\xC3\xA9pond pas") : wr::L("Deezer profile private or not found (", "profil Deezer priv\xC3\xA9 ou introuvable (") + String(err.c_str()) + ")";
    for (auto& t : deezerResults_) ev.tracks.push_back(wr::QueueTrack{t.title, t.artist, t.album, t.cover});
    push(std::move(ev));
}

void WiimWorker::runPlayDeezerAlbum(const std::string& albumId) {
    WiimEvent ev;
    ev.kind = WiimEvent::FavPlayed;
    std::string control, templ;
    if (!findPlayQueue(control, false) || !deezerModel(control, templ)) {
        ev.text = wr::L("first start a Deezer album from the WiiM Home app", "lance d'abord un album Deezer depuis l'app WiiM Home");
        push(std::move(ev));
        return;
    }
    const std::string base = wr::urlOrigin(wr::parseQueueContext(templ).searchUrl);
    ev.ok = playAlbumAt(control, templ, base + "/deezer/album/" + albumId, "", "Album Deezer");
    if (!ev.ok) ev.text = wr::L("the WiiM did not start this album", "le WiiM n'a pas lanc\xC3\xA9 cet album");
    push(std::move(ev));
}

// Album favori Qobuz : file de l'album lancée au premier morceau
void WiimWorker::runPlayQobuzAlbum(const std::string& albumId) {
    WiimEvent ev;
    ev.kind = WiimEvent::FavPlayed;
    std::string control, templ;
    if (!findPlayQueue(control, false) || !qobuzModel(control, templ)) {
        ev.text = wr::L("first start a Qobuz album from the WiiM Home app", "lance d'abord un album Qobuz depuis l'app WiiM Home");
        push(std::move(ev));
        return;
    }
    ev.ok = playAlbumAt(control, templ, wr::qobuzAlbumUrl(albumId), "", "Album Qobuz");
    if (!ev.ok) ev.text = wr::L("the WiiM did not start this album", "le WiiM n'a pas lanc\xC3\xA9 cet album");
    push(std::move(ev));
}

// Recherche d'albums Qobuz : le Tab5 interroge Qobuz (album/search, accès donné par le WiiM), rien ne joue
void WiimWorker::runQobuzAlbumSearch(const String& query) {
    resultsFrom_ = ResultsFrom::QobuzAlbums;
    WiimEvent ev;
    ev.kind = WiimEvent::SearchDone;
    ev.name = query;
    std::string control, body;
    qobuzAlbums_.clear();
    if (!cfg_.qobuzDirect) {  // le WiiM ne remplit pas une file depuis une recherche d'albums (vérifié, 2026-11-16)
        ev.text = wr::L("album search needs the direct Qobuz search (setup page \xE2\x80\xBA Advanced)",
                        "la recherche d'albums demande la recherche Qobuz directe (configuration \xE2\x80\xBA Avanc\xC3\xA9)");
        push(std::move(ev));
        return;
    }
    const uint32_t t0 = millis();
    const int code = findPlayQueue(control, false)
                         ? qobuzGet(control, "album/search?query=" + wr::urlQueryEncode(query.c_str()) + "&limit=50", body)
                         : -1;
    if (code == 200) qobuzAlbums_ = wr::parseQobuzAlbums(body);
    logEvent("Recherche d'albums Qobuz \xC2\xAB %s \xC2\xBB : %d, %u album(s) (%lu ms)", query.c_str(), code,
             (unsigned)qobuzAlbums_.size(), (unsigned long)(millis() - t0));
    ev.ok = code == 200;
    if (!ev.ok) ev.text = code < 0 ? wr::L("no Qobuz access through the WiiM", "pas d'acc\xC3\xA8s \xC3\xA0 Qobuz par le WiiM") : wr::L("Qobuz is not responding", "Qobuz ne r\xC3\xA9pond pas");
    ev.albums = qobuzAlbums_;
    for (auto& a : qobuzAlbums_)
        ev.tracks.push_back(wr::QueueTrack{a.title, a.artist, a.tracks > 1 ? std::to_string(a.tracks) + wr::L(" tracks", " pistes") : a.tracks == 1 ? wr::L("1 track", "1 piste") : "",
                                           a.image});
    push(std::move(ev));
}

// Modèle de file Qobuz : celui gardé, sinon une file Qobuz du WiiM, sinon un favori Qobuz
bool WiimWorker::qobuzModel(const std::string& control, std::string& templ, bool scanQueues) {
    if (fav::qobuzTemplate(templ)) return true;
    if (scanQueues) scanModels(control);
    if (fav::qobuzTemplate(templ)) return true;
    for (auto& f : fav::list())
        if (f.source == "Qobuz" && fav::context(f.id, templ)) return true;
    // Aucune file Qobuz connue : modèle construit avec le nom du compte Qobuz lié au WiiM (GetUserInfo ;
    // vérifié sur un WiiM, 2026-11-16 : la file des morceaux favoris construite ainsi se remplit et joue)
    if (qobuzAccess(control, false) && !qobuz_.user.empty()) {
        templ = wr::qobuzQueueModel(qobuz_.user);
        fav::saveQobuzTemplate(templ);
        logEvent("Qobuz : mod\xC3\xA8le de file construit depuis le compte du WiiM");
        return true;
    }
    return false;
}
