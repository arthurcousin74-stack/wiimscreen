#include "voice.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace wr {

namespace {

// Caractères latins accentués (UTF-8 sur 2 octets) → lettre de base
char baseLetter(unsigned c1, unsigned c2) {
    unsigned cp = ((c1 & 0x1F) << 6) | (c2 & 0x3F);
    if (cp >= 0xC0 && cp <= 0xC5) return 'a';
    if (cp == 0xC7) return 'c';
    if (cp >= 0xC8 && cp <= 0xCB) return 'e';
    if (cp >= 0xCC && cp <= 0xCF) return 'i';
    if (cp == 0xD1) return 'n';
    if (cp >= 0xD2 && cp <= 0xD6) return 'o';
    if (cp >= 0xD9 && cp <= 0xDC) return 'u';
    if (cp == 0xDD) return 'y';
    if (cp >= 0xE0 && cp <= 0xE5) return 'a';
    if (cp == 0xE7) return 'c';
    if (cp >= 0xE8 && cp <= 0xEB) return 'e';
    if (cp >= 0xEC && cp <= 0xEF) return 'i';
    if (cp == 0xF1) return 'n';
    if (cp >= 0xF2 && cp <= 0xF6) return 'o';
    if (cp >= 0xF9 && cp <= 0xFC) return 'u';
    if (cp == 0xFD || cp == 0xFF) return 'y';
    return ' ';
}

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ' ') {
            if (!cur.empty()) out.push_back(cur), cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::string join(const std::vector<std::string>& w, size_t from = 0) {
    std::string s;
    for (size_t i = from; i < w.size(); ++i) s += (s.empty() ? "" : " ") + w[i];
    return s;
}

bool has(const std::vector<std::string>& w, const char* word) {
    return std::find(w.begin(), w.end(), word) != w.end();
}

bool contains(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

// Nombre écrit en chiffres ou en lettres (0 à 100) à partir du mot i ; -1 sinon
int numberAt(const std::vector<std::string>& w, size_t i) {
    if (i >= w.size()) return -1;
    if (std::isdigit(static_cast<unsigned char>(w[i][0]))) return std::atoi(w[i].c_str());
    static const char* units[] = {"zero", "un",   "deux",     "trois",   "quatre", "cinq",
                                  "six",  "sept", "huit",     "neuf",    "dix",    "onze",
                                  "douze", "treize", "quatorze", "quinze", "seize"};
    static const char* tens[] = {"", "", "vingt", "trente", "quarante", "cinquante", "soixante"};
    // « dix sept » (le tiret de « dix-sept » est devenu une espace)
    if (w[i] == "dix" && i + 1 < w.size())
        for (int u = 7; u <= 9; ++u)
            if (w[i + 1] == units[u]) return 10 + u;
    for (int u = 0; u <= 16; ++u)
        if (w[i] == units[u]) return u;
    if (w[i] == "une") return 1;
    if (w[i] == "cent") return 100;
    for (int t = 2; t <= 6; ++t)
        if (w[i] == tens[t]) {
            int v = t * 10;
            size_t j = i + 1;
            if (j < w.size() && w[j] == "et") ++j;
            if (j < w.size()) {
                if (w[j] == "dix" && j + 1 < w.size())  // soixante-dix-sept…
                    for (int u = 7; u <= 9; ++u)
                        if (w[j + 1] == units[u]) return v + 10 + u;
                for (int u = 1; u <= 16; ++u)
                    if (w[j] == units[u] && (u <= 9 || t == 6)) return v + u;
            }
            return v;
        }
    return -1;
}

// Mots d'introduction et articles retirés avant de chercher un nom
// Verbes et formules de politesse en tête de phrase
const char* kVerbs[] = {"joue",    "jouer",    "mets", "mettre", "met",  "lance", "lancer",   "ecoute",
                        "ecouter", "passe",    "passer", "je",   "veux", "voudrais", "j",     "aimerais",
                        "peux",    "tu",       "s",    "il",     "te",   "plait", "stp",      "sil",
                        "vous",    "play",     "put",  "on",     "please", "moi", "nous",     "ok",
                        "alors",   "euh"};
// Articles et mots génériques, retirés ensuite (« la radio », « le preset »…)
const char* kArticles[] = {"la", "le", "les", "l", "du", "de", "des", "d", "sur", "un", "une",
                           "radio", "station", "chaine", "preset", "favori", "numero"};

std::string stripLeading(const std::vector<std::string>& w, bool articlesToo) {
    std::vector<std::string> keep;
    bool started = false;
    for (auto& x : w) {
        bool skip = false;
        if (!started) {
            for (const char* f : kVerbs)
                if (x == f) skip = true;
            if (articlesToo)
                for (const char* f : kArticles)
                    if (x == f) skip = true;
        }
        if (!skip) started = true;
        if (started) keep.push_back(x);
    }
    return join(keep);
}

// Découpe d'un tableau JSON d'objets plats en tenant compte des chaînes
std::vector<std::string> jsonObjects(const std::string& json) {
    std::vector<std::string> out;
    int depth = 0;
    bool inStr = false, esc = false;
    size_t start = 0;
    for (size_t i = 0; i < json.size(); ++i) {
        char c = json[i];
        if (inStr) {
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"') inStr = false;
            continue;
        }
        if (c == '"') inStr = true;
        else if (c == '{') {
            if (depth++ == 0) start = i;
        } else if (c == '}' && depth > 0) {
            if (--depth == 0) out.push_back(json.substr(start, i - start + 1));
        }
    }
    return out;
}

}  // namespace

bool sharesWord(const std::string& query, const std::vector<std::string>& texts) {
    std::string all;
    for (auto& t : texts) all += " " + normalizeText(t) + " ";
    std::string q = normalizeText(query), w;
    for (size_t i = 0; i <= q.size(); ++i) {
        if (i == q.size() || q[i] == ' ') {
            if (w.size() >= 3 && all.find(w) != std::string::npos) return true;
            w.clear();
        } else {
            w += q[i];
        }
    }
    return false;
}

std::string normalizeText(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = s[i];
        char add = ' ';
        if (c < 0x80) {
            if (std::isalnum(c)) add = static_cast<char>(std::tolower(c));
            else if (c == '-' || c == '\'') add = ' ';
        } else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
            unsigned cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3F);
            if (cp == 0x152 || cp == 0x153) out += 'o', add = 'e';  // œ → oe
            else add = baseLetter(c, static_cast<unsigned char>(s[i + 1]));
            ++i;
        } else {
            while (i + 1 < s.size() && (static_cast<unsigned char>(s[i + 1]) & 0xC0) == 0x80) ++i;
        }
        if (add == ' ' && (out.empty() || out.back() == ' ')) continue;
        out += add;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// Coefficient de Dice sur les bigrammes de caractères (espaces compris)
float similarity(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return 0;
    if (a == b) return 1;
    auto grams = [](const std::string& s) {
        std::vector<std::string> g;
        std::string t = " " + s + " ";
        for (size_t i = 0; i + 1 < t.size(); ++i) g.push_back(t.substr(i, 2));
        std::sort(g.begin(), g.end());
        return g;
    };
    auto ga = grams(a), gb = grams(b);
    size_t i = 0, j = 0, common = 0;
    while (i < ga.size() && j < gb.size()) {
        if (ga[i] == gb[j]) ++common, ++i, ++j;
        else if (ga[i] < gb[j]) ++i;
        else ++j;
    }
    return 2.0f * common / (ga.size() + gb.size());
}

VoiceIntent parseVoice(const std::string& text, const std::vector<Preset>& presets) {
    VoiceIntent in;
    const std::string n = normalizeText(text);
    const auto w = split(n);
    if (w.empty()) return in;

    // --- Commandes de lecture
    auto set = [&](VoiceIntent::Kind k) {
        in.kind = k;
        return in;
    };
    for (size_t i = 0; i < w.size(); ++i)
        if (w[i] == "volume" || w[i] == "son") {
            size_t j = i + 1;
            if (j < w.size() && (w[j] == "a" || w[j] == "au" || w[j] == "sur")) ++j;
            int v = numberAt(w, j);
            if (v >= 0) {
                in.volume = std::min(100, v);
                return set(VoiceIntent::SetVolume);
            }
        }
    if (contains(n, "remet le son") || contains(n, "remets le son") || contains(n, "reactive le son") ||
        contains(n, "unmute"))
        return set(VoiceIntent::Unmute);
    if (contains(n, "coupe le son") || has(w, "muet") || has(w, "mute") || has(w, "silence"))
        return set(VoiceIntent::Mute);
    if (contains(n, "plus fort") || contains(n, "monte le son") || contains(n, "augmente") ||
        contains(n, "volume up"))
        return set(VoiceIntent::VolUp);
    if (contains(n, "moins fort") || contains(n, "baisse") || contains(n, "diminue") || contains(n, "volume down"))
        return set(VoiceIntent::VolDown);
    if (has(w, "suivant") || has(w, "suivante") || has(w, "next") || contains(n, "passe a la suite"))
        return set(VoiceIntent::Next);
    if (has(w, "precedent") || has(w, "precedente") || has(w, "previous"))
        return set(VoiceIntent::Prev);
    if (w.size() <= 3 && (has(w, "pause") || has(w, "stop") || has(w, "arrete") || has(w, "arreter")))
        return set(VoiceIntent::Pause);
    const std::string rest = stripLeading(w, true);      // « mets la radio fip » → « fip »
    const std::string restKeep = stripLeading(w, false);  // « joue radio swiss jazz » → « radio swiss jazz »
    if (rest.empty() || rest == "reprends" || rest == "reprendre" || rest == "lecture" || rest == "musique" ||
        rest == "continue" || rest == "resume")
        return set(VoiceIntent::Play);

    // --- Preset par numéro : « preset 3 », « favori numéro deux »
    for (size_t i = 0; i < w.size(); ++i)
        if (w[i] == "preset" || w[i] == "favori" || w[i] == "numero") {
            size_t j = i + 1;
            if (j < w.size() && w[j] == "numero") ++j;
            int num = numberAt(w, j);
            for (size_t p = 0; p < presets.size(); ++p)
                if (presets[p].slot == num) {
                    in.preset = static_cast<int>(p);
                    return set(VoiceIntent::Preset);
                }
        }

    // --- Preset par nom (approché)
    float best = 0;
    for (size_t p = 0; p < presets.size(); ++p) {
        std::string label = normalizeText(presets[p].label);
        float s = std::max(similarity(rest, label), similarity(restKeep, label));
        // « fip » doit trouver « FIP » plutôt que « FIP Jazz » : égalité exacte prioritaire
        if (rest == label || restKeep == label) s = 2;
        if (s > best) best = s, in.preset = static_cast<int>(p);
    }
    if (best >= 0.62f) return set(VoiceIntent::Preset);
    in.preset = -1;

    // --- Sinon : recherche de radio
    in.query = rest;
    return set(VoiceIntent::Radio);
}

std::vector<RadioStation> parseRadioBrowser(const std::string& json) {
    std::vector<RadioStation> out;
    for (auto& obj : jsonObjects(json)) {
        RadioStation s;
        std::string v;
        jsonString(obj, "name", s.name);
        if (!jsonString(obj, "url_resolved", s.url) || s.url.empty()) jsonString(obj, "url", s.url);
        if (!isHttpUrl(s.url)) continue;
        if (jsonString(obj, "favicon", v) && isHttpUrl(v)) s.favicon = v;
        long n;
        if (jsonInt(obj, "hls", n)) s.hls = n != 0;
        if (jsonInt(obj, "clickcount", n)) s.clicks = n;
        if (s.name.size() > 80) s.name.resize(80);
        out.push_back(s);
    }
    return out;
}

int bestStation(const std::vector<RadioStation>& st, const std::string& query) {
    const std::string q = normalizeText(query);
    int best = -1;
    float bestScore = -1;
    for (size_t i = 0; i < st.size(); ++i) {
        float s = similarity(q, normalizeText(st[i].name));
        if (normalizeText(st[i].name) == q) s += 0.5f;
        if (!st[i].hls) s += 0.15f;                                  // flux direct : lu par tous les WiiM
        s += std::min(0.1f, st[i].clicks / 5000.0f);                 // popularité, à peine
        if (s > bestScore) bestScore = s, best = static_cast<int>(i);
    }
    return best;
}

std::string urlEncode(const std::string& s, bool keepSlashColon) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' ||
            (keepSlashColon && (c == '/' || c == ':'))) {
            o += static_cast<char>(c);
        } else {
            o += '%';
            o += hex[c >> 4];
            o += hex[c & 15];
        }
    }
    return o;
}

void wavHeader(uint8_t h[44], uint32_t dataBytes, uint32_t sampleRate) {
    auto le32 = [&](int at, uint32_t v) {
        for (int k = 0; k < 4; ++k) h[at + k] = static_cast<uint8_t>(v >> (8 * k));
    };
    auto le16 = [&](int at, uint16_t v) {
        h[at] = static_cast<uint8_t>(v);
        h[at + 1] = static_cast<uint8_t>(v >> 8);
    };
    std::memcpy(h, "RIFF", 4);
    le32(4, 36 + dataBytes);
    std::memcpy(h + 8, "WAVEfmt ", 8);
    le32(16, 16);           // taille du bloc fmt
    le16(20, 1);            // PCM
    le16(22, 1);            // mono
    le32(24, sampleRate);
    le32(28, sampleRate * 2);
    le16(32, 2);            // octets par échantillon
    le16(34, 16);           // bits
    std::memcpy(h + 36, "data", 4);
    le32(40, dataBytes);
}

SpeechEndpoint::SpeechEndpoint(int blockMs, int silenceMs, int maxMs, int waitMs)
    : blockMs_(blockMs), silenceMs_(silenceMs), maxMs_(maxMs), waitMs_(waitMs) {}

SpeechEndpoint::State SpeechEndpoint::feed(float rms) {
    if (state_ == Done || state_ == NothingHeard) return state_;
    elapsed_ += blockMs_;
    if (noise_ < 0) noise_ = rms;
    const float speechLevel = std::max(0.02f, noise_ * 3.0f);
    const bool loud = rms > speechLevel;
    if (state_ == Waiting) {
        if (!loud) noise_ = 0.9f * noise_ + 0.1f * rms;  // bruit de fond, appris avant la parole
        voiced_ = loud ? voiced_ + 1 : 0;
        if (voiced_ * blockMs_ >= 96) state_ = Speaking;  // ~0,1 s de voix
        else if (elapsed_ >= waitMs_) state_ = NothingHeard;
    } else {
        quiet_ = loud ? 0 : quiet_ + blockMs_;
        if (quiet_ >= silenceMs_) state_ = Done;
    }
    if (elapsed_ >= maxMs_ && state_ != NothingHeard) state_ = state_ == Waiting ? NothingHeard : Done;
    return state_;
}

}  // namespace wr
