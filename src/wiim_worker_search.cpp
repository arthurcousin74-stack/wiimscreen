// Recherche Qobuz par une file du WiiM : relevé et remise de la musique d'avant, son coupé et lecture aléatoire
// retirée pendant la recherche (searchGuardBegin/End), liste des favoris Qobuz, lecture d'un résultat.
// Partie de la classe WiimWorker (wiim_worker.h).
#include "wiim_worker.h"

#include "decisions.h"
#include "favorites.h"
#include "i18n.h"
#include "log.h"
#include "qobuz.h"
#include "upnp.h"
#include "voice.h"
#include "wiim_worker_internal.h"

using namespace wiimq;

static bool readQueueStable(WiimWorker* w, const std::string& control, const std::string& name, int seconds,
                            wr::QueueInfo& out, bool poll = false);

// ---------------------------------------------------------------------------
// Recherche Qobuz sans clé : le WiiM lit lui-même les résultats d'une file dont l'adresse est une
// recherche de morceaux (vérifié sur un WiiM, 2026-11). La file est relue pour afficher les résultats.

static const char* kSearchQueue = "wiimscreen-recherche";

// Le WiiM ajoute les pistes peu à peu (1 ou 2 après 1 s, 7 après 6 s) : on relit la file jusqu'à ce que
// leur nombre ne bouge plus (deux lectures égales), dans la limite de `seconds`
static bool readQueueStable(WiimWorker* w, const std::string& control, const std::string& name, int seconds,
                            wr::QueueInfo& out, bool poll) {
    wr::QueueStability stability;  // règle : lib/core/src/decisions.h
    const uint32_t start = millis(), until = start + seconds * 1000UL;
    while ((int32_t)(millis() - until) < 0) {
        w->pause(700, poll);
        std::string back;
        if (browseQueue(control, name, back) != 200 || back.empty()) continue;
        wr::QueueInfo q = wr::parseQueueContext(back);
        if (stability.feed(q.tracks, millis() - start)) break;
        out = q;
    }
    return out.tracks > 0;
}


// Ce qui joue avant une recherche Qobuz : le WiiM lance la file de recherche dès qu'elle est créée
// (même avec QueuePolicy 0, constaté sur un WiiM) ; on remet ensuite la musique d'avant.
WiimWorker::Playback WiimWorker::snapshot(const std::string& control) {
    Playback p;
    wr::PlayerStatus st;
    wr::TrackMeta m;
    if (client_.status(st) != WiimError::None) return p;
    p.known = true;
    p.playing = st.playing;
    p.muted = st.muted;
    p.posMs = st.positionMs;
    p.loop = st.loop;
    if (client_.metaInfo(m) == WiimError::None) p.title = m.title;
    // File en cours (« CurrentQueue ») et place du morceau dedans
    std::string ctx;
    if (!p.title.empty() && browseQueue(control, "CurrentQueue", ctx) == 200 && !ctx.empty()) {
        const wr::QueueInfo q = wr::parseQueueContext(ctx, wr::kQueueTitlesMax);
        p.index = wr::queueIndexOf(q, p.title, false);
        if (p.index > 0) {
            p.ctx = ctx;
            p.listName = q.listName;
        }
    }
    logTagged("search.before", "playing=%d inqueue=%d | Avant la recherche : %s \xC2\xAB %s \xC2\xBB%s",
              p.playing ? 1 : 0, p.index ? 1 : 0, p.playing ? "lecture" : "arr\xC3\xAAt", p.title.c_str(),
              p.index ? " (file connue)" : "");
    return p;
}

// Lecture aléatoire : CreateQueue mélange la file de recherche (résultats dans le désordre : « Children » de
// Robert Miles en tête pour « miles ») et la remise de la musique d'avant doit chercher son morceau. Le WiiM garde
// le morceau et la position quand on change ce mode (vu sur un WiiM, 2026-11-17).
bool WiimWorker::shuffleOff(const Playback& before) {
    const int plain = wr::loopWithoutShuffle(before.loop);
    if (plain < 0) return false;
    return client_.command("setPlayerCmd:loopmode:" + String(plain)) == WiimError::None;
}

bool WiimWorker::shuffleBack(const Playback& before, bool changed) {
    if (!changed) return true;
    const WiimError e = client_.command("setPlayerCmd:loopmode:" + String(before.loop));
    logTagged("search.shuffle", "result=%s | Recherche : lecture al\xC3\xA9" "atoire remise (%s)", errorKey(e),
              wiimErrorName(e));
    return e == WiimError::None;
}

// Son coupé et lecture aléatoire retirée pendant une recherche par file : notés dans le stockage tant qu'elle dure.
// Si le Tab5 s'éteint ou redémarre au milieu, le WiiM resterait muet et sans lecture aléatoire (vu avec le
// simulateur arrêté en pleine recherche, 2026-11-17) : undoInterruptedSearch() les remet au démarrage suivant.
void WiimWorker::noteSearchChanges(const Playback& before, bool muted, bool unshuffled) {
    if (!muted && !unshuffled) return;
    wr::SearchUndo u;
    u.unmute = muted;
    u.loop = unshuffled ? before.loop : -1;
    fav::saveSearchUndo(wr::searchUndoText(u));
}

void WiimWorker::undoInterruptedSearch(const wr::PlayerStatus& now) {
    undoChecked_ = true;
    std::string text;
    wr::SearchUndo u;
    if (!fav::searchUndo(text)) return;
    if (wr::parseSearchUndo(text, u)) {
        // Seulement ce qui est encore dans l'état laissé par la recherche : quelqu'un a pu y toucher depuis
        const bool unmute = u.unmute && now.muted;
        const bool reshuffle = u.loop >= 0 && now.loop == wr::loopWithoutShuffle(u.loop);
        WiimError stop = WiimError::None, loop = WiimError::None, mute = WiimError::None;
        // Encore muet : ce qui joue est un résultat de la recherche, jamais choisi ; arrêté avant de rendre le son.
        // Sauf si la fin de la recherche a remis la musique d'avant (seul le son, ou le mode, n'a pas été rétabli).
        if (unmute && now.playing && !undoKeepsPlayback_) stop = client_.command("setPlayerCmd:stop");
        if (reshuffle) loop = client_.command("setPlayerCmd:loopmode:" + String(u.loop));
        if (unmute) mute = client_.command("setPlayerCmd:mute:0");
        logTagged("search.undo", "unmute=%s shuffle=%s | Recherche interrompue : son %s, lecture al\xC3\xA9" "atoire %s",
                  unmute ? errorKey(mute) : "skip", reshuffle ? errorKey(loop) : "skip",
                  unmute ? wiimErrorName(mute) : "-", reshuffle ? wiimErrorName(loop) : "-");
        if (stop != WiimError::None || loop != WiimError::None || mute != WiimError::None) {
            undoChecked_ = false;  // WiiM injoignable : nouvel essai au prochain relevé réussi
            return;
        }
    }
    undoKeepsPlayback_ = false;
    fav::saveSearchUndo("");
}

// Début d'une recherche par file : faux (et `error`) si elle ne doit pas commencer, avant tout DeleteQueue ou
// CreateQueue. Sans relevé du lecteur, rien ne coupait le son : la file de recherche jouait à voix haute.
bool WiimWorker::searchGuardBegin(const std::string& control, SearchGuard& g, String& error) {
    const String unreachable = wr::L("the WiiM is not responding", "le WiiM ne r\xC3\xA9pond pas");
    // Note d'une recherche précédente pas encore défaite (son, lecture aléatoire) : d'abord, sinon elle serait
    // remplacée par celle-ci et le son resterait coupé
    if (!undoChecked_) {
        wr::PlayerStatus st;
        if (client_.status(st) == WiimError::None) undoInterruptedSearch(st);
    }
    g = SearchGuard();
    g.before = snapshot(control);
    if (!g.before.known) {  // un relevé manqué (réseau) : un seul nouvel essai
        pause(500, false);
        g.before = snapshot(control);
    }
    if (!g.before.known) {
        logTagged("search.cancelled",
                  "reason=status | Recherche annul\xC3\xA9" "e : \xC3\xA9tat du WiiM illisible (rien n'est lanc\xC3\xA9)");
        error = unreachable;
        return false;
    }
    if (!undoChecked_) {
        logEvent("Recherche annul\xC3\xA9" "e : la recherche d'avant n'est pas encore d\xC3\xA9" "faite");
        error = unreachable;
        return false;
    }
    if (!g.before.muted) {
        g.muted = client_.command("setPlayerCmd:mute:1") == WiimError::None;
        if (!g.muted) {
            // Son pas coupé : la file de recherche serait entendue. mute:0 au cas où la commande serait passée.
            client_.command("setPlayerCmd:mute:0");
            logEvent("Recherche annul\xC3\xA9" "e : son du WiiM non coup\xC3\xA9");
            error = unreachable;
            return false;
        }
    }
    g.unshuffled = shuffleOff(g.before);
    noteSearchChanges(g.before, g.muted, g.unshuffled);
    return true;
}

// Fin : musique d'avant remise (sinon arrêt de ce que la recherche a lancé), lecture aléatoire, son. La note n'est
// retirée que si tout est revenu ; sinon le prochain relevé réussi s'en charge (undoInterruptedSearch).
bool WiimWorker::searchGuardEnd(const std::string& control, const SearchGuard& g, String& error) {
    const bool restored = restoreAfterSearch(control, g.before);
    bool stopped = true;
    if (!restored) {
        // La file de recherche joue peut-être encore (son coupé) : arrêtée avant de rendre le son
        const WiimError e = client_.command("setPlayerCmd:stop");
        stopped = e == WiimError::None;
        logTagged("search.stop",
                  "result=%s | Apr\xC3\xA8s la recherche : musique d'avant non remise, lecture arr\xC3\xAAt\xC3\xA9" "e (%s)",
                  errorKey(e), wiimErrorName(e));
        error = wr::L("the music before could not be put back", "la musique d'avant n'a pas pu \xC3\xAAtre remise");
    }
    const bool shuffled = shuffleBack(g.before, g.unshuffled);
    bool unmuted = true;
    if (g.muted) {
        if (stopped) {
            pause(600, false);  // la musique d'avant a repris (ou la lecture est arrêtée)
            const WiimError e = client_.command("setPlayerCmd:mute:0");
            unmuted = e == WiimError::None;
            logTagged("search.unmute", "result=%s | Recherche : son r\xC3\xA9tabli (%s)", errorKey(e), wiimErrorName(e));
        } else {
            unmuted = false;  // un morceau jamais choisi pourrait être entendu
            logEvent("Recherche : son laiss\xC3\xA9 coup\xC3\xA9 (arr\xC3\xAAt refus\xC3\xA9)");
        }
    }
    if (g.muted || g.unshuffled) {
        if (shuffled && unmuted) {
            fav::saveSearchUndo("");
        } else {
            // Note gardée : nouvel essai au prochain relevé réussi, sans arrêter la musique d'avant si elle est remise
            undoKeepsPlayback_ = restored;
            undoChecked_ = false;
            logEvent("Recherche : son ou lecture al\xC3\xA9" "atoire \xC3\xA0 remettre au prochain relev\xC3\xA9");
        }
    }
    return restored && shuffled && unmuted;
}

// Vrai si la musique d'avant est remise (ou s'il n'y avait rien à faire) ; faux si ce que la recherche a lancé peut
// encore jouer : l'appelant l'arrête avant de rendre le son
bool WiimWorker::restoreAfterSearch(const std::string& control, const Playback& before) {
    if (!before.known) return false;
    wr::PlayerStatus st;
    wr::TrackMeta m;
    if (client_.status(st) != WiimError::None) {
        logEvent("Apr\xC3\xA8s la recherche : \xC3\xA9tat du WiiM illisible");
        return false;
    }
    if (client_.metaInfo(m) == WiimError::None) st.title = m.title;
    // Décision (lib/core/src/decisions.h) : rien, arrêt de la file de recherche, remise, pause ou arrêt
    wr::PlaybackBefore was;
    was.playing = before.playing;
    was.title = before.title;
    was.posMs = before.posMs;
    was.inQueue = before.index > 0;
    const wr::RestoreAction action = wr::restoreAction(was, st);
    if (action == wr::RestoreAction::Nothing || action == wr::RestoreAction::StopSearchQueue) {
        if (action == wr::RestoreAction::StopSearchQueue) {
            const WiimError e = client_.command("setPlayerCmd:stop");
            logEvent("Apr\xC3\xA8s la recherche : file de recherche arr\xC3\xAAt\xC3\xA9" "e (%s)", wiimErrorName(e));
            return e == WiimError::None;
        }
        logEvent("Apr\xC3\xA8s la recherche : rien n'a \xC3\xA9t\xC3\xA9 lanc\xC3\xA9");
        return true;
    }
    bool ok;
    if (action == wr::RestoreAction::PutBack || action == wr::RestoreAction::PutBackPaused) {
        // Remise de la file d'avant, au même morceau et à la même position
        const int c = createQueue(control, before.ctx);
        // Lecture aléatoire : CreateQueue remélange la file (peu après sa création) et PlayQueueWithIndex suit
        // l'ordre relu ensuite (vu sur un WiiM, 2026-11-16) : place du morceau cherchée par son titre, vérifiée
        auto placeOf = [&]() {
            std::string back;
            if (browseQueue(control, before.listName, back) != 200) return before.index;
            const int k = wr::queueIndexOf(wr::parseQueueContext(back, wr::kQueueTitlesMax), before.title, false);
            return k > 0 ? k : before.index;
        };
        auto playAt = [&](int index) {
            return playQueueAt(control, before.listName, index);
        };
        int p = -1, tries = 0;
        if (c == 200) {
            pause(700, false);
            p = playAt(placeOf());
            wr::TrackMeta m;
            for (tries = 1; p == 200 && tries < 3; ++tries) {
                pause(1500, false);
                if (client_.metaInfo(m) != WiimError::None || m.title == before.title) break;
                p = playAt(placeOf());  // autre morceau : ordre changé entre-temps, nouvel essai
            }
        }
        if (p == 200 && before.posMs > 5000) {
            // Seek seulement une fois le morceau chargé (« load » puis « play ») : ignoré sinon
            wr::PlayerStatus now;
            for (int k = 0; k < 8; ++k) {
                if (client_.status(now) == WiimError::None && now.playing && !now.loading && now.durationMs > 0) break;
                pause(500, false);
            }
            client_.command("setPlayerCmd:seek:" + String(before.posMs / 1000));
        }
        // Le morceau était en pause : il le reste (le son est encore coupé pour la recherche)
        if (action == wr::RestoreAction::PutBackPaused && p == 200) client_.command("setPlayerCmd:pause");
        ok = c == 200 && p == 200;
        const bool paused = action == wr::RestoreAction::PutBackPaused;
        if (ok)
            logTagged("search.restore", "paused=%d create=%d play=%d tries=%d | "
                      "Apr\xC3\xA8s la recherche : musique d'avant remise%s (%d, %d, essai %d)",
                      paused ? 1 : 0, c, p, tries, paused ? " en pause" : "", c, p, tries);
        else
            logEvent("Apr\xC3\xA8s la recherche : remise refus\xC3\xA9" "e (CreateQueue %d, PlayQueueWithIndex %d)", c, p);
    } else {
        // Rien ne jouait (ou lecture hors file : Spotify, AirPlay…) : on arrête ce que la recherche a lancé
        const WiimError e = client_.command(action == wr::RestoreAction::Pause ? "setPlayerCmd:pause" : "setPlayerCmd:stop");
        logEvent("Apr\xC3\xA8s la recherche : lecture arr\xC3\xAAt\xC3\xA9" "e (%s)", wiimErrorName(e));
        ok = e == WiimError::None;
    }
    nextPoll_ = millis();
    return ok;
}

void WiimWorker::runSearch(const String& query) {
    resultsFrom_ = ResultsFrom::WiimQueue;
    searchUrl_.clear();
    searchTitles_.clear();
    searchAlbums_.clear();
    WiimEvent ev;
    ev.kind = WiimEvent::SearchDone;
    ev.name = query;
    std::string control, templ;
    if (!findPlayQueue(control, false)) {
        ev.text = wr::L("the WiiM is not responding", "le WiiM ne r\xC3\xA9pond pas");
        push(std::move(ev));
        return;
    }
    // Recherche directe (accès Qobuz donné par le WiiM, non officielle, si choisie) : rien ne joue avant le choix
    if (!cfg_.qobuzDirect) {
        logEvent("Recherche Qobuz par une file du WiiM (recherche directe non choisie)");
    } else {
        std::string body;
        const uint32_t t0 = millis();
        const int code = qobuzGet(control, "track/search?query=" + wr::urlQueryEncode(query.c_str()) + "&limit=40", body);
        if (code == 200) {
            qobuzResults_ = wr::parseQobuzTracks(body);
            resultsFrom_ = ResultsFrom::QobuzDirect;
            logEvent("Recherche Qobuz directe \xC2\xAB %s \xC2\xBB : %u morceau(x) (%lu ms)", query.c_str(),
                     (unsigned)qobuzResults_.size(), (unsigned long)(millis() - t0));
            ev.ok = true;
            for (auto& t : qobuzResults_) ev.tracks.push_back(wr::QueueTrack{t.title, t.artist, t.album, t.image});
            push(std::move(ev));
            return;
        }
        logEvent("Recherche Qobuz directe : %d, recherche par une file du WiiM", code);
    }
    if (!qobuzModel(control, templ)) {
        ev.text = wr::L("first start a Qobuz album from the WiiM Home app (only once)", "lance d'abord un album Qobuz depuis l'app WiiM Home (une seule fois)");
        push(std::move(ev));
        return;
    }
    // Résultats relus ; d'abord sans lancer la lecture, puis en la lançant si la file reste vide
    auto readBack = [&](const std::string& name, int seconds, wr::QueueInfo& out) {
        return readQueueStable(this, control, name, seconds, out);
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
    // Le WiiM lance la file de recherche dès sa création : son coupé pendant la recherche, pour ne pas
    // entendre un morceau que l'on n'a pas choisi ; rétabli après la remise de la musique d'avant.
    SearchGuard guard;
    String error;
    if (!searchGuardBegin(control, guard, error)) {
        ev.text = error;
        push(std::move(ev));
        return;
    }
    wr::QueueInfo best;
    const bool severalWords = plus != enc;
    int empties = 0, created = 0;
    for (auto& v : variants) {
        if (!severalWords && &v == &variants[1]) continue;  // un seul mot : même adresse que la première
        // Nom nouveau à chaque essai (deux en alternance) : pas de risque de relire l'ancienne file
        const std::string name = std::string(kSearchQueue) + "-" + std::to_string(++searchCount_ % 2);
        deleteQueue(control, name);
        const std::string& url = v.second;
        const std::string ctx = wr::queueFromTemplate(templ, name, url);
        const int create = createQueue(control, ctx);
        logTagged("search.queue", "create=%d | Recherche \xC2\xAB %s \xC2\xBB (%s) : CreateQueue %d", create,
                  query.c_str(), v.first, create);
        logLong("envoy\xC3\xA9 ", url);
        // File refusée (ou sans réponse) : comptée comme une file vide. Avant, toutes les écritures étaient
        // essayées (chacune avec un DeleteQueue qui arrête la lecture), puis « aucun résultat » au lieu d'une erreur.
        bool found = false, relevant = false;
        if (create == 200) {
            ++created;
            wr::QueueInfo q;
            found = readBack(name, 10, q);
            if (found) logLong(q.searchUrl == url ? "relu (identique) " : "relu ", wr::maskSecrets(q.searchUrl));
            std::vector<std::string> texts;
            for (auto& t : q.items) texts.insert(texts.end(), {t.title, t.artist, t.album});
            relevant = found && wr::sharesWord(words, texts);
            logTagged("search.results", "tracks=%d relevant=%d | " "  %d r\xC3\xA9sultat(s)%s%s", found ? q.tracks : 0,
                      relevant ? 1 : 0, found ? q.tracks : 0, ", sans lecture",
                      found ? (relevant ? ", en rapport" : ", SANS RAPPORT avec la recherche") : "");
            for (size_t i = 0; i < q.items.size() && i < 2; ++i)
                logLong("  - ", q.items[i].title + " \xE2\x80\x94 " + q.items[i].artist, "search.item",
                        i == 0 ? "n=1" : "n=2");
            if (found && (best.tracks == 0 || relevant)) {
                best = q;
                searchQueue_ = name;
                searchUrl_ = url;
            }
        }
        // En rapport : terminé. File restée vide (rien trouvé, ou le WiiM ne charge rien : 4 écritures prenaient
        // 43 s, vu sur un WiiM, 2026-11-17) : au plus une autre écriture. Règle : lib/core/src/decisions.h
        if (!found) ++empties;
        if (!wr::searchTryNext(found, relevant, empties, severalWords)) break;
    }
    searchGuardEnd(control, guard, error);  // le WiiM lance la file de recherche dès sa création
    logTagged("search.done", "created=%d | Recherche : termin\xC3\xA9" "e en %lu ms%s", created,
              (unsigned long)(millis() - t0), created ? "" : " (file de recherche refus\xC3\xA9" "e par le WiiM)");
    // Aucune file créée : erreur, pas « aucun résultat ». Musique d'avant non remise : dit seulement sans résultat
    // (la liste des résultats reste utile).
    ev.ok = created > 0 && (best.tracks > 0 || error.isEmpty());
    if (!created) ev.text = wr::L("the WiiM refused the search queue", "le WiiM a refus\xC3\xA9 la file de recherche");
    else ev.text = error;
    ev.tracks = best.items;
    searchTitles_.clear();
    searchAlbums_.clear();
    for (auto& t : best.items) {
        searchTitles_.push_back(t.title);
        searchAlbums_.push_back(t.albumId);
    }
    push(std::move(ev));
}

void WiimWorker::runPlaySearchResult(int index) {
    if (resultsFrom_ == ResultsFrom::QobuzAlbums) {
        if (index >= 1 && index <= (int)qobuzAlbums_.size()) {
            runPlayQobuzAlbum(qobuzAlbums_[index - 1].id);
        } else {
            WiimEvent ev;
            ev.kind = WiimEvent::FavPlayed;
            ev.text = wr::L("album not found", "album introuvable");
            push(std::move(ev));
        }
        return;
    }
    if (resultsFrom_ == ResultsFrom::Deezer) {
        runPlayDeezer(index - 1);
        return;
    }
    if (resultsFrom_ == ResultsFrom::QobuzDirect) {
        WiimEvent ev;
        ev.kind = WiimEvent::FavPlayed;
        std::string control, templ;
        const int i = index - 1;
        if (i >= 0 && i < (int)qobuzResults_.size() && findPlayQueue(control, false) && qobuzModel(control, templ)) {
            const wr::QobuzTrack& t = qobuzResults_[i];
            ev.ok = playAlbumAt(control, templ,
                                "https://www.qobuz.com/api.json/0.2/album/get?album_id=" + wr::urlQueryEncode(t.albumId) +
                                    "&offset=0&limit=50",
                                t.title, "Qobuz");
        }
        if (!ev.ok) ev.text = wr::L("the WiiM did not start this track", "le WiiM n'a pas lanc\xC3\xA9 ce morceau");
        push(std::move(ev));
        return;
    }
    std::string control, templ;
    int code = -1;
    // La file de la liste a pu disparaître (le WiiM n'en garde que deux ou trois ; la remise de la musique
    // d'avant en recrée une) : file recréée depuis son adresse, lancée au morceau choisi (par son titre)
    if (!searchUrl_.empty() && index >= 1 && index <= (int)searchTitles_.size() && findPlayQueue(control, false) &&
        qobuzModel(control, templ)) {
        WiimEvent ev;
        ev.kind = WiimEvent::FavPlayed;
        // Résultat d'une recherche : son album, à ce morceau. La file de recherche grandit jusqu'à 1000 morceaux et,
        // en lecture aléatoire, le WiiM la remélange pendant qu'elle grandit : un autre morceau partait et la remise
        // après une recherche échouait (vu sur un WiiM, 2026-11-17). Un album a une taille fixe.
        const std::string& album = index <= (int)searchAlbums_.size() ? searchAlbums_[index - 1] : std::string();
        if (!album.empty())
            ev.ok = playAlbumAt(control, templ, wr::qobuzAlbumUrl(album), searchTitles_[index - 1], "Qobuz (album)");
        else
            ev.ok = playAlbumAt(control, templ, searchUrl_, searchTitles_[index - 1], "Liste Qobuz");
        if (!ev.ok) ev.text = wr::L("the WiiM did not start this track", "le WiiM n'a pas lanc\xC3\xA9 ce morceau");
        push(std::move(ev));
        return;
    }
    if (findPlayQueue(control, false))
        code = playQueueAt(control, searchQueue_, index);
    logEvent("Recherche : r\xC3\xA9sultat %d -> %d", index, code);
    WiimEvent ev;
    ev.kind = WiimEvent::FavPlayed;
    ev.ok = code == 200;
    if (!ev.ok) ev.text = wr::L("the WiiM did not start this track", "le WiiM n'a pas lanc\xC3\xA9 ce morceau");
    push(std::move(ev));
    nextPoll_ = millis() + 800;
}

// Morceaux favoris du compte Qobuz, en liste : on choisit celui à écouter (rien ne joue avant).
// D'abord par l'accès Qobuz donné par le WiiM ; sinon par une file que le WiiM charge lui-même (il la lance :
// son coupé, puis la musique d'avant est remise, comme pour la recherche).
void WiimWorker::runQobuzFavoriteList(const String& title) {
    WiimEvent ev;
    ev.kind = WiimEvent::SearchDone;
    ev.name = title;
    std::string control, templ, body, resp;
    if (!findPlayQueue(control, false)) {
        ev.text = wr::L("the WiiM is not responding", "le WiiM ne r\xC3\xA9pond pas");
        push(std::move(ev));
        return;
    }
    const int code = qobuzFavDenied_ ? 401 : qobuzGet(control, "favorite/getUserFavorites?type=tracks&limit=100", body);
    if (code == 401 || code == 403) qobuzFavDenied_ = true;
    if (code == 200) {
        qobuzResults_ = wr::parseQobuzTracks(body);
        resultsFrom_ = ResultsFrom::QobuzDirect;
        logEvent("Favoris Qobuz : %u morceau(x), liste sans lecture", (unsigned)qobuzResults_.size());
        ev.ok = true;
        for (auto& t : qobuzResults_) ev.tracks.push_back(wr::QueueTrack{t.title, t.artist, t.album, t.image});
        push(std::move(ev));
        return;
    }
    if (cfg_.qobuzDirect) logEvent("Favoris Qobuz : acc\xC3\xA8s direct %d, liste par une file du WiiM", code);
    else logEvent("Favoris Qobuz : liste par une file du WiiM (recherche directe non choisie)");
    if (!qobuzModel(control, templ)) {
        ev.text = wr::L("first start a Qobuz album from the WiiM Home app", "lance d'abord un album Qobuz depuis l'app WiiM Home");
        push(std::move(ev));
        return;
    }
    const std::string name = "Favoris Qobuz";
    // Relevé d'abord : DeleteQueue arrête la lecture, même pour une file inconnue (vu sur un WiiM, 2026-11-16)
    SearchGuard guard;
    String error;
    if (!searchGuardBegin(control, guard, error)) {
        ev.text = error;
        push(std::move(ev));
        return;
    }
    deleteQueue(control, name);
    const int c = createQueue(
        control,
        wr::queueFromTemplate(templ, name,
                              "https://www.qobuz.com/api.json/0.2/favorite/getUserFavorites?type=tracks&offset=0&limit=100"));
    wr::QueueInfo q;
    const bool loaded = c == 200 && readQueueStable(this, control, name, 8, q);
    searchGuardEnd(control, guard, error);
    logEvent("Favoris Qobuz (file) : CreateQueue %d, %d morceau(x)", c, loaded ? q.tracks : 0);
    resultsFrom_ = ResultsFrom::WiimQueue;
    searchQueue_ = name;
    searchUrl_ = "https://www.qobuz.com/api.json/0.2/favorite/getUserFavorites?type=tracks&offset=0&limit=100";
    ev.ok = loaded;
    ev.tracks = q.items;
    searchTitles_.clear();
    searchAlbums_.clear();
    for (auto& t : q.items) searchTitles_.push_back(t.title);
    if (!loaded)
        ev.text = error.length() ? error
                  : c != 200     ? String(wr::L("the WiiM refused the queue", "le WiiM a refus\xC3\xA9 la file"))
                                 : String(wr::L("no favorite track on Qobuz", "aucun morceau favori sur Qobuz"));
    push(std::move(ev));
}
