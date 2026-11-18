// Logique pure (sans Arduino) : testable sur PC avec `pio test -e native`.
// En-tête parapluie : core.h contenait tout ceci, chaque thème a maintenant son fichier. Le nouveau code inclut
// l'en-tête du thème ; `#include "core.h"` marche toujours.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "color.h"     // nameColor, accentColor
#include "deezer.h"    // baselineCoverUrl (and the Deezer parsers)
#include "fields.h"    // parsePresets, encodeFields, decodeFields
#include "focus.h"     // Target, TargetList: tappable elements of a screen
#include "http.h"      // HTTP responses, URLs, hosts, fingerprints, sniffImage
#include "json.h"      // jsonString, jsonInt, decodeJsonUnicode (and the depth-aware helpers)
#include "layout.h"    // Rect, Layout, computeLayout, Key, keyboardLayout
#include "text.h"      // appendUtf8, htmlEscape, wrapText, utf8PopBack (and trim, normalizeText…)
#include "wiim_api.h"  // Preset, PlayerStatus, TrackMeta, WiimIdentity and their parsers
