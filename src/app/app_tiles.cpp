// Tuiles : presets du WiiM, favoris enregistrés sur la télécommande, albums favoris des comptes ; images
// téléchargées en tâche de fond (pochette en cours, images des tuiles, pochettes des lignes de résultats).

#include <esp_heap_caps.h>

#include <cstring>

#include "app/app_internal.h"
#include "storage/favorites.h"
#include "i18n.h"

namespace app {

static bool samePresets(const std::vector<wr::Preset>& a, const std::vector<wr::Preset>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].slot != b[i].slot || a[i].label != b[i].label || a[i].picUrl != b[i].picUrl || a[i].favId != b[i].favId)
            return false;
    return true;
}

static void applyPresets(const std::vector<wr::Preset>& list) {
    // Doigt posé sur une tuile : son indice ne vaut plus rien dans la nouvelle liste (mauvaise tuile lancée, ou
    // lecture hors de la liste si elle a raccourci)
    if (g_strip.touch) {
        g_strip.touch = false;
        g_strip.preset = -1;
    }
    g_tiles.list = list;
    // Images des tuiles disparues : libérées (PSRAM) quand le cache grandit
    if (g_img.presets.size() > 48)
        for (auto it = g_img.presets.begin(); it != g_img.presets.end();) {
            bool used = it->first == g_img.art.url;
            for (auto& p : g_tiles.list) used |= p.picUrl == it->first;
            it = used ? std::next(it) : g_img.presets.erase(it);
        }
    g_ui.setPresets(g_tiles.list);
    g_active.index = -1;
    for (size_t i = 0; i < g_tiles.list.size(); ++i) {
        const std::string& url = g_tiles.list[i].picUrl;
        if (url.empty()) continue;
        auto it = g_img.presets.find(url);
        if (it != g_img.presets.end()) g_ui.setPresetArt((int)i, &it->second);
        else g_img.fetcher.request(url);
    }
}

// Tuiles : presets du WiiM, puis favoris enregistrés sur la télécommande
void refreshTiles() {
    g_tiles.favs = g_tiles.favOk ? fav::list() : std::vector<wr::Favorite>();
    std::vector<wr::Preset> list = g_tiles.wiimPresets;
    for (size_t i = 0; i < g_tiles.favs.size(); ++i) {
        wr::Preset p;
        p.slot = 100 + (int)i;
        p.label = g_tiles.favs[i].title;
        p.picUrl = g_tiles.favs[i].artUrl;
        p.favId = g_tiles.favs[i].id;
        list.push_back(p);
    }
    // Favoris Qobuz du compte (ceux de l'app) : le WiiM les charge avec sa propre connexion Qobuz
    if (g_tiles.favOk && fav::hasQobuzModel()) {
        wr::Preset p;
        p.slot = 99;
        p.label = wr::L("Qobuz favorites", "Favoris Qobuz");
        p.favId = kQobuzFavsId;
        list.push_back(p);
    }
    // Favoris Deezer (profil public indiqué dans la configuration) : il faut un modèle de file Deezer du WiiM
    if (g_tiles.favOk && g_settings.deezerUser.length() && fav::hasDeezerModel()) {
        wr::Preset p;
        p.slot = 98;
        p.label = wr::L("Deezer favorites", "Favoris Deezer");

        p.favId = kDeezerFavsId;
        list.push_back(p);
    }
    // Albums favoris Qobuz, avec leur pochette
    for (size_t i = 0; i < g_tiles.qobuzAlbums.size() && i < 36; ++i) {
        wr::Preset p;
        p.slot = 200 + (int)i;
        p.label = g_tiles.qobuzAlbums[i].title;
        p.picUrl = g_tiles.qobuzAlbums[i].image;
        p.favId = "qobuz-album:" + g_tiles.qobuzAlbums[i].id;
        list.push_back(p);
    }
    // Albums favoris Deezer
    for (size_t i = 0; i < g_tiles.deezerAlbums.size() && i < 36; ++i) {
        wr::Preset p;
        p.slot = 300 + (int)i;
        p.label = g_tiles.deezerAlbums[i].title;
        p.picUrl = g_tiles.deezerAlbums[i].image;
        p.favId = "deezer-album:" + g_tiles.deezerAlbums[i].id;
        list.push_back(p);
    }
    if (samePresets(list, g_tiles.list)) return;
    applyPresets(list);
    show();
}

// Favori correspondant à ce qui joue : même file (clé), sinon même album et même artiste
std::string currentFavorite() {
    if (!g_tiles.queueKey.empty())
        for (auto& f : g_tiles.favs)
            if (f.key == g_tiles.queueKey) return f.id;
    const UiState& st = g_state;
    const std::string album = st.player.album.empty() ? st.meta.album : st.player.album;
    if (album.empty()) return "";
    const std::string a = wr::normalizeText(album), artist = wr::normalizeText(st.player.artist);
    for (auto& f : g_tiles.favs)
        if (wr::normalizeText(f.title) == a && (f.artist.empty() || artist.empty() || wr::normalizeText(f.artist) == artist))
            return f.id;
    return "";
}

int favoriteState() {
    if (!g_tiles.favOk || !g_state.online || g_state.player.title.empty()) return 0;
    if (g_jobs.favorite) return 3;
    return currentFavorite().empty() ? 1 : 2;
}

// Réponse de getPresetInfo : presets du WiiM (noms et images), ou liste saisie
// complétée par les images du WiiM. Sans preset sur le WiiM : pas de tuiles, et
// l'écran explique comment en ajouter (plutôt que des presets factices).
void onPresets(bool ok, const std::vector<wr::Preset>& fromWiim) {
    g_tiles.fromWiim = ok;
    std::vector<wr::Preset> list;
    if (g_settings.autoPresets()) {
        if (!ok) return;  // on garde la liste actuelle ; nouvel essai plus tard
        list = fromWiim;
    } else {
        list = g_settings.presets;
        if (ok)
            for (auto& p : list)
                for (auto& w : fromWiim)
                    if (w.slot == p.slot) p.picUrl = w.picUrl;
    }
    g_tiles.wiimPresets = list;
    refreshTiles();
}

void requestPresets() {
    g_tiles.lastFetch = millis();
    g_worker.refreshPresets();
    g_worker.refreshQobuzFavorites();  // albums favoris Qobuz (tuiles), même rythme que les presets
}

// Images téléchargées en tâche de fond : pochette ou image de preset
void checkImages() {
    ArtImage img;
    bool changed = false;
    while (g_img.fetcher.poll(img)) {
        if (!img.ok()) {
            // Échec (réseau, format) : rien n'est gardé, l'image sera redemandée (retryImages, nouvelle page)
            g_img.coverRequested.erase(img.url);
            // Pochette du nouveau morceau introuvable : celle du précédent (et sa couleur d'accent) ne reste pas
            // à côté du nouveau titre ; image par défaut jusqu'au prochain essai
            if (!g_img.wanted.empty() && img.url == g_img.wanted && g_img.art.url != g_img.wanted &&
                (g_img.art.ok() || !g_img.art.url.empty())) {
                g_ui.setArt(nullptr);
                g_img.art.reset();
                g_img.art.url.clear();
                changed = true;
            }
            continue;
        }
        if (!g_img.wanted.empty() && img.url == g_img.wanted) {
            // Même image pour une tuile (album en cours parmi les favoris) : une copie pour la tuile, sinon elle
            // ne recevait jamais son image (vu dans le simulateur)
            bool tile = false;
            for (auto& p : g_tiles.list) tile |= p.picUrl == img.url;
            if (tile && img.ok() && !g_img.presets[img.url].ok()) {
                ArtImage& copy = g_img.presets[img.url];
                copy.url = img.url;
                copy.data = static_cast<uint8_t*>(heap_caps_malloc(img.len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
                if (copy.data) {
                    memcpy(copy.data, img.data, img.len);
                    copy.len = img.len;
                    for (size_t i = 0; i < g_tiles.list.size(); ++i)
                        if (g_tiles.list[i].picUrl == img.url) g_ui.setPresetArt((int)i, &copy);
                }
            }
            g_img.art = std::move(img);
            g_ui.setArt(&g_img.art);
            changed = true;
            continue;
        }
        bool used = false;
        for (auto& p : g_tiles.list)
            if (p.picUrl == img.url) used = true;
        if (!used && g_img.coverRequested.count(img.url)) {  // pochette d'une ligne de résultats
            const std::string url = img.url;
            ArtImage& stored = g_img.results[url];
            stored = std::move(img);
            if (g_view.screen == Screen::Results)
                for (size_t i = 0; i < g_search.results.size(); ++i)
                    if (g_search.results[i].art == url) g_ui.updateResultCover((int)i, &stored);
            continue;
        }
        if (!used) continue;  // piste ou liste déjà changée : image périmée
        std::string url = img.url;
        ArtImage& stored = g_img.presets[url];
        stored = std::move(img);
        for (size_t i = 0; i < g_tiles.list.size(); ++i)
            if (g_tiles.list[i].picUrl == url) g_ui.setPresetArt((int)i, &stored);
        if (g_view.screen == Screen::Results && g_img.coverRequested.count(url))  // même image dans la liste ouverte
            for (size_t i = 0; i < g_search.results.size(); ++i)
                if (g_search.results[i].art == url) g_ui.updateResultCover((int)i, &stored);
        changed = true;
    }
    if (changed) show();
}

// Images manquantes (échec, ou résultat perdu pendant un écran sans checkImages) : redemandées toutes les minutes
void retryImages() {
    static uint32_t last = 0;
    if (millis() - last < 60000) return;
    last = millis();
    if (!g_img.wanted.empty() && g_img.art.url != g_img.wanted) g_img.fetcher.request(g_img.wanted, true);
    for (auto& p : g_tiles.list) {
        if (p.picUrl.empty()) continue;
        auto it = g_img.presets.find(p.picUrl);
        if (it == g_img.presets.end() || !it->second.ok()) g_img.fetcher.request(p.picUrl);
    }
}

// Image déjà téléchargée pour cette adresse (résultats, presets, pochette en cours), sinon nullptr
const ArtImage* knownImage(const std::string& url) {
    if (url.empty()) return nullptr;
    auto r = g_img.results.find(url);
    if (r != g_img.results.end()) return &r->second;
    auto p = g_img.presets.find(url);
    if (p != g_img.presets.end()) return &p->second;
    return g_img.art.url == url ? &g_img.art : nullptr;
}

}  // namespace app
