#include "ui.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "fonts/font_ui_24.h"
#include "fonts/font_ui_28.h"
#include "fonts/font_ui_40.h"
#include "fonts/font_ui_56.h"

static auto& D = M5.Display;

// Couleurs en RGB888 (uint32_t) : M5GFX interpréterait un uint16_t comme du RGB565
static constexpr uint32_t kBg = 0x0E0F12u;      // fond
static constexpr uint32_t kCard = 0x1F2128u;    // boutons
static constexpr uint32_t kCardHi = 0x3A3D47u;  // bouton appuyé
static constexpr uint32_t kTrack = 0x34363Eu;   // rail des barres
static constexpr uint32_t kText = 0xFFFFFFu;
static constexpr uint32_t kText2 = 0xA6A9B2u;   // texte secondaire
static constexpr uint32_t kAccent = 0xE8A33Du;  // accent par défaut (ambre)

static const uint8_t* const kTiny = font_ui_24;  // noms de presets sur deux lignes
static const uint8_t* const kSmall = font_ui_28;
static const uint8_t* const kMid = font_ui_40;
static const uint8_t* const kBig = font_ui_56;

static uint32_t mix(uint32_t a, uint32_t b, float t) {  // t = 0 : a, t = 1 : b
    auto ch = [&](int sh) {
        float v = ((a >> sh) & 0xFF) * (1 - t) + ((b >> sh) & 0xFF) * t;
        return (uint32_t)std::lround(v) << sh;
    };
    return ch(16) | ch(8) | ch(0);
}

static bool endsWithEllipsis(const std::vector<std::string>& lines) {
    static const std::string ell = "\xE2\x80\xA6";
    return !lines.empty() && lines.back().size() >= ell.size() &&
           lines.back().compare(lines.back().size() - ell.size(), ell.size(), ell) == 0;
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
    setPresets(s.presets);
}

void Ui::relayout() {
    layout_ = wr::computeLayout(D.width(), D.height(), (int)presets_.size());
    if (art_.width() != layout_.art.w || art_.height() != layout_.art.h) {
        art_.deleteSprite();
        art_.createSprite(layout_.art.w, layout_.art.h);
        setArt(artSrc_);  // redécodée à la nouvelle taille
    }
}

void Ui::setPresets(const std::vector<wr::Preset>& presets) {
    presets_ = presets;
    presetArtSrc_.assign(presets_.size(), nullptr);
    thumbs_.clear();
    thumbs_.resize(presets_.size());
    page_ = 0;
    active_ = -1;
    relayout();
}

int Ui::pageCount() const {
    if (layout_.perPage <= 0) return 0;
    return ((int)presets_.size() + layout_.perPage - 1) / layout_.perPage;
}

void Ui::setPage(int p) { page_ = std::max(0, std::min(pageCount() - 1, p)); }

void Ui::presentRect(const wr::Rect& r) {
    D.startWrite();
    D.setClipRect(r.x, r.y, r.w, r.h);
    canvas_.pushSprite(&D, 0, 0);
    D.clearClipRect();
    D.endWrite();
}

bool Ui::updateProgress(const UiState& st) {
    wr::Rect r;
    if (view_ == View::Controls) r = layout_.progress;
    else if (view_ == View::Ambient) r = ambProg_;
    else return false;
    if (r.w <= 0 || r.h <= 0) return false;
    // Fond d'origine sur la seule ligne de progression, puis la ligne redessinée
    canvas_.setClipRect(r.x, r.y, r.w, r.h);
    bg_.pushSprite(&canvas_, 0, 0);
    progressRow(st, r, view_ == View::Controls ? &progHit_ : nullptr);
    canvas_.clearClipRect();
    presentRect(r);
    return true;
}

void Ui::present() {
    D.startWrite();
    canvas_.pushSprite(&D, 0, 0);
    D.endWrite();
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

// Côté de l'image dans une tuile de preset (sous l'image : le nom)
int Ui::thumbSide() const {
    if (layout_.presets.empty()) return 0;
    const wr::Rect& r = layout_.presets[0];
    return std::min(r.w - 2 * sc(10), r.h - sc(10) - sc(46));  // dessous : nom sur 1 ou 2 lignes
}

void Ui::setPresetArt(int i, const ArtImage* img) {
    if (i < 0 || i >= (int)presets_.size()) return;
    presetArtSrc_[i] = img;
    thumbs_[i].reset();
    if (!img || !img->ok()) return;
    const int side = thumbSide();
    if (side < sc(32)) return;
    auto t = std::unique_ptr<M5Canvas>(new M5Canvas());
    t->setPsram(true);
    t->setColorDepth(16);
    if (!t->createSprite(side, side)) return;
    if (decode(*t, img, kCard)) thumbs_[i] = std::move(t);
}

// --------------------------------------------------------------------- éléments

static constexpr uint32_t kRed = 0xE5484Du;

// Puce : petite pilule (point coloré facultatif + texte)
void Ui::chip(int& x, int y, const std::string& text, uint32_t dot, uint32_t textColor) {
    auto& C = canvas_;
    C.loadFont(kSmall);
    const int h = sc(28), padX = sc(12), dotD = dot ? sc(10) : 0, dotGap = dot ? sc(8) : 0;
    const int w = padX * 2 + dotD + dotGap + (int)C.textWidth(text.c_str());
    if (x + w > layout_.info.right()) return;
    C.fillRoundRect(x, y, w, h, h / 2, kCard);
    if (dot) C.fillCircle(x + padX + dotD / 2, y + h / 2, dotD / 2, dot);
    C.setTextDatum(textdatum_t::middle_left);
    C.setTextColor(textColor);
    C.drawString(text.c_str(), x + padX + dotD + dotGap, y + h / 2 + 1);
    x += w + sc(8);
}

// Textes : puces (état, source, qualité), titre, artiste (couleur d'accent), album
void Ui::info(const UiState& st) {
    auto& C = canvas_;
    const wr::Rect& r = layout_.info;
    auto measure = [&](const std::string& s) { return (int)C.textWidth(s.c_str()); };
    auto fit = [&](const std::string& s, int width) {
        auto l = wr::wrapText(s, width, 1, measure);
        return l.empty() ? std::string() : l[0];
    };

    // --- Puces ; batterie à droite
    int right = r.right();
    if (st.battery >= 0) {
        C.loadFont(kSmall);
        const int bw = sc(34), bh = sc(17), by = r.y + (sc(28) - bh) / 2;
        int bx = right - bw - sc(4);
        C.drawRoundRect(bx, by, bw, bh, sc(4), kText2);
        C.drawRoundRect(bx + 1, by + 1, bw - 2, bh - 2, sc(4) - 1, kText2);
        C.fillRect(bx + bw, by + bh / 2 - sc(4), sc(4), sc(8), kText2);
        int inner = (bw - sc(8)) * std::max(0, std::min(100, st.battery)) / 100;
        if (inner > 0) C.fillRect(bx + sc(4), by + sc(4), inner, bh - sc(8), st.battery <= 15 ? kRed : kText);
        C.setTextDatum(textdatum_t::middle_right);
        C.setTextColor(kText2);
        C.drawString((String(st.battery) + " %").c_str(), bx - sc(10), r.y + sc(14) + 1);
    }
    int x = r.x;
    const int chipY = r.y;
    if (!st.online) {
        chip(x, chipY, "Hors ligne", kRed, kText);
    } else {
        if (st.player.loading) chip(x, chipY, "Chargement\xE2\x80\xA6", accent_, kText);
        else if (st.player.playing && st.player.durationMs == 0 && !st.player.title.empty())
            chip(x, chipY, "EN DIRECT", accent_, kText);
        std::string src = wr::sourceName(st.player.mode);
        if (!src.empty()) chip(x, chipY, src, 0, kText2);
        std::string q = wr::formatQuality(st.meta.sampleRate, st.meta.bitDepth);
        if (!q.empty()) chip(x, chipY, q, 0, kText2);
    }

    // --- Titre (deux lignes si la place le permet), artiste, album
    int y = chipY + sc(28) + sc(8);
    const bool empty = st.player.title.empty();
    std::string album = st.player.album.empty() ? st.meta.album : st.player.album;
    C.setTextDatum(textdatum_t::top_left);
    C.loadFont(kBig);
    const int titleH = C.fontHeight() + sc(2);
    C.loadFont(kMid);
    const int artistH = C.fontHeight() + sc(4);
    C.loadFont(kSmall);
    const int albumH = C.fontHeight();
    const int needOne = titleH + artistH + albumH;
    const int titleLines = (r.bottom() - y >= needOne + titleH) ? 2 : 1;

    C.loadFont(kBig);
    if (empty) {
        C.setTextColor(kText2);
        C.drawString(st.online ? "Rien en lecture" : "WiiM injoignable", r.x, y);
        y += titleH;
        C.loadFont(kSmall);
        if (presets_.empty()) {
            C.drawString(fit("Lance la musique depuis ton t\xC3\xA9l\xC3\xA9phone.", r.w).c_str(), r.x, y + sc(6));
        } else {
            C.drawString(fit("Choisis un preset ci-dessous.", r.w).c_str(), r.x, y + sc(6));
        }
        return;
    }
    C.setTextColor(kText);
    for (auto& l : wr::wrapText(st.player.title, r.w, titleLines, measure)) {
        C.drawString(l.c_str(), r.x, y);
        y += titleH;
    }
    if (!st.player.artist.empty() && y + artistH <= r.bottom()) {
        C.loadFont(kMid);
        C.setTextColor(accent_);
        C.drawString(fit(st.player.artist, r.w).c_str(), r.x, y + sc(2));
        y += artistH;
    }
    if (!album.empty() && y + albumH <= r.bottom()) {
        C.loadFont(kSmall);
        C.setTextColor(kText2);
        C.drawString(fit(album, r.w).c_str(), r.x, y + sc(4));
    }
}

// Progression : « 1:23 ━━━━━━●──── 4:56 », ou « ● EN DIRECT » pour un flux sans durée
void Ui::progressRow(const UiState& st, const wr::Rect& r, wr::Rect* hit) {
    auto& C = canvas_;
    if (hit) *hit = wr::Rect{};
    if (!st.online || st.player.title.empty()) return;
    const int cy = r.y + r.h / 2;
    if (st.player.durationMs == 0) {
        // Flux sans durée : « EN DIRECT » est dans les puces ; ici seulement en vue ambiance
        if (!st.player.playing || hit) return;
        int x = r.x;
        chip(x, cy - sc(14), "EN DIRECT", accent_, kText2);
        return;
    }
    C.loadFont(kSmall);
    const uint32_t pos = std::min(st.player.positionMs, st.player.durationMs);
    const int gutter = sc(68);
    C.setTextColor(kText2);
    C.setTextDatum(textdatum_t::middle_left);
    C.drawString(wr::formatTime(pos).c_str(), r.x, cy);
    C.setTextDatum(textdatum_t::middle_right);
    C.drawString(wr::formatTime(st.player.durationMs).c_str(), r.right(), cy);
    const int bx = r.x + gutter, bw = r.w - 2 * gutter, bh = sc(6);
    C.fillRoundRect(bx, cy - bh / 2, bw, bh, bh / 2, kTrack);
    int fill = (int)((uint64_t)bw * pos / st.player.durationMs);
    if (fill > 0) C.fillRoundRect(bx, cy - bh / 2, std::max(bh, fill), bh, bh / 2, accent_);
    C.fillCircle(bx + fill, cy, sc(7), kText);
    if (hit) *hit = wr::Rect{bx - sc(10), r.y - sc(10), bw + sc(20), r.h + sc(20)};
}

static void prevIcon(M5Canvas& C, int cx, int cy, int s, uint32_t c) {
    const int t = std::max(3, s / 4);
    C.fillRect(cx - s - t, cy - s, t, 2 * s, c);
    C.fillTriangle(cx + s / 4, cy - s, cx + s / 4, cy + s, cx - s, cy, c);
    C.fillTriangle(cx + s + s / 4, cy - s, cx + s + s / 4, cy + s, cx, cy, c);
}

static void nextIcon(M5Canvas& C, int cx, int cy, int s, uint32_t c) {
    const int t = std::max(3, s / 4);
    C.fillRect(cx + s, cy - s, t, 2 * s, c);
    C.fillTriangle(cx - s / 4, cy - s, cx - s / 4, cy + s, cx + s, cy, c);
    C.fillTriangle(cx - s - s / 4, cy - s, cx - s - s / 4, cy + s, cx, cy, c);
}

// Précédent / lecture-pause / suivant ; anneau tournant pendant le chargement
void Ui::transport(const UiState& st, int pressed) {
    auto& C = canvas_;
    const uint32_t icon = st.online ? kText : 0x5A5D66u;
    for (int which : {wr::kPrev, wr::kNext}) {
        const wr::Rect& r = layout_.transport[which];
        const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
        C.fillCircle(cx, cy, r.w / 2, pressed == which ? kCardHi : kCard);
        if (which == wr::kPrev) prevIcon(C, cx, cy, r.w / 6, icon);
        else nextIcon(C, cx, cy, r.w / 6, icon);
        if (st.pending == which) {  // commande envoyée, le WiiM n'a pas encore changé de morceau
            const int a = (int)((millis() / 3) % 360);
            C.fillArc(cx, cy, r.w / 2 + sc(2), r.w / 2 + sc(6), a, a + 100, accent_);
        }
    }
    const wr::Rect& r = layout_.transport[wr::kPlayPause];
    const int cx = r.x + r.w / 2, cy = r.y + r.h / 2, R = r.w / 2;
    C.fillCircle(cx, cy, R + sc(7), mix(bgAt(cy), accent_, 0.25f));  // halo
    C.fillCircle(cx, cy, R, pressed == wr::kPlayPause ? kText : (st.online ? accent_ : kCardHi));
    if (st.player.loading || st.pending == wr::kPlayPause) {
        const int a = (int)((millis() / 4) % 360);
        C.fillArc(cx, cy, R + sc(3), R + sc(7), a, a + 100, kText);
    }
    const int s = r.w / 5;
    const uint32_t fg = kBg;
    if (st.player.playing && !st.player.loading) {
        C.fillRoundRect(cx - s * 2 / 3, cy - s, s / 2, 2 * s, 3, fg);
        C.fillRoundRect(cx + s / 6, cy - s, s / 2, 2 * s, 3, fg);
    } else {
        C.fillTriangle(cx - s / 2, cy - s, cx - s / 2, cy + s, cx + s, cy, fg);
    }
}

// Pilule de volume : [ − ] 🔈 ━━━━━●──── 42 [ + ]
void Ui::volumePill(const UiState& st, int pressed) {
    auto& C = canvas_;
    const wr::Rect& v = layout_.volume;
    C.fillRoundRect(v.x, v.y, v.w, v.h, v.h / 2, kCard);
    const uint32_t ink = st.online ? kText : 0x5A5D66u;
    for (int which : {wr::kVolDown, wr::kVolUp}) {
        const wr::Rect& r = layout_.transport[which];
        const int cx = r.x + r.w / 2, cy = r.y + r.h / 2, s = r.w / 5, t = std::max(3, s / 3);
        C.fillCircle(cx, cy, r.w / 2, pressed == which ? accent_ : kCardHi);
        C.fillRoundRect(cx - s, cy - t / 2, 2 * s, t, t / 2, ink);
        if (which == wr::kVolUp) C.fillRoundRect(cx - t / 2, cy - s, t, 2 * s, t / 2, ink);
    }
    // Haut-parleur (barré si le son est coupé)
    const wr::Rect& m = layout_.mute;
    const int sx = m.x + sc(6), cy = m.y + m.h / 2;
    const uint32_t c = st.online ? kText2 : 0x5A5D66u;
    C.fillRect(sx, cy - sc(5), sc(7), sc(10), c);
    C.fillTriangle(sx + sc(4), cy, sx + sc(15), cy - sc(11), sx + sc(15), cy + sc(11), c);
    if (st.player.muted) {
        const int mx = sx + sc(20);
        for (int k = -1; k <= 1; ++k) {
            C.drawLine(mx, cy - sc(6) + k, mx + sc(11), cy + sc(6) + k, c);
            C.drawLine(mx, cy + sc(6) + k, mx + sc(11), cy - sc(6) + k, c);
        }
    } else {
        C.fillArc(sx + sc(15), cy, sc(6), sc(8), -50, 50, c);
        C.fillArc(sx + sc(15), cy, sc(12), sc(14), -50, 50, c);
    }
    // Curseur
    const wr::Rect& b = layout_.volBar;
    const int bh = sc(6), bx = b.x + sc(8), bw = b.w - sc(16);
    C.fillRoundRect(bx, cy - bh / 2, bw, bh, bh / 2, kTrack);
    const int vol = std::max(0, std::min(100, st.player.volume));
    const int fill = bw * vol / 100;
    if (st.online && !st.player.muted && fill > 0) C.fillRoundRect(bx, cy - bh / 2, std::max(bh, fill), bh, bh / 2, kText);
    if (st.online) C.fillCircle(bx + fill, cy, sc(9), st.player.muted ? kText2 : kText);
    // Valeur
    C.loadFont(kSmall);
    C.setTextDatum(textdatum_t::middle_right);
    C.setTextColor(st.online ? kText : kText2);
    String t = !st.online ? String("\xE2\x80\x94") : st.player.muted ? String("Muet") : String(vol);
    C.drawString(t.c_str(), layout_.transport[wr::kVolUp].x - sc(10), cy + 1);
}

// Tuile de preset : image (ou monogramme coloré) et nom ; le preset en cours a un
// contour et une petite icône d'égaliseur de la couleur d'accent
void Ui::tile(int slot, int index, bool pressed) {
    auto& C = canvas_;
    const wr::Rect& r = layout_.presets[slot];
    const std::string& label = presets_[index].label;
    const bool active = index == active_;
    const uint32_t card = pressed ? accent_ : kCard;
    const int R = sc(14);
    C.fillRoundRect(r.x, r.y, r.w, r.h, R, card);
    if (active && !pressed)
        for (int k = 0; k < sc(3); ++k) C.drawRoundRect(r.x + k, r.y + k, r.w - 2 * k, r.h - 2 * k, R - k, accent_);

    const int side = thumbSide();
    const int ax = r.x + (r.w - side) / 2, ay = r.y + sc(10);
    if (M5Canvas* t = thumbs_[index].get()) {
        roundedPush(*t, ax, ay, sc(10), false, card);
    } else {
        // Monogramme : initiales sur une couleur tirée du nom
        C.fillRoundRect(ax, ay, side, side, sc(10), wr::nameColor(label));
        // Initiales du premier et du dernier mot : « Radio Swiss Jazz » → RJ, « Couleur 3 » → C3
        std::vector<char> firsts;
        bool start = true;
        for (unsigned char ch : label) {
            if (ch == ' ') {
                start = true;
                continue;
            }
            if (start && ch < 0x80 && std::isalnum(ch)) firsts.push_back((char)std::toupper(ch));
            start = false;
        }
        std::string ini;
        if (!firsts.empty()) ini += firsts.front();
        if (firsts.size() > 1) ini += firsts.back();
        if (ini.empty()) ini = "\xE2\x99\xAA";
        C.loadFont(kMid);
        C.setTextDatum(textdatum_t::middle_center);
        C.setTextColor(kText);
        C.drawString(ini.c_str(), ax + side / 2, ay + side / 2 + 1);
    }
    if (!presets_[index].favId.empty()) {  // favori de la télécommande : petit cœur dans le coin
        const int d = sc(26), hx = ax + sc(4), hy = ay + sc(4);
        C.fillRoundRect(hx, hy, d, d, sc(8), kBg);
        heartIcon(hx + d / 2, hy + d / 2 - sc(1), sc(7), accent_);
    }
    if (active) {  // petite icône d'égaliseur dans le coin de l'image
        const int d = sc(30), ex = ax + side - d - sc(4), ey = ay + sc(4);
        C.fillRoundRect(ex, ey, d, d, sc(8), kBg);
        const int bw = sc(4), gap = sc(3), base = ey + d - sc(7);
        const int hs[3] = {sc(10), sc(16), sc(7)};
        for (int k = 0; k < 3; ++k)
            C.fillRect(ex + (d - 3 * bw - 2 * gap) / 2 + k * (bw + gap), base - hs[k], bw, hs[k], accent_);
    }
    // Nom : une ligne en police normale, sinon deux lignes plus petites
    auto measure = [&](const std::string& s) { return (int)C.textWidth(s.c_str()); };
    std::vector<std::string> lines;
    C.loadFont(kSmall);
    lines = wr::wrapText(label, r.w - sc(16), 1, measure);
    if (endsWithEllipsis(lines)) {
        C.loadFont(kTiny);
        lines = wr::wrapText(label, r.w - sc(16), 2, measure);
    }
    const int lh = C.fontHeight() + sc(1);
    C.setTextDatum(textdatum_t::middle_center);
    C.setTextColor(pressed ? kBg : kText);
    int ly = ay + side + (r.bottom() - ay - side) / 2 - (int)(lines.size() - 1) * lh / 2;
    for (auto& l : lines) {
        C.drawString(l.c_str(), r.x + r.w / 2, ly);
        ly += lh;
    }
}

void Ui::pageDots() {
    const int n = pageCount();
    if (n < 2) return;
    auto& C = canvas_;
    const wr::Rect& d = layout_.dots;
    const int dot = sc(8), activeW = sc(22), gap = sc(8);
    const int total = (n - 1) * (dot + gap) + activeW;
    int x = d.x + (d.w - total) / 2;
    const int cy = d.y + d.h / 2;
    for (int i = 0; i < n; ++i) {
        const int w = i == page_ ? activeW : dot;
        C.fillRoundRect(x, cy - dot / 2, w, dot, dot / 2, i == page_ ? accent_ : kTrack);
        x += w + gap;
    }
}

// Notification : carte posée en bas de la pochette (décorative), pour ne cacher ni
// le titre ni les commandes
void Ui::toast(const String& msg) {
    if (!msg.length()) return;
    auto& C = canvas_;
    const wr::Rect& a = layout_.art;
    C.loadFont(kSmall);
    auto measure = [&](const std::string& s) { return (int)C.textWidth(s.c_str()); };
    const int pad = sc(14), dotW = sc(26);
    const int w = a.w - 2 * sc(12);
    auto lines = wr::wrapText(msg.c_str(), w - 2 * pad - dotW, 3, measure);
    const int lh = C.fontHeight() + sc(3);
    const int h = (int)lines.size() * lh + 2 * pad;
    const int x = a.x + sc(12), y = a.bottom() - sc(12) - h;
    C.fillRoundRect(x, y, w, h, sc(14), mix(kBg, 0x000000u, 0.3f));
    C.drawRoundRect(x, y, w, h, sc(14), kCardHi);
    C.fillCircle(x + pad + sc(6), y + pad + lh / 2, sc(6), accent_);
    C.setTextDatum(textdatum_t::top_left);
    C.setTextColor(kText);
    int ty = y + pad;
    for (auto& l : lines) {
        C.drawString(l.c_str(), x + pad + dotW, ty);
        ty += lh;
    }
}

// ---------------------------------------------------------------------- vues

void Ui::render(const UiState& st, int pressedPreset, int pressedTransport) {
    ambientShown_ = false;
    view_ = View::Controls;
    railShown_ = true;
    progDuration_ = st.player.durationMs;
    background();
    roundedPush(art_, layout_.art.x, layout_.art.y, sc(16), true, 0);
    info(st);
    progressRow(st, layout_.progress, &progHit_);
    transport(st, pressedTransport);
    navRail(0);
    if (favState_ > 0) {
        const wr::Rect& f = layout_.fav;
        const bool on = favState_ == 2;
        canvas_.fillCircle(f.x + f.w / 2, f.y + f.h / 2, f.w / 2, on ? accent_ : kCard);
        heartIcon(f.x + f.w / 2, f.y + f.h / 2, f.w / 4, on ? kBg : (favState_ == 3 ? kTrack : kText));
        if (!on) heartIcon(f.x + f.w / 2, f.y + f.h / 2 - 1, f.w / 4 - sc(4), favState_ == 3 ? kText2 : kCard);
    }
    if (voiceOn_) {
        const wr::Rect& m = layout_.mic;
        canvas_.fillCircle(m.x + m.w / 2, m.y + m.h / 2, m.w / 2, kCard);
        micIcon(m.x + m.w / 2, m.y + m.h / 2, m.w / 4, st.online ? kText : 0x5A5D66u, kCard);
    }
    volumePill(st, pressedTransport);
    for (size_t slot = 0; slot < layout_.presets.size(); ++slot) {
        int i = page_ * layout_.perPage + (int)slot;
        if (i < (int)presets_.size()) tile((int)slot, i, i == pressedPreset);
    }
    if (presets_.empty()) emptyStrip();
    pageDots();
    toast(st.message);
    present();
}

// Cœur : deux disques et une pointe
void Ui::heartIcon(int cx, int cy, int s, uint32_t c) {
    auto& C = canvas_;
    const int r = s / 2 + 1;
    C.fillCircle(cx - r + 1, cy - s / 4, r, c);
    C.fillCircle(cx + r - 1, cy - s / 4, r, c);
    C.fillTriangle(cx - 2 * r + 1, cy - s / 8, cx + 2 * r - 1, cy - s / 8, cx, cy + s, c);
}

bool Ui::hitFav(int x, int y) const {
    if (view_ != View::Controls || favState_ <= 0) return false;
    const wr::Rect& g = layout_.fav;
    const int pad = sc(10);
    return wr::Rect{g.x - pad, g.y - pad, g.w + 2 * pad, g.h + 2 * pad}.contains(x, y);
}

// Bande des presets vide : dire pourquoi, et comment la remplir
void Ui::emptyStrip() {
    auto& C = canvas_;
    const wr::Rect& r = layout_.strip;
    C.fillRoundRect(r.x, r.y, r.w, r.h, sc(14), kCard);
    // Étoile en pointillé : une place pour les favoris
    const int d = std::min(r.h - sc(40), sc(88)), ix = r.x + sc(24), iy = r.y + (r.h - d) / 2;
    C.drawRoundRect(ix, iy, d, d, sc(12), kTrack);
    C.drawRoundRect(ix + 1, iy + 1, d - 2, d - 2, sc(11), kTrack);
    C.setTextDatum(textdatum_t::middle_center);
    C.loadFont(kBig);
    C.setTextColor(kTrack);
    C.drawString("+", ix + d / 2, iy + d / 2);
    const int tx = ix + d + sc(22), tw = r.right() - sc(20) - tx;
    auto measure = [&](const std::string& s) { return (int)C.textWidth(s.c_str()); };
    C.setTextDatum(textdatum_t::top_left);
    C.loadFont(kMid);
    C.setTextColor(kText);
    int y = r.y + sc(22);
    C.drawString(wr::wrapText("Aucun favori pour l'instant", tw, 1, measure)[0].c_str(), tx, y);
    y += C.fontHeight() + sc(8);
    C.loadFont(kSmall);
    C.setTextColor(kText2);
    for (auto& l : wr::wrapText("Pendant qu'un album ou une playlist joue, touche le bouton favori (\xC3\xA0 gauche des commandes) "
                                "pour l'ajouter ici.",
                                tw, 3, measure)) {
        if (y + C.fontHeight() > r.bottom() - sc(10)) break;
        C.drawString(l.c_str(), tx, y);
        y += C.fontHeight() + sc(4);
    }
}

void Ui::renderAmbient(const UiState& st) {
    auto& C = canvas_;
    auto measure = [&](const std::string& s) { return (int)C.textWidth(s.c_str()); };
    background();
    const int W = C.width(), H = C.height(), m = sc(30);
    const bool landscape = W > H;
    const int side = landscape ? H - 2 * m : W - 2 * m;
    if (artBig_.width() != side) {
        artBig_.deleteSprite();
        artBig_.createSprite(side, side);
        artBigValid_ = false;
    }
    if (!artBigValid_) {
        if (!decode(artBig_, artSrc_, kBg)) placeholderArt(artBig_);
        artBigValid_ = true;
    }
    roundedPush(artBig_, m, m, sc(20), true, 0);

    // Textes à droite (paysage) ou dessous (portrait), centrés verticalement ;
    // visualiseur en bas de cette colonne
    const int tx = landscape ? m + side + sc(40) : m;
    const int tw = landscape ? W - tx - m : W - 2 * m;
    const int top = landscape ? m : m + side + sc(30);
    int bottom = H - m;
    visRect_ = wr::Rect{};
    if (visEnabled_) {
        const int visH = sc(110);
        visRect_ = wr::Rect{tx, H - m - visH, tw, visH};
        bottom = visRect_.y - sc(24);
    }

    std::string title = st.player.title.empty() ? (st.online ? "Rien en lecture" : "Hors ligne")
                                                : st.player.title;
    C.loadFont(kBig);
    auto titleLines = wr::wrapText(title, tw, 3, measure);
    const int titleH = C.fontHeight() + sc(6);
    C.loadFont(kMid);
    const int artistH = C.fontHeight() + sc(6);
    C.loadFont(kSmall);
    const int smallH = C.fontHeight() + sc(6);
    std::string album = st.player.album.empty() ? st.meta.album : st.player.album;
    std::string q = wr::formatQuality(st.meta.sampleRate, st.meta.bitDepth);
    std::string src = wr::sourceName(st.player.mode);
    std::string detail = src;
    if (!q.empty()) detail += (detail.empty() ? "" : " \xC2\xB7 ") + q;

    int blockH = (int)titleLines.size() * titleH + (st.player.artist.empty() ? 0 : artistH) +
                 (album.empty() ? 0 : smallH) + (detail.empty() ? 0 : smallH) + sc(80);
    int y = landscape ? std::max(top, top + (bottom - top - blockH) / 2) : top;

    C.setTextDatum(textdatum_t::top_left);
    C.loadFont(kBig);
    C.setTextColor(st.player.title.empty() ? kText2 : kText);
    for (auto& l : titleLines) {
        C.drawString(l.c_str(), tx, y);
        y += titleH;
    }
    if (!st.player.artist.empty()) {
        C.loadFont(kMid);
        C.setTextColor(accent_);
        C.drawString(wr::wrapText(st.player.artist, tw, 1, measure)[0].c_str(), tx, y);
        y += artistH;
    }
    C.loadFont(kSmall);
    if (!album.empty()) {
        C.setTextColor(kText);
        C.drawString(wr::wrapText(album, tw, 1, measure)[0].c_str(), tx, y);
        y += smallH;
    }
    if (!detail.empty() && st.online) {
        C.setTextColor(kText2);
        C.drawString(wr::wrapText(detail, tw, 1, measure)[0].c_str(), tx, y);
        y += smallH;
    }
    ambProg_ = wr::Rect{tx, y + sc(28), tw, sc(30)};
    progressRow(st, ambProg_, nullptr);
    // Bouton Accueil, en haut à droite : retour aux commandes (toucher ailleurs marche aussi)
    {
        const int d = sc(52);
        homeRect_ = wr::Rect{W - m - d, m, d, d};
        C.fillCircle(homeRect_.x + d / 2, homeRect_.y + d / 2, d / 2, kCard);
        homeIcon(homeRect_.x + d / 2, homeRect_.y + d / 2, d / 4, kText, kCard);
    }
    if (visEnabled_) drawBars();
    ambientShown_ = true;
    view_ = View::Ambient;
    railShown_ = false;
    present();
}

// Spectre : barres arrondies posées sur une ligne, plus claires quand elles montent,
// avec un repère de crête
void Ui::drawBars() {
    auto& C = canvas_;
    const wr::Rect& r = visRect_;
    const int n = visLevels_.empty() ? 24 : (int)visLevels_.size();
    const int gap = sc(6);
    const int bw = std::max(2, (r.w - (n - 1) * gap) / n);
    const int x0 = r.x + (r.w - (n * bw + (n - 1) * gap)) / 2;
    const int base = r.y + r.h, capH = sc(4), minH = sc(5);
    for (int i = 0; i < n; ++i) {
        float l = visLevels_.empty() ? 0.0f : visLevels_[i];
        int h = std::max(minH, (int)(l * (r.h - capH - sc(4))));
        int x = x0 + i * (bw + gap);
        C.fillRoundRect(x, base - h, bw, h, std::min(bw / 2, sc(4)), mix(accent_, kText, 0.35f * l));
        if (!visPeaks_.empty() && visPeaks_[i] > 0.04f) {
            int py = base - std::max(minH, (int)(visPeaks_[i] * (r.h - capH - sc(4)))) - capH - sc(3);
            C.fillRoundRect(x, py, bw, capH, capH / 2, kText2);
        }
    }
}

void Ui::showLevels(const std::vector<float>& levels, const std::vector<float>& peaks) {
    visLevels_ = levels;
    visPeaks_ = peaks;
    if (!ambientShown_ || visRect_.w == 0) return;
    const wr::Rect& r = visRect_;
    canvas_.setClipRect(r.x, r.y, r.w, r.h);
    bg_.pushSprite(&canvas_, 0, 0);
    drawBars();
    canvas_.clearClipRect();
    presentRect(r);
}

// Micro : capsule, arceau et pied (s : demi-hauteur de l'icône)
void Ui::micIcon(int cx, int cy, int s, uint32_t c, uint32_t bg) {
    auto& C = canvas_;
    const int cw = s * 2 / 3, ch = s * 5 / 4, t = std::max(2, s / 6);
    C.fillRoundRect(cx - cw / 2, cy - s, cw, ch, cw / 2, c);
    // Arceau : demi-anneau sous la capsule
    C.fillArc(cx, cy - s + ch - cw / 2, cw / 2 + t * 2, cw / 2 + t, 0, 180, c);
    C.fillRect(cx - t / 2, cy + s / 2 + t, t, s / 3, c);
    C.fillRoundRect(cx - cw / 2, cy + s / 2 + t + s / 3 - 1, cw, t, t / 2, c);
    (void)bg;
}

// Barre de navigation : Accueil en haut, Réglages en bas ; l'écran affiché est en couleur
void Ui::navRail(int active) {
    auto& C = canvas_;
    const wr::Rect& r = layout_.rail;
    C.fillRect(r.x, r.y, r.w, r.h, 0x0A0B0Eu);
    if (r.h > r.w) C.drawFastVLine(r.right() - 1, r.y, r.h, kCard);
    else C.drawFastHLine(r.x, r.bottom() - 1, r.w, kCard);
    const wr::Rect* items[] = {&layout_.navHome, &layout_.navSettings, &layout_.navSearch};
    for (int i = 0; i < 3; ++i) {
        const wr::Rect& b = *items[i];
        const int cx = b.x + b.w / 2, cy = b.y + b.h / 2;
        const bool on = i == active;
        const uint32_t bg = on ? accent_ : kCard, fg = on ? kBg : kText2;
        C.fillCircle(cx, cy, b.w / 2, bg);
        if (i == 0) homeIcon(cx, cy, b.w / 4, fg, bg);
        else if (i == 1) gearIcon(cx, cy, b.w / 4, fg, bg);
        else searchIcon(cx, cy, b.w / 4, fg, bg);
    }
    railShown_ = true;
}

static bool hitNav(const wr::Rect& rail, const wr::Rect& b, int pad, int x, int y);  // plus bas

// Loupe : anneau et manche
void Ui::searchIcon(int cx, int cy, int r, uint32_t c, uint32_t bg) {
    auto& C = canvas_;
    const int rr = r * 3 / 4, ox = cx - r / 4, oy = cy - r / 4, t = std::max(3, r / 4);
    C.fillCircle(ox, oy, rr, c);
    C.fillCircle(ox, oy, rr - t, bg);
    for (int k = -t / 2; k <= t / 2; ++k)
        C.drawLine(ox + rr * 7 / 10 + k, oy + rr * 7 / 10 - k, cx + r, cy + r, c);
    C.fillCircle(cx + r, cy + r, t / 2, c);
}

bool Ui::hitSearch(int x, int y) const {
    return railShown_ && view_ != View::Ambient && hitNav(layout_.rail, layout_.navSearch, sc(16), x, y);
}

// Clavier de recherche : champ de texte, état, touches
void Ui::renderSearch(const std::string& query, const String& status, bool busy, int pressedKey) {
    ambientShown_ = false;
    view_ = View::Other;
    railShown_ = false;
    auto& C = canvas_;
    background();
    navRail(2);
    const wr::Rect& A = layout_.content;
    const int m = sc(18), X = A.x + m, W = A.w - 2 * m;
    auto measure = [&](const std::string& s) { return (int)C.textWidth(s.c_str()); };
    // Champ de texte
    const int fh = sc(64), fy = A.y + m;
    C.fillRoundRect(X, fy, W, fh, fh / 2, kCard);
    C.drawRoundRect(X, fy, W, fh, fh / 2, accent_);
    searchIcon(X + fh / 2, fy + fh / 2, sc(12), kText2, kCard);
    C.loadFont(kMid);
    C.setTextDatum(textdatum_t::middle_left);
    const int tx = X + fh, tw = W - fh - sc(24);
    if (query.empty()) {
        C.setTextColor(kText2);
        C.drawString(wr::wrapText("Artiste, morceau, album\xE2\x80\xA6", tw, 1, measure)[0].c_str(), tx, fy + fh / 2 + 1);
    } else {
        std::string shown = query;  // le début coupé si le texte déborde : on voit la fin, là où l'on tape
        while (shown.size() > 1 && measure(shown) > tw - sc(10)) shown = shown.substr(1);
        C.setTextColor(kText);
        C.drawString(shown.c_str(), tx, fy + fh / 2 + 1);
        if (!busy) C.fillRect(tx + measure(shown) + sc(3), fy + sc(14), sc(3), fh - sc(28), accent_);  // curseur
    }
    // État (recherche en cours, aucun résultat, erreur) et source
    C.loadFont(kSmall);
    C.setTextDatum(textdatum_t::top_left);
    C.setTextColor(busy ? accent_ : kText2);
    const String line = status.length() ? status : String("Morceaux Qobuz, cherch\xC3\xA9s par ton WiiM");
    C.drawString(wr::wrapText(line.c_str(), W, 1, measure)[0].c_str(), X + sc(8), fy + fh + sc(10));
    // Touches
    const int ky = fy + fh + sc(10) + C.fontHeight() + sc(14);
    keys_ = wr::keyboardLayout(wr::Rect{X, ky, W, A.bottom() - m - ky});
    for (size_t i = 0; i < keys_.size(); ++i) {
        const wr::Key& k = keys_[i];
        const bool enter = k.kind == wr::Key::Enter, down = (int)i == pressedKey;
        const uint32_t bg = down ? kCardHi : (enter ? (busy ? kCardHi : accent_) : kCard);
        C.fillRoundRect(k.r.x, k.r.y, k.r.w, k.r.h, sc(10), bg);
        const int cx = k.r.x + k.r.w / 2, cy = k.r.y + k.r.h / 2;
        if (k.kind == wr::Key::Back) {  // flèche d'effacement
            const int s = std::min(k.r.h, k.r.w) / 4;
            C.fillTriangle(cx - 2 * s, cy, cx - s, cy - s, cx - s, cy + s, kText);
            C.fillRect(cx - s, cy - s, 3 * s, 2 * s, kText);
            C.drawLine(cx - s / 3, cy - s / 2, cx + s, cy + s / 2, bg);
            C.drawLine(cx - s / 3, cy + s / 2, cx + s, cy - s / 2, bg);
            continue;
        }
        C.loadFont(k.kind == wr::Key::Char ? kMid : kSmall);
        C.setTextDatum(textdatum_t::middle_center);
        C.setTextColor(enter && !busy ? kBg : (k.kind == wr::Key::Space || k.kind == wr::Key::Clear ? kText2 : kText));
        C.drawString(enter && busy ? "\xE2\x80\xA6" : k.label.c_str(), cx, cy + 1);
    }
    present();
}

int Ui::hitKey(int x, int y) const {
    const int pad = sc(4);  // les espaces entre touches comptent pour la plus proche
    for (size_t i = 0; i < keys_.size(); ++i) {
        const wr::Rect& r = keys_[i].r;
        if (wr::Rect{r.x - pad, r.y - pad, r.w + 2 * pad, r.h + 2 * pad}.contains(x, y)) return (int)i;
    }
    return -1;
}

// Résultats : une ligne par morceau (titre, artiste · album), par pages
void Ui::renderResults(const std::string& query, const std::vector<wr::QueueTrack>& items, int page,
                       const String& status, int pressed) {
    ambientShown_ = false;
    view_ = View::Other;
    railShown_ = false;
    auto& C = canvas_;
    background();
    navRail(2);
    const wr::Rect& A = layout_.content;
    const int m = sc(18), X = A.x + m, W = A.w - 2 * m;
    auto measure = [&](const std::string& s) { return (int)C.textWidth(s.c_str()); };
    // En-tête : « texte » · nombre, bouton Modifier
    const int bh = sc(52), bw = sc(170);
    resultsEdit_ = wr::Rect{X + W - bw, A.y + m, bw, bh};
    C.fillRoundRect(resultsEdit_.x, resultsEdit_.y, bw, bh, bh / 2, kCard);
    searchIcon(resultsEdit_.x + sc(30), resultsEdit_.y + bh / 2, sc(9), kText, kCard);
    C.loadFont(kSmall);
    C.setTextDatum(textdatum_t::middle_center);
    C.setTextColor(kText);
    C.drawString("Modifier", resultsEdit_.x + bw / 2 + sc(14), resultsEdit_.y + bh / 2 + 1);
    C.loadFont(kMid);
    C.setTextDatum(textdatum_t::middle_left);
    const std::string head = "\xC2\xAB " + query + " \xC2\xBB";
    C.drawString(wr::wrapText(head, W - bw - sc(20), 1, measure)[0].c_str(), X, A.y + m + bh / 2);
    C.loadFont(kSmall);
    C.setTextDatum(textdatum_t::top_left);
    C.setTextColor(kText2);
    String sub = status.length() ? status
                                 : String((int)items.size()) + (items.size() > 1 ? " morceaux" : " morceau") +
                                       " \xC2\xB7 touche celui \xC3\xA0 \xC3\xA9" "couter";
    C.drawString(wr::wrapText(sub.c_str(), W, 1, measure)[0].c_str(), X, A.y + m + bh + sc(8));
    // Lignes
    const int top = A.y + m + bh + sc(8) + C.fontHeight() + sc(14), navH = sc(56);
    const int rowH = sc(72), gap = sc(8);
    resultsPerPage_ = std::max(1, (A.bottom() - m - navH - sc(10) - top + gap) / (rowH + gap));
    const int pages = std::max(1, ((int)items.size() + resultsPerPage_ - 1) / resultsPerPage_);
    page = std::max(0, std::min(pages - 1, page));
    resultsFirst_ = page * resultsPerPage_;
    resultRects_.clear();
    for (int k = 0; k < resultsPerPage_ && resultsFirst_ + k < (int)items.size(); ++k) {
        const int i = resultsFirst_ + k;
        const wr::Rect r{X, top + k * (rowH + gap), W, rowH};
        resultRects_.push_back(r);
        const uint32_t bg = i == pressed ? kCardHi : kCard;
        C.fillRoundRect(r.x, r.y, r.w, r.h, sc(12), bg);
        // Numéro, titre, artiste · album
        C.loadFont(kSmall);
        C.setTextDatum(textdatum_t::middle_center);
        C.setTextColor(kText2);
        C.drawString(String(i + 1).c_str(), r.x + sc(30), r.y + r.h / 2 + 1);
        const int tx = r.x + sc(60), tw = r.w - sc(80);
        const wr::QueueTrack& t = items[i];
        C.setTextDatum(textdatum_t::top_left);
        C.loadFont(kSmall);
        C.setTextColor(kText);
        C.drawString(wr::wrapText(t.title.empty() ? "?" : t.title, tw, 1, measure)[0].c_str(), tx, r.y + sc(8));
        std::string under = t.artist;
        if (!t.album.empty()) under += (under.empty() ? "" : " \xC2\xB7 ") + t.album;
        C.loadFont(kTiny);
        C.setTextColor(kText2);
        if (!under.empty()) C.drawString(wr::wrapText(under, tw, 1, measure)[0].c_str(), tx, r.y + r.h / 2 + sc(4));
    }
    // Pages
    resultsPrev_ = resultsNext_ = wr::Rect{};
    if (pages > 1) {
        const int ny = A.bottom() - m - navH, nw = sc(200);
        C.loadFont(kSmall);
        C.setTextDatum(textdatum_t::middle_center);
        if (page > 0) {
            resultsPrev_ = wr::Rect{X, ny, nw, navH};
            C.fillRoundRect(X, ny, nw, navH, navH / 2, kCard);
            C.setTextColor(kText);
            C.drawString("Pr\xC3\xA9" "c\xC3\xA9" "dents", X + nw / 2, ny + navH / 2 + 1);
        }
        if (page < pages - 1) {
            resultsNext_ = wr::Rect{X + W - nw, ny, nw, navH};
            C.fillRoundRect(X + W - nw, ny, nw, navH, navH / 2, kCard);
            C.setTextColor(kText);
            C.drawString("Suivants", X + W - nw / 2, ny + navH / 2 + 1);
        }
        C.setTextColor(kText2);
        C.drawString((String(page + 1) + " / " + String(pages)).c_str(), X + W / 2, ny + navH / 2 + 1);
    }
    present();
}

int Ui::hitResult(int x, int y) const {
    for (size_t k = 0; k < resultRects_.size(); ++k)
        if (resultRects_[k].contains(x, y)) return resultsFirst_ + (int)k;
    return -1;
}

int Ui::hitResultsPage(int x, int y) const {
    if (resultsPrev_.contains(x, y)) return -1;
    if (resultsNext_.contains(x, y)) return 1;
    return 0;
}

// Engrenage : dents en couronne, disque, trou central
void Ui::gearIcon(int cx, int cy, int r, uint32_t c, uint32_t bg) {
    auto& C = canvas_;
    const int teeth = 8, tr = std::max(2, r / 4);
    for (int k = 0; k < teeth; ++k) {
        float a = k * 2 * 3.14159265f / teeth;
        C.fillCircle(cx + (int)std::lround(std::cos(a) * r), cy + (int)std::lround(std::sin(a) * r), tr, c);
    }
    C.fillCircle(cx, cy, r * 85 / 100, c);
    C.fillCircle(cx, cy, r * 38 / 100, bg);
}

// Maison : toit, corps, porte
void Ui::homeIcon(int cx, int cy, int s, uint32_t c, uint32_t bg) {
    auto& C = canvas_;
    C.fillTriangle(cx - s - s / 4, cy - s / 8, cx + s + s / 4, cy - s / 8, cx, cy - s - s / 4, c);
    C.fillRect(cx - s * 3 / 4, cy - s / 8, s * 3 / 2, s, c);
    C.fillRect(cx - s / 5, cy + s / 3, s * 2 / 5, s * 2 / 3 - s / 8, bg);
}

// Écran d'écoute : grand micro dont l'anneau suit la voix
void Ui::renderVoice(VoicePhase phase, float level) {
    ambientShown_ = false;
    view_ = View::Other;
    railShown_ = false;
    auto& C = canvas_;
    background();
    const int W = C.width(), H = C.height();
    const int cx = W / 2, cy = H * 38 / 100, R = sc(60);
    const bool listening = phase == VoicePhase::Listening;
    if (listening) {
        const float l = std::max(0.0f, std::min(1.0f, level));
        // Anneaux bornés : à pleine voix, ils restent au-dessus du titre
        C.fillCircle(cx, cy, R + sc(14) + (int)(l * sc(50)), mix(bgAt(cy), accent_, 0.18f));
        C.fillCircle(cx, cy, R + sc(8) + (int)(l * sc(28)), mix(bgAt(cy), accent_, 0.40f));
    } else {
        const int a = (int)((millis() / 3) % 360);
        C.fillArc(cx, cy, R + sc(8), R + sc(14), a, a + 120, accent_);
    }
    C.fillCircle(cx, cy, R, listening ? accent_ : kCardHi);
    micIcon(cx, cy, R / 2, listening ? kBg : kText, listening ? accent_ : kCardHi);

    C.setTextDatum(textdatum_t::top_center);
    C.loadFont(kBig);
    C.setTextColor(kText);
    int y = cy + R + sc(70);
    C.drawString(listening ? "Je t'\xC3\xA9" "coute\xE2\x80\xA6" : "Je r\xC3\xA9" "fl\xC3\xA9" "chis\xE2\x80\xA6", cx, y);
    y += C.fontHeight() + sc(10);
    C.loadFont(kSmall);
    C.setTextColor(kText2);
    if (listening) {
        C.drawString("\xC2\xAB Joue FIP \xC2\xBB \xC2\xB7 \xC2\xAB Mets France Culture \xC2\xBB \xC2\xB7 "
                     "\xC2\xAB Plus fort \xC2\xBB \xC2\xB7 \xC2\xAB Suivant \xC2\xBB",
                     cx, y);
        y += C.fontHeight() + sc(6);
        C.drawString("Touche l'\xC3\xA9" "cran quand tu as fini.", cx, y);
    }
    // Annuler
    C.loadFont(kSmall);
    const int bw = sc(150), bh = sc(44);
    cancelRect_ = wr::Rect{cx - bw / 2, H - sc(30) - bh, bw, bh};
    C.fillRoundRect(cancelRect_.x, cancelRect_.y, bw, bh, bh / 2, kCard);
    C.setTextDatum(textdatum_t::middle_center);
    C.setTextColor(kText);
    C.drawString("Annuler", cx, cancelRect_.y + bh / 2 + 1);
    present();
}

// -------------------------------------------------------------------- zones

bool Ui::hitMic(int x, int y) const {
    if (!voiceOn_) return false;
    const wr::Rect& m = layout_.mic;
    const int pad = sc(10);
    return wr::Rect{m.x - pad, m.y - pad, m.w + 2 * pad, m.h + 2 * pad}.contains(x, y);
}

bool Ui::hitVoiceCancel(int x, int y) const { return cancelRect_.contains(x, y); }

// Boutons de la barre : toute la largeur de la barre compte, et un peu autour
static bool hitNav(const wr::Rect& rail, const wr::Rect& b, int pad, int x, int y) {
    if (rail.h > rail.w) return x >= rail.x && x < rail.right() && y >= b.y - pad && y < b.bottom() + pad;
    return y >= rail.y && y < rail.bottom() && x >= b.x - pad && x < b.right() + pad;
}

bool Ui::hitHome(int x, int y) const {
    if (view_ == View::Ambient) return homeRect_.contains(x, y);
    return railShown_ && hitNav(layout_.rail, layout_.navHome, sc(16), x, y);
}

bool Ui::hitSettings(int x, int y) const {
    return railShown_ && view_ != View::Ambient && hitNav(layout_.rail, layout_.navSettings, sc(16), x, y);
}

int Ui::hitPreset(int x, int y) const {
    for (size_t slot = 0; slot < layout_.presets.size(); ++slot) {
        int i = page_ * layout_.perPage + (int)slot;
        if (i < (int)presets_.size() && layout_.presets[slot].contains(x, y)) return i;
    }
    return -1;
}

int Ui::hitTransport(int x, int y) const {
    // Cercles un peu agrandis : le doigt touche souvent à côté
    const int pad = sc(10);
    for (int i = 0; i < wr::kTransportCount; ++i) {
        const wr::Rect& r = layout_.transport[i];
        wr::Rect big{r.x - pad, r.y - pad, r.w + 2 * pad, r.h + 2 * pad};
        if (i == wr::kVolDown || i == wr::kVolUp) big = r;  // collés au curseur : zone exacte
        if (big.contains(x, y)) return i;
    }
    return -1;
}

bool Ui::hitHeader(int x, int y) const { return layout_.art.contains(x, y) || layout_.info.contains(x, y); }

bool Ui::hitVolume(int x, int y, int& volume) const {
    const wr::Rect& b = layout_.volBar;
    if (!b.contains(x, y)) return false;
    volume = std::max(0, std::min(100, (x - b.x) * 100 / std::max(1, b.w)));
    return true;
}

bool Ui::hitMute(int x, int y) const { return layout_.mute.contains(x, y); }

bool Ui::hitProgress(int x, int y, uint32_t& posMs) const {
    if (!progHit_.contains(x, y) || progDuration_ == 0) return false;
    int dx = std::max(0, std::min(progHit_.w, x - progHit_.x));
    posMs = (uint32_t)((uint64_t)progDuration_ * dx / std::max(1, progHit_.w));
    return true;
}

bool Ui::hitStrip(int x, int y) const { return !presets_.empty() && layout_.strip.contains(x, y); }

int Ui::hitDots(int x, int y) const {
    if (pageCount() < 2) return 0;
    const wr::Rect& d = layout_.dots;
    wr::Rect zone{d.x, d.y - sc(6), d.w, d.h + sc(12)};
    if (!zone.contains(x, y)) return 0;
    return x < d.x + d.w / 2 ? -1 : 1;
}

// -------------------------------------------------------------- plein écran

// Gros bouton de menu : titre et sous-titre, sur carte
static void menuButton(M5Canvas& C, const wr::Rect& r, const char* title, const char* sub, uint32_t dot, int sc16) {
    C.fillRoundRect(r.x, r.y, r.w, r.h, sc16, kCard);
    C.fillCircle(r.x + sc16 * 2, r.y + r.h / 2, sc16 * 2 / 3, dot);
    C.setTextDatum(textdatum_t::top_left);
    C.loadFont(kMid);
    C.setTextColor(kText);
    const int tx = r.x + sc16 * 4;
    C.drawString(title, tx, r.y + r.h / 2 - C.fontHeight());
    C.loadFont(kSmall);
    C.setTextColor(kText2);
    C.drawString(sub, tx, r.y + r.h / 2 + sc16 / 4);
}

void Ui::renderMenu(const char* version) {
    ambientShown_ = false;
    view_ = View::Other;
    railShown_ = false;
    auto& C = canvas_;
    background();
    navRail(1);
    const wr::Rect& A = layout_.content;
    const int W = A.w, H = C.height();
    const int bw = std::min(W - 2 * sc(40), sc(560)), bh = sc(64), gap = sc(10);
    const int x = A.x + (W - bw) / 2, y0 = A.y + sc(100);
    C.setTextDatum(textdatum_t::bottom_center);
    C.loadFont(kBig);
    C.setTextColor(kText);
    C.drawString("R\xC3\xA9glages", A.x + W / 2, y0 - sc(20));
    menuRects_[MenuConfig] = wr::Rect{x, y0, bw, bh};
    menuRects_[MenuUpdate] = wr::Rect{x, y0 + bh + gap, bw, bh};
    menuRects_[MenuTouch] = wr::Rect{x, y0 + 2 * (bh + gap), bw, bh};
    menuRects_[MenuLog] = wr::Rect{x, y0 + 3 * (bh + gap), bw, bh};
    menuButton(C, menuRects_[MenuConfig], "Configuration", "Wi-Fi, WiiM, presets, voix",
               accent_, sc(16));
    menuButton(C, menuRects_[MenuUpdate], "Mise \xC3\xA0 jour", "Installer un nouveau firmware par le Wi-Fi", kText2,
               sc(16));
    menuButton(C, menuRects_[MenuTouch], "Test tactile", "Voir o\xC3\xB9 l'\xC3\xA9" "cran d\xC3\xA9tecte ton doigt",
               kText2, sc(16));
    menuButton(C, menuRects_[MenuLog], "Journal", "Derniers \xC3\xA9v\xC3\xA9nements, pour le diagnostic", kText2, sc(16));
    const int cw = sc(150), ch = sc(44);
    menuRects_[MenuClose] = wr::Rect{A.x + (W - cw) / 2, menuRects_[MenuLog].bottom() + sc(20), cw, ch};
    const wr::Rect& c = menuRects_[MenuClose];
    C.fillRoundRect(c.x, c.y, c.w, c.h, c.h / 2, kCard);
    C.loadFont(kSmall);
    C.setTextDatum(textdatum_t::middle_center);
    C.setTextColor(kText);
    C.drawString("Fermer", c.x + c.w / 2, c.y + c.h / 2 + 1);
    C.setTextDatum(textdatum_t::bottom_right);
    C.setTextColor(kText2);
    C.drawString((String("Version ") + version).c_str(), C.width() - sc(20), H - sc(14));
    present();
}

// Journal : les lignes les plus récentes en bas, sur deux colonnes si besoin
void Ui::renderLog(const std::vector<String>& lines, const char* version) {
    ambientShown_ = false;
    view_ = View::Other;
    railShown_ = false;
    auto& C = canvas_;
    C.fillScreen(kBg);
    navRail(1);
    const wr::Rect& A = layout_.content;
    const int W = C.width(), H = C.height(), m = sc(20), X = A.x + m, Y = A.y + m;
    C.loadFont(kMid);
    C.setTextDatum(textdatum_t::top_left);
    C.setTextColor(kText);
    C.drawString("Journal", X, Y);
    const int bw = sc(150), bh = sc(44), qw = sc(170);
    C.loadFont(kSmall);
    C.setTextColor(kText2);
    const String ver = String("Version ") + version;
    if (X + sc(150) + C.textWidth(ver.c_str()) < W - m - bw - sc(36) - qw - sc(190))  // sinon : version dans Réglages
        C.drawString(ver.c_str(), X + sc(150), Y + sc(6));
    logClose_ = wr::Rect{W - bw - m, Y, bw, bh};
    C.fillRoundRect(logClose_.x, logClose_.y, bw, bh, bh / 2, kCardHi);
    C.setTextDatum(textdatum_t::middle_center);
    C.setTextColor(kText);
    C.drawString("Fermer", logClose_.x + bw / 2, logClose_.y + bh / 2 + 1);
    // Diagnostic de la file de lecture du WiiM (service UPnP PlayQueue)
    logQueue_ = wr::Rect{logClose_.x - sc(12) - qw, Y, qw, bh};
    C.fillRoundRect(logQueue_.x, logQueue_.y, qw, bh, bh / 2, kCard);
    C.drawRoundRect(logQueue_.x, logQueue_.y, qw, bh, bh / 2, accent_);
    C.drawString("File du WiiM", logQueue_.x + qw / 2, logQueue_.y + bh / 2 + 1);
    // Essai : favoris Qobuz lus par le WiiM (sans lancer la lecture)
    const int tw = sc(190);
    logTest_ = wr::Rect{logQueue_.x - sc(12) - tw, Y, tw, bh};
    C.loadFont(kMid);
    const int titleRight = X + C.textWidth("Journal") + sc(16);
    C.loadFont(kSmall);
    if (logTest_.x < titleRight) logTest_.y = Y + bh + sc(10), logTest_.x = logClose_.right() - tw;  // portrait : dessous
    C.fillRoundRect(logTest_.x, logTest_.y, tw, bh, bh / 2, kCard);
    C.drawRoundRect(logTest_.x, logTest_.y, tw, bh, bh / 2, kTrack);
    C.drawString("Essai favoris", logTest_.x + tw / 2, logTest_.y + bh / 2 + 1);
    // Lignes : police fine, pleine largeur ; une ligne longue passe sur 3 lignes au plus.
    // On part de la plus récente (en bas) et on remonte tant qu'il y a de la place.
    C.loadFont(kTiny);
    C.setTextDatum(textdatum_t::top_left);
    const int top = std::max(Y + bh, logTest_.bottom()) + sc(14), lh = C.fontHeight() + sc(2);
    const int rowsFit = std::max(1, (H - top - m) / lh), textW = W - X - m;
    auto measure = [&](const std::string& s) { return (int)C.textWidth(s.c_str()); };
    std::vector<std::pair<std::string, bool>> rows;  // texte, dernière entrée
    for (int i = (int)lines.size() - 1; i >= 0 && (int)rows.size() < rowsFit; --i) {
        auto wrapped = wr::wrapText(lines[i].c_str(), textW, 3, measure);
        for (int k = (int)wrapped.size() - 1; k >= 0 && (int)rows.size() < rowsFit; --k)
            rows.emplace_back((k ? "    " : "") + wrapped[k], i == (int)lines.size() - 1);
    }
    for (int r = 0; r < (int)rows.size(); ++r) {
        const auto& row = rows[rows.size() - 1 - r];
        C.setTextColor(row.second ? kText : kText2);
        C.drawString(row.first.c_str(), X, top + r * lh);
    }
    present();
}

// Test tactile : quadrillage, rond sous le doigt, coordonnées, nombre d'appuis
void Ui::renderTouchTest(int x, int y, int count) {
    ambientShown_ = false;
    view_ = View::Other;
    railShown_ = false;
    auto& C = canvas_;
    C.fillScreen(kBg);
    const int W = C.width(), H = C.height();
    for (int gx = 0; gx <= W; gx += W / 8) C.drawFastVLine(std::min(gx, W - 1), 0, H, kCard);
    for (int gy = 0; gy <= H; gy += H / 6) C.drawFastHLine(0, std::min(gy, H - 1), W, kCard);
    C.drawRect(0, 0, W, H, kText2);
    C.loadFont(kMid);
    C.setTextDatum(textdatum_t::top_left);
    C.setTextColor(kText);
    C.drawString("Test tactile", sc(24), sc(20));
    C.loadFont(kSmall);
    C.setTextColor(kText2);
    C.drawString("Touche l'\xC3\xA9" "cran : un rond doit appara\xC3\xAEtre sous ton doigt.", sc(24), sc(76));
    String info = "\xC3\x89" "cran " + String(W) + " x " + String(H) + "   Appuis : " + String(count);
    if (x >= 0) info += "   Dernier : x " + String(x) + ", y " + String(y);
    C.drawString(info.c_str(), sc(24), sc(110));
    if (x >= 0) {
        C.fillCircle(x, y, sc(34), accent_);
        C.drawFastHLine(0, y, W, accent_);
        C.drawFastVLine(x, 0, H, accent_);
        C.fillCircle(x, y, sc(6), kText);
    }
    const int bw = sc(150), bh = sc(48);
    touchClose_ = wr::Rect{W - bw - sc(20), sc(20), bw, bh};
    C.fillRoundRect(touchClose_.x, touchClose_.y, bw, bh, bh / 2, kCardHi);
    C.setTextDatum(textdatum_t::middle_center);
    C.setTextColor(kText);
    C.drawString("Fermer", touchClose_.x + bw / 2, touchClose_.y + bh / 2 + 1);
    present();
}

void Ui::renderPicker(const std::vector<std::pair<std::string, std::string>>& devices) {
    ambientShown_ = false;
    view_ = View::Other;
    railShown_ = false;
    auto& C = canvas_;
    auto measure = [&](const std::string& t) { return (int)C.textWidth(t.c_str()); };
    background();
    const int W = C.width(), H = C.height(), m = sc(40);
    C.setTextDatum(textdatum_t::top_left);
    C.loadFont(kBig);
    C.setTextColor(kText);
    C.drawString(devices.empty() ? "Aucun WiiM trouv\xC3\xA9" : "Quel WiiM ?", m, m);
    int y = m + C.fontHeight() + sc(16);
    C.loadFont(kSmall);
    C.setTextColor(kText2);
    pickRects_.clear();
    if (devices.empty()) {
        for (auto& l : wr::wrapText("V\xC3\xA9rifie que le WiiM est allum\xC3\xA9 et branch\xC3\xA9 sur le m\xC3\xAA" "me "
                                    "Wi-Fi que le Tab5, puis r\xC3\xA9" "essaie. Tu peux aussi saisir son adresse IP dans la "
                                    "configuration.",
                                    W - 2 * m, 4, measure)) {
            C.drawString(l.c_str(), m, y);
            y += C.fontHeight() + sc(6);
        }
    } else {
        C.drawString("Plusieurs lecteurs sont sur ton Wi-Fi : touche celui \xC3\xA0 commander.", m, y);
        y += C.fontHeight() + sc(20);
        const int cols = W > H ? 2 : 1, gap = sc(16);
        const int cw = (W - 2 * m - (cols - 1) * gap) / cols, ch = sc(84);
        for (size_t i = 0; i < devices.size() && i < 6; ++i) {
            wr::Rect r{m + (int)(i % cols) * (cw + gap), y + (int)(i / cols) * (ch + gap), cw, ch};
            pickRects_.push_back(r);
            C.fillRoundRect(r.x, r.y, r.w, r.h, sc(16), kCard);
            C.fillCircle(r.x + sc(34), r.y + r.h / 2, sc(14), accent_);
            C.loadFont(kMid);
            C.setTextColor(kText);
            C.setTextDatum(textdatum_t::top_left);
            auto name = wr::wrapText(devices[i].first, r.w - sc(80), 1, measure);
            C.drawString(name.empty() ? "WiiM" : name[0].c_str(), r.x + sc(64), r.y + sc(10));
            C.loadFont(kSmall);
            C.setTextColor(kText2);
            C.drawString(devices[i].second.c_str(), r.x + sc(64), r.y + r.h - sc(10) - C.fontHeight());
        }
    }
    // Réessayer / Configuration
    const int bw = sc(190), bh = sc(48), gap = sc(16);
    pickRetry_ = wr::Rect{W / 2 - gap / 2 - bw, H - m - bh, bw, bh};
    pickConfig_ = wr::Rect{W / 2 + gap / 2, H - m - bh, bw, bh};
    C.loadFont(kSmall);
    C.setTextDatum(textdatum_t::middle_center);
    for (auto* b : {&pickRetry_, &pickConfig_}) {
        C.fillRoundRect(b->x, b->y, b->w, b->h, b->h / 2, b == &pickRetry_ && devices.empty() ? accent_ : kCard);
        C.setTextColor(b == &pickRetry_ && devices.empty() ? kBg : kText);
        C.drawString(b == &pickRetry_ ? "R\xC3\xA9" "essayer" : "Configuration", b->x + b->w / 2, b->y + b->h / 2 + 1);
    }
    present();
}

int Ui::hitPicker(int x, int y) const {
    for (size_t i = 0; i < pickRects_.size(); ++i)
        if (pickRects_[i].contains(x, y)) return (int)i;
    if (pickRetry_.contains(x, y)) return PickRetry;
    if (pickConfig_.contains(x, y)) return PickConfig;
    return PickNone;
}

Ui::MenuChoice Ui::hitMenu(int x, int y) const {
    for (int i = 0; i < 5; ++i)
        if (menuRects_[i].contains(x, y)) return static_cast<MenuChoice>(i);
    return MenuNone;
}

void Ui::showUpdate(const String& url, const String& code, const char* version, int percent, const String& status) {
    ambientShown_ = false;
    view_ = View::Other;
    railShown_ = false;
    auto& C = canvas_;
    background();
    const int W = C.width(), H = C.height(), m = sc(40);
    C.setTextDatum(textdatum_t::top_left);
    C.loadFont(kBig);
    C.setTextColor(kText);
    C.drawString("Mise \xC3\xA0 jour", m, m);
    int y = m + C.fontHeight() + sc(24);
    C.loadFont(kSmall);
    C.setTextColor(kText2);
    C.drawString("1. Sur ton ordinateur (m\xC3\xAAme Wi-Fi), ouvre :", m, y);
    y += C.fontHeight() + sc(8);
    C.loadFont(kBig);
    C.setTextColor(accent_);
    C.drawString(url.c_str(), m + sc(24), y);
    y += C.fontHeight() + sc(18);
    C.loadFont(kSmall);
    C.setTextColor(kText2);
    C.drawString("2. Saisis ce code, puis choisis le fichier wiim-remote-tab5.bin :", m, y);
    y += C.fontHeight() + sc(8);
    // Code en gros, chiffres espacés
    C.loadFont(kBig);
    std::string spaced;
    for (const char* c = code.c_str(); *c; ++c) {
        if (c != code.c_str()) spaced += ' ';
        spaced += *c;
    }
    const int cw = (int)C.textWidth(spaced.c_str()) + sc(48), ch = C.fontHeight() + sc(20);
    C.fillRoundRect(m + sc(24), y, cw, ch, sc(14), kCard);
    C.setTextColor(kText);
    C.setTextDatum(textdatum_t::middle_center);
    C.drawString(spaced.c_str(), m + sc(24) + cw / 2, y + ch / 2 + 2);
    y += ch + sc(30);

    // Progression ou état
    C.loadFont(kSmall);
    C.setTextDatum(textdatum_t::top_left);
    if (percent >= 0) {
        const int bw = W - 2 * m, bh = sc(14);
        C.fillRoundRect(m, y, bw, bh, bh / 2, kTrack);
        C.fillRoundRect(m, y, std::max(bh, bw * std::min(100, percent) / 100), bh, bh / 2, accent_);
        y += bh + sc(12);
    }
    if (status.length()) {
        C.setTextColor(kText);
        C.drawString(status.c_str(), m, y);
    }
    // Version et bouton Annuler
    C.setTextColor(kText2);
    C.setTextDatum(textdatum_t::bottom_left);
    C.drawString((String("Version install\xC3\xA9" "e : ") + version).c_str(), m, H - sc(24));
    const int bw = sc(150), bh = sc(44);
    cancelRect_ = wr::Rect{W - m - bw, H - sc(24) - bh, bw, bh};
    C.fillRoundRect(cancelRect_.x, cancelRect_.y, bw, bh, bh / 2, kCard);
    C.setTextDatum(textdatum_t::middle_center);
    C.setTextColor(kText);
    C.drawString("Annuler", cancelRect_.x + bw / 2, cancelRect_.y + bh / 2 + 1);
    present();
}


void Ui::showInfo(const char* title, const String& body, const char* footer) {
    ambientShown_ = false;
    view_ = View::Other;
    railShown_ = false;
    auto& C = canvas_;
    auto measure = [&](const std::string& s) { return (int)C.textWidth(s.c_str()); };
    accent_ = kAccent;
    background();
    const int m = sc(40), w = C.width() - 2 * m;
    C.loadFont(kBig);
    C.setTextDatum(textdatum_t::top_left);
    C.setTextColor(kText);
    C.drawString(title, m, m);
    C.loadFont(kSmall);
    int y = m + C.fontHeight() + sc(60);
    for (auto& l : wr::wrapText(body.c_str(), w, 12, measure)) {
        C.drawString(l.c_str(), m, y);
        y += C.fontHeight() + sc(8);
    }
    if (footer) {
        C.setTextColor(kText2);
        C.setTextDatum(textdatum_t::bottom_left);
        C.drawString(footer, m, C.height() - m);
    }
    present();
}

void Ui::showPortal(const String& apName, const String& apPass, const String& url, const String& reason) {
    ambientShown_ = false;
    view_ = View::Other;
    railShown_ = false;
    auto& C = canvas_;
    auto measure = [&](const std::string& s) { return (int)C.textWidth(s.c_str()); };
    accent_ = kAccent;
    background();
    const int m = sc(32);
    const bool landscape = C.width() > C.height();
    const int qr = landscape ? C.height() - 2 * m - sc(40) : C.width() - 2 * m - sc(80);

    // QR code pour rejoindre le réseau de configuration avec un téléphone :
    // modules noirs sur carte blanche, lisibles par tous les appareils photo
    String wifiQr = "WIFI:T:WPA;S:" + apName + ";P:" + apPass + ";;";
    int qx = landscape ? C.width() - m - qr : (C.width() - qr) / 2;
    int qy = landscape ? (C.height() - qr) / 2 : C.height() - m - qr;
    const int pad = sc(16);
    C.fillRoundRect(qx - pad, qy - pad, qr + 2 * pad, qr + 2 * pad, sc(16), kText);
    C.qrcode(wifiQr.c_str(), qx, qy, qr);

    const int textW = landscape ? qx - pad - 2 * m : C.width() - 2 * m;
    int y = m;
    C.setTextDatum(textdatum_t::top_left);
    C.loadFont(kBig);
    C.setTextColor(kText);
    C.drawString("Configuration", m, y);
    y += C.fontHeight() + sc(12);

    // Paragraphe avec retrait (sous le numéro d'étape)
    const int indent = sc(46);
    auto para = [&](const String& s, const uint8_t* font, uint32_t color, int x) {
        C.loadFont(font);
        C.setTextColor(color);
        const int lh = C.fontHeight() + sc(6);
        for (auto& l : wr::wrapText(s.c_str(), textW - (x - m), 3, measure)) {
            C.drawString(l.c_str(), x, y);
            y += lh;
        }
    };
    // Numéro d'étape dans une pastille de la couleur d'accent
    auto step = [&](int n) {
        const int rad = sc(15);
        C.fillCircle(m + rad, y + rad + sc(2), rad, accent_);
        C.loadFont(kSmall);
        C.setTextDatum(textdatum_t::middle_center);
        C.setTextColor(kBg);
        C.drawString(String(n).c_str(), m + rad, y + rad + sc(3));
        C.setTextDatum(textdatum_t::top_left);
    };

    if (reason.length()) para(reason, kSmall, kText2, m);
    y += sc(14);
    step(1);
    para("Scanne le QR code avec ton téléphone, ou rejoins le Wi-Fi :", kSmall, kText, m + indent);
    para(apName, kMid, kText, m + indent);
    para("mot de passe : " + apPass, kMid, kText, m + indent);
    y += sc(14);
    step(2);
    para("Si la page ne s'ouvre pas, va sur " + url, kSmall, kText, m + indent);
    y += sc(14);
    step(3);
    para("Choisis ton Wi-Fi et tape son mot de passe : le Tab5 trouve ton WiiM tout seul.", kSmall, kText,
         m + indent);
    present();
}
