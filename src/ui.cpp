#include "ui.h"

#include <algorithm>
#include <cmath>

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
    columns_ = s.columns;
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
    layout_ = wr::computeLayout(D.width(), D.height(), columns_, (int)presets_.size());
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
    relayout();
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

void Ui::setPresetArt(int i, const ArtImage* img) {
    if (i < 0 || i >= (int)presets_.size()) return;
    presetArtSrc_[i] = img;
    thumbs_[i].reset();
    if (!img || !img->ok()) return;
    const wr::Rect& r = layout_.presets[i];
    const int side = r.h - 2 * sc(9);
    if (side < sc(32)) return;  // bouton trop petit pour une image lisible
    auto t = std::unique_ptr<M5Canvas>(new M5Canvas());
    t->setPsram(true);
    t->setColorDepth(16);
    if (!t->createSprite(side, side)) return;
    if (decode(*t, img, kCard)) thumbs_[i] = std::move(t);
}

// --------------------------------------------------------------------- éléments

// Bouton de preset : carte sombre ; avec image à gauche et nom aligné à gauche,
// sinon trait d'accent et nom centré. Appuyé : rempli de la couleur d'accent.
void Ui::button(int index, bool pressed) {
    auto& C = canvas_;
    const wr::Rect& r = layout_.presets[index];
    const std::string& label = presets_[index].label;
    const int R = sc(14);
    const uint32_t card = pressed ? accent_ : kCard;
    C.fillRoundRect(r.x, r.y, r.w, r.h, R, card);

    M5Canvas* thumb = thumbs_[index].get();
    int textX = r.x + sc(28), textW = r.w - sc(44);
    if (thumb) {
        const int pad = sc(9);
        roundedPush(*thumb, r.x + pad, r.y + pad, sc(8), false, card);
        textX = r.x + pad + thumb->width() + sc(14);
        textW = r.x + r.w - sc(14) - textX;
    } else if (!pressed) {
        int bh = std::max(sc(20), r.h * 2 / 5);
        C.fillRoundRect(r.x + sc(10), r.y + (r.h - bh) / 2, sc(5), bh, sc(3), accent_);
    }

    // Grande police si le nom tient en entier, sinon plus petite sur deux lignes
    C.setTextColor(pressed ? kBg : kText);
    C.setTextDatum(thumb ? textdatum_t::middle_left : textdatum_t::middle_center);
    auto measure = [&](const std::string& s) { return (int)C.textWidth(s.c_str()); };
    std::vector<std::string> lines;
    int lineH = 0;
    for (const uint8_t* font : {kMid, kSmall}) {
        C.loadFont(font);
        lineH = C.fontHeight() + sc(4);
        int maxLines = std::max(1, std::min(2, (r.h - sc(12)) / lineH));
        lines = wr::wrapText(label, textW, maxLines, measure);
        if (!endsWithEllipsis(lines)) break;
    }
    int y = r.y + r.h / 2 - (int)(lines.size() - 1) * lineH / 2;
    const int x = thumb ? textX : textX + textW / 2;
    for (auto& l : lines) {
        C.drawString(l.c_str(), x, y);
        y += lineH;
    }
}

void Ui::transportIcon(int which, const wr::Rect& r, bool pressed, bool playing) {
    auto& C = canvas_;
    const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    uint32_t fg = kText;
    if (which == wr::kPlayPause) {
        // Bouton principal : disque de la couleur d'accent
        int d = std::min(r.w, r.h);
        C.fillCircle(cx, cy, d / 2, pressed ? kText : accent_);
        fg = kBg;
    } else {
        C.fillRoundRect(r.x, r.y, r.w, r.h, r.h / 2, pressed ? kCardHi : kCard);
    }

    const int s = std::min(r.w, r.h) / 5;  // demi-taille de l'icône
    const int t = std::max(4, s / 4);      // épaisseur des traits
    switch (which) {
        case wr::kVolDown:
            C.fillRoundRect(cx - s, cy - t / 2, 2 * s, t, t / 2, fg);
            break;
        case wr::kVolUp:
            C.fillRoundRect(cx - s, cy - t / 2, 2 * s, t, t / 2, fg);
            C.fillRoundRect(cx - t / 2, cy - s, t, 2 * s, t / 2, fg);
            break;
        case wr::kPrev:
            C.fillRect(cx - s - t, cy - s, t, 2 * s, fg);
            C.fillTriangle(cx + s / 4, cy - s, cx + s / 4, cy + s, cx - s, cy, fg);
            C.fillTriangle(cx + s + s / 4, cy - s, cx + s + s / 4, cy + s, cx, cy, fg);
            break;
        case wr::kNext:
            C.fillRect(cx + s, cy - s, t, 2 * s, fg);
            C.fillTriangle(cx - s / 4, cy - s, cx - s / 4, cy + s, cx + s, cy, fg);
            C.fillTriangle(cx - s - s / 4, cy - s, cx - s - s / 4, cy + s, cx, cy, fg);
            break;
        case wr::kPlayPause:
            if (playing) {  // pause
                C.fillRoundRect(cx - s * 2 / 3, cy - s, s / 2, 2 * s, 3, fg);
                C.fillRoundRect(cx + s / 6, cy - s, s / 2, 2 * s, 3, fg);
            } else {        // lecture, légèrement décalée pour paraître centrée
                C.fillTriangle(cx - s / 2, cy - s, cx - s / 2, cy + s, cx + s, cy, fg);
            }
            break;
    }
}

// Progression : « 1:23 ━━━━━━━━──── 4:56 », ou « ● EN DIRECT » pour un flux sans durée
void Ui::progressRow(const UiState& st, int x, int w, int cy, wr::Rect* hit) {
    auto& C = canvas_;
    if (hit) *hit = wr::Rect{};
    if (!st.online || (!st.player.playing && st.player.title.empty())) return;
    C.loadFont(kSmall);
    if (st.player.durationMs == 0) {
        if (!st.player.playing) return;
        C.fillCircle(x + sc(6), cy, sc(6), accent_);
        C.setTextDatum(textdatum_t::middle_left);
        C.setTextColor(kText2);
        C.drawString("EN DIRECT", x + sc(20), cy);
        return;
    }
    const uint32_t pos = std::min(st.player.positionMs, st.player.durationMs);
    const int gutter = sc(64);
    C.setTextColor(kText2);
    C.setTextDatum(textdatum_t::middle_left);
    C.drawString(wr::formatTime(pos).c_str(), x, cy);
    C.setTextDatum(textdatum_t::middle_right);
    C.drawString(wr::formatTime(st.player.durationMs).c_str(), x + w, cy);
    const int bx = x + gutter, bw = w - 2 * gutter, bh = sc(6);
    C.fillRoundRect(bx, cy - bh / 2, bw, bh, bh / 2, kTrack);
    int fill = (int)((uint64_t)bw * pos / st.player.durationMs);
    if (fill > 0) C.fillRoundRect(bx, cy - bh / 2, std::max(bh, fill), bh, bh / 2, accent_);
    if (hit) *hit = wr::Rect{bx, cy - sc(26), bw, sc(48)};
}

// Infos de lecture : état, titre, artiste, album (ou message), progression, volume
void Ui::info(const UiState& st) {
    auto& C = canvas_;
    const wr::Rect& r = layout_.info;
    const bool roomy = r.h >= sc(160);  // assez de place pour l'album et les barres
    auto measure = [&](const std::string& s) { return (int)C.textWidth(s.c_str()); };
    auto fit = [&](const std::string& s, int width) {
        auto l = wr::wrapText(s, width, 1, measure);
        return l.empty() ? std::string() : l[0];
    };
    volHit_ = muteHit_ = progHit_ = wr::Rect{};
    progDuration_ = st.player.durationMs;

    // --- Ligne d'état : ▶ EN LECTURE · Spotify · 16 bits · 44,1 kHz        85 % [■■■ ]
    C.loadFont(kSmall);
    C.setTextDatum(textdatum_t::top_right);
    const int lineH = C.fontHeight();
    int right = r.x + r.w;
    if (st.battery >= 0) {
        const int bw = sc(36), bh = sc(18), by = r.y + (lineH - bh) / 2;
        int bx = right - bw - sc(4);
        C.drawRoundRect(bx, by, bw, bh, sc(4), kText2);
        C.drawRoundRect(bx + 1, by + 1, bw - 2, bh - 2, sc(4) - 1, kText2);
        C.fillRect(bx + bw, by + bh / 2 - sc(4), sc(4), sc(8), kText2);
        int inner = (bw - sc(8)) * std::max(0, std::min(100, st.battery)) / 100;
        if (inner > 0) C.fillRect(bx + sc(4), by + sc(4), inner, bh - sc(8), st.battery <= 15 ? 0xE5484Du : kText);
        right = bx - sc(10);
        C.setTextColor(kText2);
        String b = String(st.battery) + " %";
        C.drawString(b.c_str(), right, r.y);
        right -= C.textWidth(b.c_str()) + sc(16);
    }
    if (!roomy && st.online) {  // bandeau compact : volume en texte
        C.setTextColor(kText);
        String v = st.player.muted ? String("Muet") : "Vol " + String(st.player.volume);
        C.drawString(v.c_str(), right, r.y);
        right -= C.textWidth(v.c_str()) + sc(16);
    }

    int x = r.x;
    const int iconH = sc(16), iconY = r.y + (lineH - iconH) / 2;
    std::string state;
    if (!st.online) {
        state = "HORS LIGNE";
    } else if (st.player.playing) {
        state = "EN LECTURE";
        C.fillTriangle(x, iconY, x, iconY + iconH, x + sc(13), iconY + iconH / 2, accent_);
        x += sc(24);
    } else if (!st.player.title.empty()) {
        state = "EN PAUSE";
        C.fillRect(x, iconY, sc(5), iconH, accent_);
        C.fillRect(x + sc(9), iconY, sc(5), iconH, accent_);
        x += sc(24);
    } else {
        state = "À L'ARRÊT";
    }
    if (st.online) {
        const std::string dot = " \xC2\xB7 ";
        std::string src = wr::sourceName(st.player.mode);
        std::string q = wr::formatQuality(st.meta.sampleRate, st.meta.bitDepth);
        if (!src.empty()) state += dot + src;
        if (!q.empty()) state += dot + q;
    }
    C.setTextDatum(textdatum_t::top_left);
    C.setTextColor(kText2);
    C.drawString(fit(state, right - x).c_str(), x, r.y);

    // --- Barres (bas du bandeau) : progression puis volume
    const int volCy = r.y + r.h - sc(12);
    const int progCy = volCy - sc(34);
    const int textBottom = roomy ? progCy - sc(16) : r.y + r.h;

    // --- Titre, artiste, album (ou message)
    int y = r.y + lineH + sc(8);
    C.loadFont(roomy ? kBig : kMid);
    std::string title = st.player.title.empty() ? (st.online ? "Rien en lecture" : "\xE2\x80\x94")
                                                : st.player.title;
    C.setTextColor(st.player.title.empty() ? kText2 : kText);
    C.drawString(fit(title, r.w).c_str(), r.x, y);
    y += C.fontHeight() + sc(2);

    C.loadFont(roomy ? kMid : kSmall);
    const int artistH = C.fontHeight() + sc(2);
    if (!st.player.artist.empty() && y + artistH <= textBottom + sc(4)) {
        C.setTextColor(kText);
        C.drawString(fit(st.player.artist, r.w).c_str(), r.x, y);
        y += artistH;
    }
    C.loadFont(kSmall);
    const int lh = C.fontHeight() + sc(2);
    std::string album = st.player.album.empty() ? st.meta.album : st.player.album;
    if (y + lh <= textBottom + sc(4)) {
        if (st.message.length()) {
            C.setTextColor(accent_);  // message prioritaire, à la place de l'album
            C.drawString(fit(st.message.c_str(), r.w).c_str(), r.x, y);
        } else if (!album.empty()) {
            C.setTextColor(kText2);
            C.drawString(fit(album, r.w).c_str(), r.x, y);
        }
    }
    if (!roomy) return;

    progressRow(st, r.x, r.w, progCy, &progHit_);

    // Volume : 🔈 ━━━━━━●──── 42 (l'icône coupe/rétablit le son)
    const int gutter = sc(64);
    const uint32_t c = st.online ? kText2 : kTrack;
    const int sx = r.x + sc(8);
    C.fillRect(sx, volCy - sc(5), sc(7), sc(10), c);
    C.fillTriangle(sx + sc(4), volCy, sx + sc(15), volCy - sc(11), sx + sc(15), volCy + sc(11), c);
    if (st.player.muted) {
        const int mx = sx + sc(22);
        for (int k = -1; k <= 1; ++k) {
            C.drawLine(mx, volCy - sc(6) + k, mx + sc(12), volCy + sc(6) + k, c);
            C.drawLine(mx, volCy + sc(6) + k, mx + sc(12), volCy - sc(6) + k, c);
        }
    } else {
        C.fillArc(sx + sc(15), volCy, sc(7), sc(9), -50, 50, c);
        C.fillArc(sx + sc(15), volCy, sc(13), sc(15), -50, 50, c);
    }
    C.setTextDatum(textdatum_t::middle_right);
    C.setTextColor(st.online ? kText : kText2);
    String v = !st.online ? String("\xE2\x80\x94") : st.player.muted ? String("Muet") : String(st.player.volume);
    C.drawString(v.c_str(), r.x + r.w, volCy);
    const int bx = r.x + gutter, bw = r.w - 2 * gutter, bh = sc(6);
    C.fillRoundRect(bx, volCy - bh / 2, bw, bh, bh / 2, kTrack);
    int fill = bw * std::max(0, std::min(100, st.player.volume)) / 100;
    if (st.online) {
        uint32_t fc = st.player.muted ? kTrack : kText;
        if (fill > 0) C.fillRoundRect(bx, volCy - bh / 2, std::max(bh, fill), bh, bh / 2, fc);
        C.fillCircle(bx + fill, volCy, sc(9), st.player.muted ? kText2 : kText);  // curseur
        // Zones tactiles généreuses (un doigt couvre ~8 mm)
        volHit_ = wr::Rect{bx - sc(12), progCy + sc(17), bw + sc(24), r.y + r.h + sc(10) - (progCy + sc(17))};
        muteHit_ = wr::Rect{r.x, volHit_.y, gutter - sc(12), volHit_.h};
    }
}

// ---------------------------------------------------------------------- vues

void Ui::render(const UiState& st, int pressedPreset, int pressedTransport) {
    background();
    roundedPush(art_, layout_.art.x, layout_.art.y, sc(16), true, 0);
    info(st);
    for (size_t i = 0; i < presets_.size(); ++i) button((int)i, (int)i == pressedPreset);
    for (int i = 0; i < wr::kTransportCount; ++i)
        transportIcon(i, layout_.transport[i], i == pressedTransport, st.player.playing);
    present();
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

    // Textes à droite (paysage) ou dessous (portrait), centrés verticalement
    const int tx = landscape ? m + side + sc(40) : m;
    const int tw = landscape ? W - tx - m : W - 2 * m;
    const int top = landscape ? m : m + side + sc(30);
    const int bottom = H - m;

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
    progressRow(st, tx, tw, y + sc(44), nullptr);
    present();
}

// -------------------------------------------------------------------- zones

int Ui::hitPreset(int x, int y) const {
    for (size_t i = 0; i < layout_.presets.size(); ++i)
        if (layout_.presets[i].contains(x, y)) return (int)i;
    return -1;
}

int Ui::hitTransport(int x, int y) const {
    for (int i = 0; i < wr::kTransportCount; ++i)
        if (layout_.transport[i].contains(x, y)) return i;
    return -1;
}

bool Ui::hitHeader(int x, int y) const { return layout_.header.contains(x, y); }

bool Ui::hitVolume(int x, int y, int& volume) const {
    if (!volHit_.contains(x, y)) return false;
    const int bx = volHit_.x + sc(12), bw = volHit_.w - sc(24);
    volume = std::max(0, std::min(100, (x - bx) * 100 / std::max(1, bw)));
    return true;
}

bool Ui::hitMute(int x, int y) const { return muteHit_.contains(x, y); }

bool Ui::hitProgress(int x, int y, uint32_t& posMs) const {
    if (!progHit_.contains(x, y) || progDuration_ == 0) return false;
    int dx = std::max(0, std::min(progHit_.w, x - progHit_.x));
    posMs = (uint32_t)((uint64_t)progDuration_ * dx / std::max(1, progHit_.w));
    return true;
}

// -------------------------------------------------------------- plein écran

void Ui::showInfo(const char* title, const String& body, const char* footer) {
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
    para("Renseigne ton Wi-Fi et l'IP du WiiM.", kSmall, kText, m + indent);
    present();
}
