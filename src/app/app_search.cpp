// Recherche Qobuz / Deezer (clavier, puis liste de résultats) et listes « Favoris Qobuz / Deezer » des tuiles.
// La recherche elle-même tourne dans la tâche WiiM (g_worker.search) ; les résultats arrivent par onSearchDone().

#include "app/app_internal.h"
#include "storage/favorites.h"
#include "i18n.h"
#include "app/log.h"
#include "device/sound.h"

namespace app {

static SearchKind kindOfTab(int tab) {
    return tab == kTabDeezer ? SearchKind::Deezer : tab == kTabAlbums ? SearchKind::QobuzAlbums : SearchKind::QobuzTracks;
}

void renderSearch(int pressedKey) {
    g_ui.renderSearch(g_search.query, g_search.status, g_jobs.search, pressedKey, g_search.source, g_search.deezerOk);
}

// Valeur d'une ligne marquée (logTagged : ASCII, sans espace) : espace -> '+', autres octets -> %XX
static std::string tagValue(const std::string& s) {
    static const char* const kHex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if (c == ' ') o += '+';
        else if (c > 0x20 && c < 0x7F && c != '%' && c != '|' && c != '+') o += (char)c;
        else o += '%', o += kHex[c >> 4], o += kHex[c & 15];
    }
    return o;
}

// Lance la recherche du texte saisi ou dicté
void runSearchQuery() {
    std::string q = g_search.query;
    while (!q.empty() && q.back() == ' ') q.pop_back();
    if (q.empty() || g_jobs.search) {
        renderSearch();
        return;
    }
    logTagged("search.run", "source=%s query=%s | Recherche %s : %s",
              g_search.source == kTabDeezer ? "deezer" : g_search.source == kTabAlbums ? "albums" : "qobuz",
              tagValue(q).c_str(),
              g_search.source == kTabDeezer ? "Deezer" : g_search.source == kTabAlbums ? "Albums Qobuz" : "Qobuz", q.c_str());
    g_jobs.search = true;
    g_jobs.since = millis();
    g_search.resultsTitle = q;
    g_search.resultsKind = kindOfTab(g_search.source);
    g_search.status = g_search.source == kTabDeezer ? String(wr::L("Searching Deezer\xE2\x80\xA6", "Recherche sur Deezer\xE2\x80\xA6"))
                     : g_search.source == kTabAlbums ? String(wr::L("Searching Qobuz albums\xE2\x80\xA6",
                                                          "Recherche d'albums sur Qobuz\xE2\x80\xA6"))
                                           : String(wr::L("Searching Qobuz through your WiiM\xE2\x80\xA6",
                                                          "Recherche sur Qobuz par ton WiiM\xE2\x80\xA6"));
    g_worker.search(q.c_str(), g_search.resultsKind);
    renderSearch();
}

// Tuiles « Favoris Qobuz » / « Favoris Deezer » : morceaux favoris du compte (tâche WiiM), affichés comme des
// résultats. source 0 : Qobuz, 1 : Deezer.
void openFavoriteList(bool deezer) {
    if (g_jobs.search) {
        setMessage(wr::L("Search in progress, try again in a moment",
                         "Recherche en cours, r\xC3\xA9" "essaie dans un instant"),
                   3000);
        show();  // message affiché, tuile relâchée
        return;
    }
    g_jobs.search = true;
    g_jobs.since = millis();
    g_search.resultsTitle = deezer ? wr::L("Deezer favorites", "Favoris Deezer") : wr::L("Qobuz favorites", "Favoris Qobuz");
    g_search.resultsKind = deezer ? SearchKind::DeezerFavorites : SearchKind::QobuzFavorites;
    g_search.results.clear();
    g_search.page = 0;
    g_view.screen = Screen::Results;
    g_focus.spot[(int)Screen::Results] = wr::FocusSpot{};  // anneau : la première ligne quand elle arrivera
    showResultsNote(deezer ? wr::L("Loading your Deezer favorites\xE2\x80\xA6", "Chargement de tes favoris Deezer\xE2\x80\xA6")
                           : wr::L("Loading your Qobuz favorites\xE2\x80\xA6", "Chargement de tes favoris Qobuz\xE2\x80\xA6"));
    g_worker.search(g_search.resultsTitle.c_str(), g_search.resultsKind);
}

void openSearch() {
    g_view.screen = Screen::Search;
    g_search.deezerOk = g_tiles.favOk && fav::hasDeezerModel();
    if (!g_search.deezerOk && g_search.source == kTabDeezer) g_search.source = kTabQobuz;
    if (!g_settings.qobuzDirect && g_search.source == kTabAlbums) g_search.source = kTabQobuz;
    g_search.status = g_jobs.search ? String(wr::L("Searching\xE2\x80\xA6", "Recherche en cours\xE2\x80\xA6")) : String();
    renderSearch();
}

// Liste vide avec un message (chargement, échec) ; gardé pour la redessiner (anneau du focus déplacé)
void showResultsNote(const String& note) {
    g_search.note = note;
    renderResultsList(-1, note);
}

void updateSearchField() {
    g_ui.updateSearchField(g_search.query, g_search.status, g_jobs.search, g_search.source, g_search.deezerOk);
}

void releaseKey(bool force) {
    if (g_search.keyDown < 0 || (!force && millis() - g_search.keyDownAt < 90)) return;
    g_ui.pressKey(g_search.keyDown, false);
    g_search.keyDown = -1;
}

void onSearchKey(int k) {
    if (k < 0) return;
    if (g_jobs.search && g_ui.keys()[k].kind == wr::Key::Enter) return;
    const wr::Key key = g_ui.keys()[k];
    releaseKey(true);
    g_ui.pressKey(k, true);  // retour visuel immédiat
    g_search.keyDown = k;
    g_search.keyDownAt = millis();
    sound::tap();
    switch (key.kind) {
        case wr::Key::Char:
        case wr::Key::Space: editQuery(wr::typeText(g_search.query, key.text)); break;
        case wr::Key::Back: editQuery(wr::utf8PopBack(g_search.query)); break;
        case wr::Key::Clear: editQuery(std::string()); break;
        case wr::Key::Enter: runSearchQuery(); break;
    }
}

// Texte du champ changé (touches de l'écran, clavier de l'ordinateur) : seul le champ est redessiné
void editQuery(const std::string& query) {
    g_search.query = query;
    if (!g_jobs.search) g_search.status = String();
    updateSearchField();
}

// Liste des résultats ; albums Qobuz : un cœur par ligne, plein si l'album est déjà une tuile
static int albumFavIndex(size_t i) {
    if (i >= g_search.albums.size()) return -1;
    const std::string key = wr::qobuzAlbumKey(g_search.albums[i].id);
    for (size_t k = 0; k < g_tiles.favs.size(); ++k)
        if (g_tiles.favs[k].key == key) return (int)k;
    return -1;
}

// Pochettes des lignes de la page affichée : demandées une fois chacune (pas toute la liste d'un coup)
static void requestCovers() {
    const int first = g_ui.resultsFirst(), n = g_ui.resultsPerPage();
    for (int i = first; i < first + n && i < (int)g_search.results.size(); ++i) {
        const std::string& url = g_search.results[i].art;
        if (url.empty() || knownImage(url) || !g_img.coverRequested.insert(url).second) continue;
        g_img.fetcher.request(url);
    }
}

void renderResultsList(int pressed, const String& status) {
    std::vector<int> hearts;
    String sub = status;
    if (g_search.resultsKind == SearchKind::QobuzAlbums && g_search.albums.size() == g_search.results.size()) {
        for (size_t i = 0; i < g_search.albums.size(); ++i) hearts.push_back(albumFavIndex(i) >= 0 ? 1 : 0);
        if (!sub.length() && !g_search.results.empty())
            sub = String((int)g_search.results.size()) + (g_search.results.size() > 1 ? " albums" : " album") +
                  wr::L(" \xC2\xB7 tap to play, the heart makes it a tile",
                        " \xC2\xB7 touche pour \xC3\xA9" "couter, le c\xC5\x93ur en fait une tuile");
    }
    std::vector<const ArtImage*> covers;
    for (auto& t : g_search.results) covers.push_back(knownImage(t.art));
    g_ui.renderResults(g_search.resultsTitle, g_search.results, g_search.page, sub, pressed, hearts, covers);
    requestCovers();
}

// Cœur d'un album trouvé : ajouté en tuile (tâche WiiM : il faut le modèle de file Qobuz), ou retiré
void toggleAlbumFavorite(int i) {
    if (i < 0 || i >= (int)g_search.albums.size() || g_jobs.albumFavorite) return;
    sound::tap();
    const wr::QobuzAlbum& a = g_search.albums[i];
    const int f = albumFavIndex(i);
    if (f >= 0) {
        const bool ok = fav::remove(g_tiles.favs[f].id);
        logEvent("Favori retir\xC3\xA9 depuis la recherche (%s) : %s", g_tiles.favs[f].id.c_str(), ok ? "OK" : "erreur");
        refreshTiles();
        renderResultsList();
        return;
    }
    g_jobs.albumFavorite = true;
    g_jobs.since = millis();
    g_worker.saveAlbumFavorite(a.id.c_str(), a.title.c_str(), a.artist.c_str(), a.image.c_str());
}

// Résultats arrivés (tâche WiiM)
void onSearchDone(const WiimEvent& ev) {
    // Réponse d'une recherche abandonnée (attente trop longue) arrivée pendant une autre : pas sous son titre
    if (std::string(ev.name.c_str()) != g_search.resultsTitle) {
        logEvent("Recherche : r\xC3\xA9sultat ancien ignor\xC3\xA9 (%s)", ev.name.c_str());
        return;
    }
    g_jobs.search = false;
    g_search.albums = ev.albums;
    if (!ev.ok || ev.tracks.empty()) {
        if (!ev.ok) sound::error();
        g_search.status = !ev.ok ? String(wr::L("Search failed: ", "Recherche impossible : ")) + ev.text
                                : String(g_search.resultsKind == SearchKind::QobuzAlbums ? wr::L("No album found for \xE2\x80\x9C",
                                                                      "Aucun album trouv\xC3\xA9 pour \xC2\xAB ")
                                                              : wr::L("No track found for \xE2\x80\x9C",
                                                                      "Aucun morceau trouv\xC3\xA9 pour \xC2\xAB ")) +
                                      ev.name + wr::L("\xE2\x80\x9D", " \xC2\xBB");
        if (g_view.screen == Screen::Results) {  // liste ouverte (favoris Qobuz) : le message s'y affiche
            g_search.results.clear();
            showResultsNote(!ev.ok ? String(wr::L("Failed: ", "Impossible : ")) + ev.text
                                   : String(wr::L("No favorite track on ", "Aucun morceau favori sur ")) +
                                         (g_search.resultsKind == SearchKind::DeezerFavorites ? "Deezer" : "Qobuz"));
        }
    } else {
        g_search.results = ev.tracks;
        g_search.page = 0;
        g_img.results.clear();  // pochettes de la liste précédente (PSRAM)
        g_img.coverRequested.clear();
        g_search.status = "";
        g_search.note = "";
        // Anneau du focus sur la première ligne : « taper, Entrée, Entrée » joue le premier résultat
        g_focus.spot[(int)Screen::Results] = wr::FocusSpot{wr::Act::ResultRow, 0};
        logTagged("search.results", "count=%u | R\xC3\xA9sultats : %u", (unsigned)g_search.results.size(),
                  (unsigned)g_search.results.size());
        // Toujours sur le clavier ou la liste : on montre les résultats
        if (g_view.screen == Screen::Search || g_view.screen == Screen::Results) {
            g_view.screen = Screen::Results;
            renderResultsList(-1, String());
        }
        return;
    }
    if (g_view.screen == Screen::Search) renderSearch();
}

}  // namespace app
