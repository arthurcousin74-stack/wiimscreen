#include "tiles.h"

#include <algorithm>
#include <cstdlib>

namespace wr {

static bool storableKey(const std::string& k) {
    return !k.empty() && k.size() <= 160 && k.find_first_of("\t\r\n") == std::string::npos;
}

const TileUse* TileStats::find(const std::string& key) const {
    for (const TileUse& u : uses)
        if (u.key == key) return &u;
    return nullptr;
}

// Entrée de la clé, créée au besoin ; nullptr si la clé ne peut pas être enregistrée. Liste pleine : la plus
// ancienne non épinglée part (toutes épinglées : pas de nouvelle entrée).
static TileUse* entry(TileStats& s, const std::string& key) {
    if (!storableKey(key)) return nullptr;
    for (TileUse& u : s.uses)
        if (u.key == key) return &u;
    if (s.uses.size() >= TileStats::kMax) {
        auto old = s.uses.end();
        for (auto it = s.uses.begin(); it != s.uses.end(); ++it)
            if (!it->pin && (old == s.uses.end() || it->last < old->last)) old = it;
        if (old == s.uses.end()) return nullptr;
        s.uses.erase(old);
    }
    s.uses.push_back(TileUse{key});
    return &s.uses.back();
}

void TileStats::played(const std::string& key) {
    TileUse* u = entry(*this, key);
    if (!u) return;
    u->last = ++seq;
    ++u->count;
}

void TileStats::setPin(const std::string& key, bool on) {
    if (!on) {
        for (TileUse& u : uses)
            if (u.key == key) u.pin = 0;
        return;
    }
    if (pinned(key)) return;
    uint32_t top = 0;
    for (const TileUse& u : uses) top = std::max(top, u.pin);
    if (TileUse* u = entry(*this, key)) u->pin = top + 1;
}

bool TileStats::pinned(const std::string& key) const {
    const TileUse* u = find(key);
    return u && u->pin;
}

std::string tileKey(const Preset& p) {
    std::string k = p.favId.empty() ? "preset:" + std::to_string(p.slot) : p.favId;
    return storableKey(k) ? k : std::string();
}

std::string tileStatsToText(const TileStats& s) {
    std::string out = "seq\t" + std::to_string(s.seq) + "\n";
    for (const TileUse& u : s.uses) {
        if (!storableKey(u.key)) continue;
        out += u.key + "\t" + std::to_string(u.last) + "\t" + std::to_string(u.count) + "\t" + std::to_string(u.pin) + "\n";
    }
    return out;
}

// Nombre décimal entier (pas de signe, pas d'espace), borné à 2^32 - 1 ; faux sinon
static bool parseCount(const std::string& t, uint32_t& v) {
    if (t.empty() || t.size() > 10 || t.find_first_not_of("0123456789") != std::string::npos) return false;
    const unsigned long long n = std::strtoull(t.c_str(), nullptr, 10);
    if (n > 0xFFFFFFFFull) return false;
    v = (uint32_t)n;
    return true;
}

TileStats tileStatsFromText(const std::string& text) {
    TileStats s;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> f;
        for (size_t p = 0;;) {
            const size_t t = line.find('\t', p);
            f.push_back(line.substr(p, t == std::string::npos ? std::string::npos : t - p));
            if (t == std::string::npos) break;
            p = t + 1;
        }
        if (f.size() == 2 && f[0] == "seq") {
            uint32_t v;
            if (parseCount(f[1], v)) s.seq = std::max(s.seq, v);
            continue;
        }
        TileUse u;
        u.key = f[0];
        if (f.size() != 4 || !storableKey(u.key) || s.find(u.key) || s.uses.size() >= TileStats::kMax) continue;
        if (!parseCount(f[1], u.last) || !parseCount(f[2], u.count) || !parseCount(f[3], u.pin)) continue;
        s.seq = std::max(s.seq, u.last);  // compteur jamais en arrière d'une entrée
        s.uses.push_back(u);
    }
    return s;
}

std::vector<size_t> tileOrder(const std::vector<std::string>& keys, const TileStats& s, bool recentFirst) {
    std::vector<size_t> order(keys.size());
    for (size_t i = 0; i < keys.size(); ++i) order[i] = i;
    // Rang : (groupe, valeur) ; groupe 0 épinglées (pin croissant), 1 jouées (plus récente d'abord), 2 les autres.
    // Tri stable : à égalité, l'ordre donné
    auto rank = [&](size_t i) -> std::pair<int, int64_t> {
        const TileUse* u = s.find(keys[i]);
        if (u && u->pin) return {0, (int64_t)u->pin};
        if (recentFirst && u && u->last) return {1, -(int64_t)u->last};
        return {2, 0};
    };
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return rank(a) < rank(b); });
    return order;
}

}  // namespace wr
