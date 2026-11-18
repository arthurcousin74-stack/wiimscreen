// Settings typed as text without Arduino: the preset list of the setup page, and the key/value backup copy of
// the settings.
#pragma once

#include <string>
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

}  // namespace wr
