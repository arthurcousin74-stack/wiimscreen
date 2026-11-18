// Settings typed as text without Arduino: the preset list of the setup page, and the key/value backup copy of
// the settings.
#pragma once

#include <cstddef>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "wiim_api.h"  // Preset

namespace wr {

// Une ligne par preset : "1=Radio Swiss Jazz". Lignes vides et commentaires (#) ignorés.
// Renvoie false et remplit `error` si une ligne est invalide.
bool parsePresets(const std::string& text, std::vector<Preset>& out, std::string& error);

// Champs clé/valeur en texte (une ligne « clé<TAB>valeur » ; \\, tabulation et retour à la ligne échappés) :
// copie de secours des réglages, hors de la zone que réécrit un flash USB complet.
std::string encodeFields(const std::vector<std::pair<std::string, std::string>>& fields);
std::vector<std::pair<std::string, std::string>> decodeFields(const std::string& text);

// ------------------------------------------------------------ réglages décrits par un tableau
//
// Un tableau de SettingField (une ligne par réglage) donne la clé, la sorte et le membre de la structure des
// réglages. src/storage/settings.cpp s'en sert pour la NVS et pour la copie de secours (encodeSettings/decodeSettings,
// dans l'ordre du tableau) ; aggregateFieldCount() vérifie à la compilation que chaque membre a sa ligne.
// S : la structure des réglages ; Str : son type de texte (String d'Arduino, std::string dans les tests).

enum class FieldKind { Text, Flag, Number };

// Valeurs d'un réglage dans le texte de la copie de secours
std::string flagText(bool v);                    // "1" ou "0"
bool parseFlag(const char* v, bool defaultValue);  // défaut vrai : tout sauf "0" ; défaut faux : "1" seulement
int parseNumber(const char* v, int lo, int hi);  // strtol (comme String::toInt : 0 si illisible), puis bornes

template <class S, class Str>
struct SettingField {
    const char* key;      // clé NVS (15 caractères au plus) et clé de la copie de secours
    FieldKind kind;
    Str S::*textMember;   // Text
    bool S::*flagMember;  // Flag
    int S::*numberMember;  // Number
    int lo, hi;           // Number : bornes à la lecture
    bool clampNvs;        // Number : bornes aussi à la lecture de la NVS (sinon copie de secours seulement)
    bool emptyIsDefault;  // Text : valeur vide dans la copie de secours → valeur par défaut

    static constexpr SettingField text(const char* key, Str S::*m, bool emptyIsDefault = false) {
        return {key, FieldKind::Text, m, nullptr, nullptr, 0, 0, false, emptyIsDefault};
    }
    static constexpr SettingField flag(const char* key, bool S::*m) {
        return {key, FieldKind::Flag, nullptr, m, nullptr, 0, 0, false, false};
    }
    static constexpr SettingField number(const char* key, int S::*m, int lo, int hi, bool clampNvs = true) {
        return {key, FieldKind::Number, nullptr, nullptr, m, lo, hi, clampNvs, false};
    }
};

// Réglages en champs texte (encodeFields), dans l'ordre du tableau
template <class S, class Str, size_t N>
std::string encodeSettings(const S& s, const SettingField<S, Str> (&table)[N]) {
    std::vector<std::pair<std::string, std::string>> f;
    for (const auto& d : table) {
        switch (d.kind) {
            case FieldKind::Text: f.push_back({d.key, (s.*d.textMember).c_str()}); break;
            case FieldKind::Flag: f.push_back({d.key, flagText(s.*d.flagMember)}); break;
            case FieldKind::Number: f.push_back({d.key, std::to_string(s.*d.numberMember)}); break;
        }
    }
    return encodeFields(f);
}

// Champs texte → réglages : clés inconnues ignorées, la dernière valeur d'une clé l'emporte. `defaults` : valeurs
// par défaut (case non cochée, texte vide pour emptyIsDefault). Les valeurs sont lues comme des chaînes C.
template <class S, class Str, size_t N>
void decodeSettings(const std::string& text, S& s, const S& defaults, const SettingField<S, Str> (&table)[N]) {
    for (const auto& kv : decodeFields(text)) {
        const char* v = kv.second.c_str();
        for (const auto& d : table) {
            if (kv.first != d.key) continue;
            switch (d.kind) {
                case FieldKind::Text:
                    s.*d.textMember = (d.emptyIsDefault && !*v) ? defaults.*d.textMember : Str(v);
                    break;
                case FieldKind::Flag: s.*d.flagMember = parseFlag(v, defaults.*d.flagMember); break;
                case FieldKind::Number: s.*d.numberMember = parseNumber(v, d.lo, d.hi); break;
            }
            break;
        }
    }
}

// Nombre de membres d'une structure simple (agrégat) : le plus grand N tel que T{x1, …, xN} compile, chaque x
// se convertissant en n'importe quel type. Sert à vérifier qu'un tableau de réglages n'oublie aucun membre.
namespace detail {
struct AnyField {
    template <class T>
    operator T() const;  // jamais appelée (contexte non évalué)
};
template <class T, class Seq, class = void>
struct InitWith : std::false_type {};
template <class T, size_t... I>
struct InitWith<T, std::index_sequence<I...>, std::void_t<decltype(T{(static_cast<void>(I), AnyField{})...})>>
    : std::true_type {};
template <class T, size_t N>
constexpr size_t countFields() {
    if constexpr (InitWith<T, std::make_index_sequence<N + 1>>::value) return countFields<T, N + 1>();
    else return N;
}
}  // namespace detail

template <class T>
constexpr size_t aggregateFieldCount() {
    static_assert(std::is_aggregate<T>::value, "aggregateFieldCount : structure simple seulement");
    return detail::countFields<T, 0>();
}

}  // namespace wr
