#include "json.h"

#include "text.h"

#include <cctype>
#include <cstdlib>

namespace wr {

namespace {

// End of the JSON value starting at `i` (index just after it)
size_t valueEnd(const std::string& s, size_t i) {
    if (i >= s.size()) return i;
    if (s[i] == '"') {
        for (size_t k = i + 1; k < s.size(); ++k) {
            if (s[k] == '\\') ++k;
            else if (s[k] == '"') return k + 1;
        }
        return s.size();
    }
    if (s[i] == '{' || s[i] == '[') {
        int depth = 0;
        bool str = false;
        for (size_t k = i; k < s.size(); ++k) {
            char c = s[k];
            if (str) {
                if (c == '\\') ++k;
                else if (c == '"') str = false;
                continue;
            }
            if (c == '"') str = true;
            else if (c == '{' || c == '[') ++depth;
            else if ((c == '}' || c == ']') && --depth == 0) return k + 1;
        }
        return s.size();
    }
    size_t k = i;
    while (k < s.size() && s[k] != ',' && s[k] != '}' && s[k] != ']' && !std::isspace((unsigned char)s[k])) ++k;
    return k;
}

size_t skipSpace(const std::string& s, size_t i) {
    while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
    return i;
}

// Contenu d'une chaîne JSON à partir de s[i] (après le guillemet ouvrant), ajouté à `out` : \n \t \uXXXX décodés,
// \r \b \f retirés (\r\n -> \n), \" \\ \/ (et tout autre caractère échappé) gardés sans la barre. Lit jusqu'à `limit`
// exclu ; avec stopAtQuote, s'arrête aussi au premier guillemet non échappé. Une barre en limit - 1 (rien à échapper
// avant la limite) est gardée telle quelle ; une séquence \uXXXX peut lire jusqu'à la fin de `s`.
// Renvoie la position où la lecture s'est arrêtée (guillemet fermant, ou limit).
size_t jsonUnescape(const std::string& s, size_t i, size_t limit, bool stopAtQuote, std::string& out) {
    while (i < limit && !(stopAtQuote && s[i] == '"')) {
        if (s[i] == '\\' && i + 1 < limit) {
            char e = s[++i];
            switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': case 'b': case 'f': break;  // \r\n → \n ; retour arrière, saut de page : retirés
                case 'u': i = decodeJsonUnicode(s, i, out); break;
                default: out += e;  // \" \\ \/
            }
        } else {
            out += s[i];
        }
        ++i;
    }
    return i;
}

}  // namespace

size_t decodeJsonUnicode(const std::string& s, size_t i, std::string& out) {
    auto hex4 = [&](size_t at, unsigned long& v) {
        if (at + 4 > s.size()) return false;
        v = 0;
        for (size_t k = at; k < at + 4; ++k) {
            const int d = hexVal(s[k]);
            if (d < 0) return false;
            v = v * 16 + (unsigned long)d;
        }
        return true;
    };
    unsigned long cp;
    if (!hex4(i + 1, cp)) return i;
    size_t end = i + 4;
    if (cp >= 0xD800 && cp <= 0xDBFF && end + 2 < s.size() && s[end + 1] == '\\' && s[end + 2] == 'u') {
        unsigned long lo;
        if (hex4(end + 3, lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            end += 6;
        }
    }
    if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;  // moitié de paire seule
    appendUtf8(out, cp);
    return end;
}

bool jsonString(const std::string& json, const std::string& key, std::string& out) {
    std::string needle = "\"" + key + "\"";
    size_t p = 0;
    while ((p = json.find(needle, p)) != std::string::npos) {
        size_t q = skipSpace(json, p + needle.size());
        if (q >= json.size() || json[q] != ':') {
            p = q;
            continue;
        }
        q = skipSpace(json, q + 1);
        if (q >= json.size() || json[q] != '"') return false;
        out.clear();
        q = jsonUnescape(json, q + 1, json.size(), true, out);
        return q < json.size();
    }
    return false;
}

bool jsonInt(const std::string& json, const std::string& key, long& out) {
    std::string needle = "\"" + key + "\"";
    size_t p = 0;
    while ((p = json.find(needle, p)) != std::string::npos) {
        size_t q = skipSpace(json, p + needle.size());
        if (q >= json.size() || json[q] != ':') {
            p = q;
            continue;
        }
        q = skipSpace(json, q + 1);
        if (q < json.size() && json[q] == '"') ++q;
        char* end = nullptr;
        long v = std::strtol(json.c_str() + q, &end, 10);
        if (end == json.c_str() + q) return false;
        out = v;
        return true;
    }
    return false;
}

std::string jsonTop(const std::string& o, const std::string& key) {
    size_t i = skipSpace(o, 0);
    if (i >= o.size() || o[i] != '{') return "";
    ++i;
    while (i < o.size()) {
        i = skipSpace(o, i);
        if (i >= o.size() || o[i] == '}') return "";
        if (o[i] == ',') {
            ++i;
            continue;
        }
        if (o[i] != '"') return "";
        size_t ke = valueEnd(o, i);
        std::string k = jsonText(o.substr(i, ke - i));
        i = skipSpace(o, ke);
        if (i >= o.size() || o[i] != ':') return "";
        i = skipSpace(o, i + 1);
        size_t ve = valueEnd(o, i);
        if (k == key) return o.substr(i, ve - i);
        i = ve;
    }
    return "";
}

std::string jsonText(const std::string& raw) {
    if (raw.size() < 2 || raw[0] != '"') return raw == "null" ? "" : raw;
    // The last character is taken as the closing quote (not checked); a quote in the middle is kept
    std::string o;
    jsonUnescape(raw, 1, raw.size() - 1, false, o);
    return o;
}

std::vector<std::string> jsonArray(const std::string& raw) {
    std::vector<std::string> out;
    size_t i = skipSpace(raw, 0);
    if (i >= raw.size() || raw[i] != '[') return out;
    ++i;
    while (i < raw.size()) {
        i = skipSpace(raw, i);
        if (i >= raw.size() || raw[i] == ']') break;
        if (raw[i] == ',') {
            ++i;
            continue;
        }
        size_t e = valueEnd(raw, i);
        if (e <= i) {  // stray '}' or ':' at array level: skipped (it used to loop forever)
            ++i;
            continue;
        }
        out.push_back(raw.substr(i, e - i));
        i = e;
    }
    return out;
}

std::vector<std::string> jsonValuesOf(const std::string& json, const std::string& key) {
    std::vector<std::string> out;
    const std::string k = "\"" + key + "\"";
    size_t p = 0;
    while ((p = json.find(k, p)) != std::string::npos) {
        size_t c = json.find(':', p + k.size());
        if (c == std::string::npos) break;
        size_t v = skipSpace(json, c + 1);
        size_t e = valueEnd(json, v);
        out.push_back(jsonText(json.substr(v, e - v)));
        p = e;
    }
    return out;
}

}  // namespace wr
