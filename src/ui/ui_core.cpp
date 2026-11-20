// Ui : création des canevas, fond (lueur de l'accent), envoi à l'écran, polices, pochettes.
// Les vues sont dans ui.cpp (commandes, ambiance), ui_search.cpp, ui_screens.cpp ; les petits
// éléments partagés dans ui_widgets.cpp. Constantes et aides communes : ui_internal.h.
#include "ui/ui_internal.h"

#include "ui/fonts/font_ui_24.h"
#include "ui/fonts/font_ui_28.h"
#include "ui/fonts/font_ui_40.h"
#include "ui/fonts/font_ui_56.h"

static auto& D = M5.Display;

#ifdef PC_SCALED_FONTS
#include <cmath>

// Ordinateur (simulateur, desktop, Windows) : sur un canevas plus grand que 1280x720, les polices grandissent comme
// la mise en page (sc()), faites depuis DejaVuSans.ttf (platform/pc/pc_fonts.cpp) ; jusqu'à 1280x720, ou sans le
// fichier TTF, les polices intégrées. Le Tab5 n'a pas ce code.
const uint8_t* pcScaledFont(int px);
const uint8_t* kTiny = font_ui_24;
const uint8_t* kSmall = font_ui_28;
const uint8_t* kMid = font_ui_40;
const uint8_t* kBig = font_ui_56;

static void scaleFonts(int w, int h) {
    const int s = std::min(w, h);
    const bool larger = s * 100 >= 720 * 105;  // au moins 5 % de plus que 720
    const struct {
        const uint8_t** font;
        const uint8_t* builtIn;
        int px;
    } kFonts[] = {{&kTiny, font_ui_24, 24}, {&kSmall, font_ui_28, 28}, {&kMid, font_ui_40, 40}, {&kBig, font_ui_56, 56}};
    for (const auto& f : kFonts) {
        const uint8_t* scaled = larger ? pcScaledFont((int)std::lround(f.px * s / 720.0)) : nullptr;
        *f.font = scaled ? scaled : f.builtIn;
    }
}
#else
const uint8_t* const kTiny = font_ui_24;  // noms de presets sur deux lignes
const uint8_t* const kSmall = font_ui_28;
const uint8_t* const kMid = font_ui_40;
const uint8_t* const kBig = font_ui_56;
#endif

// Police chargée sur un canevas : M5GFX relit l'en-tête VLW à chaque loadFont ; on évite de recharger la même
// (le clavier de recherche en chargeait 43 par image). Un seul canevas charge des polices sur l'appareil.
static const lgfx::LGFXBase* s_fontCanvas = nullptr;
static const uint8_t* s_font = nullptr;
void useFont(M5Canvas& C, const uint8_t* f) {
    if (s_fontCanvas == &C && s_font == f) return;
    C.loadFont(f);
    s_fontCanvas = &C;
    s_font = f;
}

void Ui::begin(const Settings& s) {
    D.setRotation(s.rotation);
    ref_ = std::min(D.width(), D.height());
    accent_ = kAccent;
    for (M5Canvas* c : {&canvas_, &bg_}) {
        c->setPsram(true);
        c->setColorDepth(16);
        c->createSprite(D.width(), D.height());
    }
    art_.setPsram(true);
    art_.setColorDepth(16);
    artBig_.setPsram(true);
    artBig_.setColorDepth(16);
    targets_.reserve(64);  // une fois : l'écran de recherche en inscrit une cinquantaine, sans allocation par image
    setPresets(s.presets);
}

void Ui::relayout() {
#ifdef PC_SCALED_FONTS
    scaleFonts(D.width(), D.height());
#endif
    layout_ = wr::computeLayout(D.width(), D.height(), (int)presets_.size());
    if (art_.width() != layout_.art.w || art_.height() != layout_.art.h) {
        art_.deleteSprite();
        art_.createSprite(layout_.art.w, layout_.art.h);
        setArt(artSrc_);  // redécodée à la nouvelle taille
    }
}

void Ui::beginScreen(View view) {
    view_ = view;
    targets_.clear();  // l'écran qui commence inscrit ses éléments touchables en se dessinant
}

void Ui::presentRect(const wr::Rect& r) {
    // Anneau du focus redessiné s'il touche la zone (elle vient d'être repeinte) ; découpé à la zone
    canvas_.setClipRect(r.x, r.y, r.w, r.h);
    focusRing();
    canvas_.clearClipRect();
    D.startWrite();
    D.setClipRect(r.x, r.y, r.w, r.h);
    canvas_.pushSprite(&D, 0, 0);
    D.clearClipRect();
    D.endWrite();
    ++presents_;
    lastPresentMs_ = millis();
}

void Ui::present() {
    focusRing();  // en dernier, par-dessus l'écran
    D.startWrite();
    canvas_.pushSprite(&D, 0, 0);
    D.endWrite();
    ++presents_;
    lastPresentMs_ = millis();
}

// Lueur de la couleur d'accent en haut, qui se fond dans le fond sombre
uint32_t Ui::bgAt(int y) const {
    const uint32_t top = mix(kBg, accent_, 0.30f);
    float t = std::min(1.0f, y / (canvas_.height() * 0.7f));
    return mix(top, kBg, t);
}

// Dégradé tramé (matrice de Bayer 4×4) : en RGB565, un dégradé sombre aussi doux
// ferait des bandes visibles. Calculé une fois par couleur d'accent.
void Ui::buildBackground() {
    if (bgAccent_ == accent_) return;
    bgAccent_ = accent_;
    static const uint8_t kBayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    auto* buf = static_cast<uint16_t*>(bg_.getBuffer());
    const int W = bg_.width(), H = bg_.height();
    if (!buf) return;
    for (int y = 0; y < H; ++y) {
        const uint32_t c = bgAt(y);
        const float r = ((c >> 16) & 0xFF) * 31 / 255.0f, g = ((c >> 8) & 0xFF) * 63 / 255.0f,
                    b = (c & 0xFF) * 31 / 255.0f;
        uint16_t row[4];
        for (int k = 0; k < 4; ++k) {
            float d = (kBayer[(y & 3) * 4 + k] + 0.5f) / 16.0f;
            uint16_t v = (uint16_t)(std::min(31, int(r + d)) << 11 | std::min(63, int(g + d)) << 5 |
                                    std::min(31, int(b + d)));
            row[k] = (uint16_t)(v >> 8 | v << 8);  // les canevas 16 bits sont en RGB565 octets inversés
        }
        uint16_t* p = buf + (size_t)y * W;
        for (int x = 0; x < W; ++x) p[x] = row[x & 3];
    }
}

void Ui::background() {
    buildBackground();
    bg_.pushSprite(&canvas_, 0, 0);
}

// ----------------------------------------------------------------------- images

// Décode un JPEG/PNG ajusté au canevas (proportions gardées, centré) sur fond `bg`
bool Ui::decode(M5Canvas& dst, const ArtImage* img, uint32_t bg) {
    dst.fillScreen(bg);
    if (!img || !img->ok()) return false;
    const int w = dst.width(), h = dst.height();
    switch (wr::sniffImage(img->data, img->len)) {
        case wr::ImageType::Jpeg:
            return dst.drawJpg(img->data, img->len, 0, 0, w, h, 0, 0, 0.0f, 0.0f, datum_t::middle_center);
        case wr::ImageType::Png:
            return dst.drawPng(img->data, img->len, 0, 0, w, h, 0, 0, 0.0f, 0.0f, datum_t::middle_center);
        default: return false;
    }
}

// Copie un canevas avec coins arrondis (masqués avec la couleur du dessous)
void Ui::roundedPush(M5Canvas& src, int x, int y, int R, bool bgIsGradient, uint32_t bg) {
    src.pushSprite(&canvas_, x, y);
    const int w = src.width(), h = src.height();
    for (int i = 0; i < R; ++i) {
        float dy = R - i - 0.5f;
        int dx = R - (int)std::lround(std::sqrt(float(R * R) - dy * dy));
        if (dx <= 0) continue;
        for (int yy : {y + i, y + h - 1 - i}) {
            uint32_t c = bgIsGradient ? bgAt(yy) : bg;
            canvas_.drawFastHLine(x, yy, dx, c);
            canvas_.drawFastHLine(x + w - dx, yy, dx, c);
        }
    }
}

// Illustration par défaut : deux croches sur carte sombre
void Ui::placeholderArt(M5Canvas& A) {
    const int s = std::min(A.width(), A.height());
    A.fillScreen(kCard);
    const int cx = A.width() / 2, cy = A.height() / 2;
    const uint32_t c = 0x5A5D66u;
    const int rx = s * 11 / 100, ry = s * 8 / 100, t = std::max(3, s * 4 / 100), bt = s * 9 / 100;
    const int h1x = cx - s * 15 / 100, h1y = cy + s * 20 / 100;
    const int h2x = cx + s * 17 / 100, h2y = cy + s * 14 / 100;
    const int s1x = h1x + rx - t, s2x = h2x + rx - t;
    const int top1 = cy - s * 22 / 100, top2 = cy - s * 28 / 100;
    A.fillEllipse(h1x, h1y, rx, ry, c);
    A.fillEllipse(h2x, h2y, rx, ry, c);
    A.fillRect(s1x, top1, t, h1y - top1, c);
    A.fillRect(s2x, top2, t, h2y - top2, c);
    A.fillTriangle(s1x, top1, s2x + t, top2, s2x + t, top2 + bt, c);
    A.fillTriangle(s1x, top1, s2x + t, top2 + bt, s1x, top1 + bt, c);
}

void Ui::setArt(const ArtImage* img) {
    artSrc_ = img;
    artBigValid_ = false;
    if (!decode(art_, img, kBg)) {
        placeholderArt(art_);
        accent_ = kAccent;
        return;
    }
    // Accent : échantillon d'un pixel sur 3 dans chaque direction
    std::vector<uint8_t> px;
    px.reserve((art_.width() / 3 + 1) * (art_.height() / 3 + 1) * 3);
    for (int y = 0; y < art_.height(); y += 3)
        for (int x = 0; x < art_.width(); x += 3) {
            auto c = art_.readPixelRGB(x, y);
            px.push_back(c.R8());
            px.push_back(c.G8());
            px.push_back(c.B8());
        }
    accent_ = wr::accentColor(px.data(), px.size() / 3, kAccent);
}
