#include "fields.h"

#include "i18n.h"
#include "text.h"

#include <cstdlib>

namespace wr {

bool parsePresets(const std::string& text, std::vector<Preset>& out, std::string& error) {
    out.clear();
    size_t start = 0;
    int lineNo = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = trim(text.substr(start, end - start));
        ++lineNo;
        start = end + 1;
        if (line.empty() || line[0] == '#') {
            if (end == text.size()) break;
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            error = L("line ", "ligne ") + std::to_string(lineNo) + L(": expected format number=name", " : format attendu numéro=nom");
            return false;
        }
        std::string num = trim(line.substr(0, eq));
        std::string label = trim(line.substr(eq + 1));
        char* endp = nullptr;
        long slot = std::strtol(num.c_str(), &endp, 10);
        if (num.empty() || *endp != '\0' || slot < 1 || slot > 20) {
            error = L("line ", "ligne ") + std::to_string(lineNo) + L(": preset number between 1 and 20", " : numéro de preset entre 1 et 20");
            return false;
        }
        if (label.empty() || label.size() > 60) {
            error = L("line ", "ligne ") + std::to_string(lineNo) + L(": name empty or too long", " : nom vide ou trop long");
            return false;
        }
        if (static_cast<int>(out.size()) >= kMaxPresets) {
            error = L("12 presets at most", "12 presets au maximum");
            return false;
        }
        Preset preset;
        preset.slot = static_cast<int>(slot);
        preset.label = label;
        out.push_back(preset);
        if (end == text.size()) break;
    }
    if (out.empty()) {
        error = L("add at least one preset", "ajoute au moins un preset");
        return false;
    }
    return true;
}

static std::string escapeField(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '\\') o += "\\\\";
        else if (c == '\t') o += "\\t";
        else if (c == '\n') o += "\\n";
        else if (c != '\r') o += c;
    }
    return o;
}

static std::string unescapeField(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) {
            o += s[i];
            continue;
        }
        char e = s[++i];
        o += e == 't' ? '\t' : e == 'n' ? '\n' : e;
    }
    return o;
}

std::string encodeFields(const std::vector<std::pair<std::string, std::string>>& fields) {
    std::string o;
    for (auto& f : fields) o += escapeField(f.first) + "\t" + escapeField(f.second) + "\n";
    return o;
}

std::vector<std::pair<std::string, std::string>> decodeFields(const std::string& text) {
    std::vector<std::pair<std::string, std::string>> out;
    size_t p = 0;
    while (p < text.size()) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        const std::string line = text.substr(p, e - p);
        p = e + 1;
        size_t t = line.find('\t');
        if (t == std::string::npos || t == 0) continue;  // ligne abîmée : ignorée
        out.emplace_back(unescapeField(line.substr(0, t)), unescapeField(line.substr(t + 1)));
    }
    return out;
}

std::string flagText(bool v) { return v ? "1" : "0"; }

bool parseFlag(const char* v, bool defaultValue) {
    const bool zero = v[0] == '0' && !v[1], one = v[0] == '1' && !v[1];
    return defaultValue ? !zero : one;
}

int parseNumber(const char* v, int lo, int hi) {
    const long n = std::strtol(v, nullptr, 10);
    return n < lo ? lo : n > hi ? hi : static_cast<int>(n);
}

}  // namespace wr
