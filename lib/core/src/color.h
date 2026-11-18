// Colours without Arduino (RGB888): preset tiles without a picture, accent colour taken from a cover.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace wr {

// Couleur stable tirée d'un nom (fond des presets sans image), lisible avec du texte blanc.
uint32_t nameColor(const std::string& name);

// Couleur d'accent tirée d'une pochette (pixels RGB888 à la suite) : moyenne des
// couleurs vives, ramenée à une luminosité lisible sur fond sombre.
// Renvoie `fallback` pour une image en noir et blanc ou presque.
uint32_t accentColor(const uint8_t* rgb, size_t pixelCount, uint32_t fallback);

}  // namespace wr
