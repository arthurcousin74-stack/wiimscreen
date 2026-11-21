#include "storage/favorites.h"
#include "i18n.h"


#include <Arduino.h>
#include <LittleFS.h>

#include <algorithm>
#include <atomic>
#include <cctype>

namespace fav {

namespace {

SemaphoreHandle_t lock() {
    static SemaphoreHandle_t m = xSemaphoreCreateMutex();
    return m;
}

// Protège g_list et l'index. Les gros fichiers (copies de files) s'écrivent et se lisent hors du verrou : la boucle
// principale (tuiles, recherche) le prend aussi, une écriture longue en flash figeait l'écran.
struct Guard {
    Guard() { xSemaphoreTake(lock(), portMAX_DELAY); }
    ~Guard() { xSemaphoreGive(lock()); }
};

std::atomic<bool> g_ok{false};
std::vector<wr::Favorite> g_list;
// Modèles de file présents, gardés en mémoire : lus par la boucle principale sans toucher à la flash
std::atomic<bool> g_qobuzModel{false}, g_deezerModel{false};

constexpr const char* kIndex = "/fav/index.txt";
constexpr const char* kQobuzTemplate = "/fav/qobuz_template.xml";

bool readFile(const char* path, std::string& out) {
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    out.clear();
    out.reserve(f.size());
    char buf[512];
    while (f.available()) {
        int n = f.read(reinterpret_cast<uint8_t*>(buf), sizeof buf);
        if (n <= 0) break;
        out.append(buf, n);
    }
    f.close();
    return true;
}

bool writeFile(const char* path, const std::string& data) {
    File f = LittleFS.open(path, "w");
    if (!f) return false;
    size_t n = f.write(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    f.close();
    return n == data.size();
}

bool saveIndex() { return writeFile(kIndex, wr::favoritesToText(g_list)); }

std::string path(const std::string& id) { return "/fav/" + id + ".xml"; }

std::string modelPath(const std::string& source) {
    std::string safe;
    for (char c : source) safe += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    return "/fav/model_" + safe.substr(0, 24) + ".xml";
}

std::string onceMarker(const std::string& tag) {
    uint32_t h = 2166136261u;  // FNV-1a : nom de fichier court et sûr
    for (unsigned char c : tag) h = (h ^ c) * 16777619u;
    char p[32];
    snprintf(p, sizeof p, "/fav/once_%08lx", (unsigned long)h);
    return p;
}

}  // namespace

bool begin() {
    Guard g;
    if (g_ok) return true;
    if (!LittleFS.begin(true)) return false;  // formate la partition au premier démarrage
    if (!LittleFS.exists("/fav")) LittleFS.mkdir("/fav");
    std::string text;
    if (readFile(kIndex, text)) g_list = wr::favoritesFromText(text);
    g_qobuzModel = LittleFS.exists(kQobuzTemplate);
    g_deezerModel = LittleFS.exists(modelPath("Deezer2").c_str()) || LittleFS.exists(modelPath("Deezer").c_str());
    g_ok = true;
    return true;
}

bool eraseAll() {
    Guard g;
    g_list.clear();
    g_ok = false;
    g_qobuzModel = g_deezerModel = false;
    return LittleFS.format();
}

std::vector<wr::Favorite> list() {
    Guard g;
    return g_list;
}

bool add(wr::Favorite& f, const std::string& queueContext, std::string& error) {
    if (queueContext.size() > kMaxContext) {
        error = wr::L("queue too long to keep (start an album or a playlist)",
                      "file trop longue pour \xC3\xAAtre gard\xC3\xA9" "e (lance un album ou une playlist)");
        return false;
    }
    char idBuf[12];
    {
        Guard g;
        if (!g_ok) {
            error = wr::L("storage unavailable", "m\xC3\xA9moire indisponible");
            return false;
        }
        const bool same = std::any_of(g_list.begin(), g_list.end(), [&](const wr::Favorite& x) { return x.key == f.key; });
        if (!same && g_list.size() >= kMax) {
            error = wr::L("24 favorites at most", "24 favoris au plus");
            return false;
        }
        // Identifiant nouveau, différent de ceux déjà pris (sinon un favori écrasait le fichier d'un autre) ; aussi
        // pour un favori remplacé : son ancien fichier reste intact tant que le nouveau n'est pas écrit en entier
        for (bool taken = true; taken;) {
            snprintf(idBuf, sizeof idBuf, "%08lx", (unsigned long)esp_random());
            taken = std::any_of(g_list.begin(), g_list.end(), [&](const wr::Favorite& x) { return x.id == idBuf; });
        }
    }
    // Écriture hors du verrou ; un fichier écrit à moitié (mémoire pleine) est retiré
    const std::string file = path(idBuf);
    if (!writeFile(file.c_str(), queueContext)) {
        LittleFS.remove(file.c_str());
        error = wr::L("cannot write (storage full?)", "\xC3\xA9" "criture impossible (m\xC3\xA9moire pleine ?)");
        return false;
    }
    std::string old;  // fichier du favori remplacé : retiré une fois le nouvel index écrit
    bool ok = false;
    {
        Guard g;
        const std::vector<wr::Favorite> before = g_list;
        auto same = std::find_if(g_list.begin(), g_list.end(), [&](const wr::Favorite& x) { return x.key == f.key; });
        f.id = idBuf;
        if (same != g_list.end()) {
            old = same->id;
            *same = f;
            ok = true;
        } else if (g_list.size() < kMax) {  // la liste a pu changer pendant l'écriture
            g_list.push_back(f);
            ok = true;
        } else {
            error = wr::L("24 favorites at most", "24 favoris au plus");
        }
        if (ok && !saveIndex()) {
            g_list = before;  // l'index sur la flash désigne toujours l'ancien fichier, intact
            ok = false;
            old.clear();
            saveIndex();  // index peut-être écrit à moitié : remis comme avant si possible
            error = wr::L("cannot write the index", "\xC3\xA9" "criture de l'index impossible");
        }
    }
    if (!ok) LittleFS.remove(file.c_str());
    else if (!old.empty()) LittleFS.remove(path(old).c_str());
    return ok;
}

bool remove(const std::string& id) {
    {
        Guard g;
        auto it = std::find_if(g_list.begin(), g_list.end(), [&](const wr::Favorite& x) { return x.id == id; });
        if (it == g_list.end()) return false;
        g_list.erase(it);
        saveIndex();
    }
    LittleFS.remove(path(id).c_str());
    return true;
}

bool saveQobuzTemplate(const std::string& queueContext) {
    wr::QueueInfo q = wr::parseQueueContext(queueContext);
    if (q.sourceName != "Qobuz") return false;
    const std::string small = wr::queueFromTemplate(queueContext, q.listName, q.searchUrl);  // sans les pistes
    Guard g;
    const bool ok = g_ok && !small.empty() && writeFile(kQobuzTemplate, small);
    if (ok) g_qobuzModel = true;
    return ok;
}

bool saveSourceModel(const std::string& source, const std::string& queueContext) {
    wr::QueueInfo q = wr::parseQueueContext(queueContext);
    const std::string small = wr::queueFromTemplate(queueContext, q.listName, q.searchUrl);
    Guard g;
    const bool ok = g_ok && !source.empty() && !small.empty() && writeFile(modelPath(source).c_str(), small);
    if (ok && (source == "Deezer2" || source == "Deezer")) g_deezerModel = true;
    return ok;
}

bool sourceModel(const std::string& source, std::string& out) {
    Guard g;
    return g_ok && readFile(modelPath(source).c_str(), out) && !out.empty();
}

bool saveSettingsBackup(const std::string& text) {
    Guard g;
    if (!g_ok) return false;
    std::string old;
    if (readFile("/settings.bak", old) && old == text) return true;  // inchangée : pas d'écriture
    return writeFile("/settings.bak", text);
}

bool settingsBackup(std::string& out) {
    Guard g;
    return g_ok && readFile("/settings.bak", out) && !out.empty();
}

bool saveSearchUndo(const std::string& text) {
    Guard g;
    if (!g_ok) return false;
    if (text.empty()) return !LittleFS.exists("/fav/search_undo") || LittleFS.remove("/fav/search_undo");
    return writeFile("/fav/search_undo", text);
}

bool searchUndo(std::string& out) {
    Guard g;
    return g_ok && LittleFS.exists("/fav/search_undo") && readFile("/fav/search_undo", out) && !out.empty();
}

bool saveTileStats(const std::string& text) {
    Guard g;
    return g_ok && writeFile("/fav/tiles.txt", text);
}

bool tileStats(std::string& out) {
    Guard g;
    return g_ok && LittleFS.exists("/fav/tiles.txt") && readFile("/fav/tiles.txt", out);
}

bool doneOnce(const std::string& tag) {
    Guard g;
    return !g_ok || LittleFS.exists(onceMarker(tag).c_str());  // sans stockage : jamais d'action automatique
}

bool markOnce(const std::string& tag) {
    Guard g;
    return g_ok && writeFile(onceMarker(tag).c_str(), tag);
}

bool hasQobuzModel() { return g_qobuzModel; }

bool hasDeezerModel() { return g_deezerModel; }

bool qobuzTemplate(std::string& out) {
    Guard g;
    return g_ok && readFile(kQobuzTemplate, out) && !out.empty();
}

bool context(const std::string& id, std::string& out) {
    // Hors du verrou (fichier jusqu'à kMaxContext) ; un favori retiré pendant la lecture donne simplement un échec
    return g_ok && readFile(path(id).c_str(), out);
}

}  // namespace fav
