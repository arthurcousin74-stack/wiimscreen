#include "ui.h"

#include <algorithm>
#include <cmath>

#include "fonts/font_ui_24.h"
#include "fonts/font_ui_32.h"
#include "fonts/font_ui_40.h"

static auto& D = M5.Display;

// Couleurs en RGB888 (uint32_t) : M5GFX interpréterait un uint16_t comme du RGB565
static constexpr uint32_t kWhite = 0xFFFFFFu;
static constexpr uint32_t kBlack = 0x000000u;
static constexpr uint32_t kGray = 0x707070u;    // texte secondaire
static constexpr uint32_t kShadow = 0xA0A0A0u;  // ombre des boutons
static constexpr uint32_t kPale = 0xE4E4E4u;    // fonds discrets

static constexpr int kRadius = 16;

static int measure(const std::string& s) { return D.textWidth(s.c_str()); }

// Première ligne (avec « … » si elle déborde)
static std::string fit(const std::string& s, int width) {
    auto l = wr::wrapText(s, width, 1, measure);
    return l.empty() ? std::string() : l[0];
}

void Ui::begin(const Settings& s) {
    D.setRotation(s.rotation);
    D.setAutoDisplay(false);
    presets_ = s.presets;
    layout_ = wr::computeLayout(D.width(), D.height(), s.columns, (int)presets_.size());
}

void Ui::beginDraw(epd_mode_t mode) {
    D.waitDisplay();  // fin du rafraîchissement précédent avant de toucher au tampon
    D.setEpdMode(mode);
    D.startWrite();
}

void Ui::endDraw(bool partial) {
    D.endWrite();
    D.display();
    if (partial) ++partialCount_;
}

// --------------------------------------------------------------------- éléments

// Bouton « carte » avec ombre portée ; enfoncé, il descend sur son ombre et s'inverse.
void Ui::button(const wr::Rect& r, const std::string& label, bool pressed) {
    const int sh = 5;  // décalage de l'ombre
    D.fillRect(r.x, r.y, r.w, r.h, kWhite);
    wr::Rect c{r.x, r.y, r.w - sh, r.h - sh};
    if (pressed) {
        c.x += sh;
        c.y += sh;
    } else {
        D.fillRoundRect(r.x + sh, r.y + sh, r.w - sh, r.h - sh, kRadius, kShadow);
    }
    uint32_t bg = pressed ? kBlack : kWhite, fg = pressed ? kWhite : kBlack;
    D.fillRoundRect(c.x, c.y, c.w, c.h, kRadius, bg);
    for (int i = 0; i < 2; ++i) D.drawRoundRect(c.x + i, c.y + i, c.w - 2 * i, c.h - 2 * i, kRadius - i, kBlack);

    // Grande police si le nom tient en entier, sinon plus petite sur deux lignes
    D.setTextColor(fg, bg);
    D.setTextDatum(textdatum_t::middle_center);
    std::vector<std::string> lines;
    int lineH = 0;
    for (const uint8_t* font : {font_ui_32, font_ui_24}) {
        D.loadFont(font);
        lineH = D.fontHeight() + 4;
        int maxLines = std::max(1, std::min(2, (c.h - 12) / lineH));
        lines = wr::wrapText(label, c.w - 28, maxLines, measure);
        const std::string ell = "\xE2\x80\xA6";
        bool cut = !lines.empty() && lines.back().size() >= ell.size() &&
                   lines.back().compare(lines.back().size() - ell.size(), ell.size(), ell) == 0;
        if (!cut) break;
    }
    int y = c.y + c.h / 2 - (int)(lines.size() - 1) * lineH / 2;
    for (auto& l : lines) {
        D.drawString(l.c_str(), c.x + c.w / 2, y);
        y += lineH;
    }
}

void Ui::transportIcon(int which, const wr::Rect& r, bool pressed, bool playing) {
    // Lecture/pause mis en avant (fond noir), les autres en gris clair
    const bool primary = which == wr::kPlayPause;
    uint32_t bg = primary ? kBlack : kPale;
    if (pressed) bg = primary ? kWhite : kBlack;
    uint32_t fg = (bg == kBlack) ? kWhite : kBlack;

    D.fillRect(r.x, r.y, r.w, r.h, kWhite);
    D.fillRoundRect(r.x, r.y, r.w, r.h, kRadius, bg);
    if (bg == kWhite)
        for (int i = 0; i < 2; ++i) D.drawRoundRect(r.x + i, r.y + i, r.w - 2 * i, r.h - 2 * i, kRadius - i, kBlack);

    const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    const int s = std::min(r.w, r.h) / 4;  // demi-taille de l'icône
    const int t = std::max(4, s / 4);      // épaisseur des traits
    switch (which) {
        case wr::kVolDown:
            D.fillRoundRect(cx - s, cy - t / 2, 2 * s, t, t / 2, fg);
            break;
        case wr::kVolUp:
            D.fillRoundRect(cx - s, cy - t / 2, 2 * s, t, t / 2, fg);
            D.fillRoundRect(cx - t / 2, cy - s, t, 2 * s, t / 2, fg);
            break;
        case wr::kPrev:
            D.fillRect(cx - s - t, cy - s, t, 2 * s, fg);
            D.fillTriangle(cx + s / 4, cy - s, cx + s / 4, cy + s, cx - s, cy, fg);
            D.fillTriangle(cx + s + s / 4, cy - s, cx + s + s / 4, cy + s, cx, cy, fg);
            break;
        case wr::kNext:
            D.fillRect(cx + s, cy - s, t, 2 * s, fg);
            D.fillTriangle(cx - s / 4, cy - s, cx - s / 4, cy + s, cx + s, cy, fg);
            D.fillTriangle(cx - s - s / 4, cy - s, cx - s - s / 4, cy + s, cx, cy, fg);
            break;
        case wr::kPlayPause:
            if (playing) {  // pause
                D.fillRoundRect(cx - s * 2 / 3, cy - s, s / 2, 2 * s, 3, fg);
                D.fillRoundRect(cx + s / 6, cy - s, s / 2, 2 * s, 3, fg);
            } else {        // lecture
                D.fillTriangle(cx - s * 2 / 3, cy - s, cx - s * 2 / 3, cy + s, cx + s, cy, fg);
            }
            break;
    }
}

// Pile : contour, niveau en noir, plot à droite
static void batteryIcon(int x, int y, int level) {
    const int w = 38, h = 20;
    for (int i = 0; i < 2; ++i) D.drawRoundRect(x + i, y + i, w - 2 * i, h - 2 * i, 4, kBlack);
    D.fillRect(x + w, y + h / 2 - 5, 4, 10, kBlack);
    int inner = (w - 8) * std::max(0, std::min(100, level)) / 100;
    if (inner > 0) D.fillRect(x + 4, y + 4, inner, h - 8, kBlack);
}

static void speakerIcon(int x, int cy, uint32_t c) {
    D.fillRect(x, cy - 5, 7, 10, c);
    D.fillTriangle(x + 4, cy, x + 15, cy - 11, x + 15, cy + 11, c);
    D.fillArc(x + 15, cy, 7, 9, -50, 50, c);
    D.fillArc(x + 15, cy, 13, 15, -50, 50, c);
}

// Coins arrondis sur une image rectangulaire (masque blanc hors de l'arrondi)
static void roundCorners(const wr::Rect& a, int R, uint32_t bg) {
    for (int i = 0; i < R; ++i) {
        float dy = R - i - 0.5f;
        int dx = R - (int)std::lround(std::sqrt(float(R * R) - dy * dy));
        if (dx <= 0) continue;
        D.fillRect(a.x, a.y + i, dx, 1, bg);
        D.fillRect(a.x + a.w - dx, a.y + i, dx, 1, bg);
        D.fillRect(a.x, a.y + a.h - 1 - i, dx, 1, bg);
        D.fillRect(a.x + a.w - dx, a.y + a.h - 1 - i, dx, 1, bg);
    }
}

// Illustration par défaut : deux croches sur fond gris clair
void Ui::placeholderArt(const wr::Rect& a) {
    D.fillRoundRect(a.x, a.y, a.w, a.h, kRadius, kPale);
    const int s = std::min(a.w, a.h);
    const int cx = a.x + a.w / 2, cy = a.y + a.h / 2;
    const uint32_t c = 0x8C8C8Cu;
    const int rx = s * 11 / 100, ry = s * 8 / 100, t = std::max(3, s * 4 / 100), bt = s * 9 / 100;
    const int h1x = cx - s * 15 / 100, h1y = cy + s * 20 / 100;
    const int h2x = cx + s * 17 / 100, h2y = cy + s * 14 / 100;
    const int s1x = h1x + rx - t, s2x = h2x + rx - t;
    const int top1 = cy - s * 22 / 100, top2 = cy - s * 28 / 100;
    D.fillEllipse(h1x, h1y, rx, ry, c);
    D.fillEllipse(h2x, h2y, rx, ry, c);
    D.fillRect(s1x, top1, t, h1y - top1, c);
    D.fillRect(s2x, top2, t, h2y - top2, c);
    D.fillTriangle(s1x, top1, s2x + t, top2, s2x + t, top2 + bt, c);
    D.fillTriangle(s1x, top1, s2x + t, top2 + bt, s1x, top1 + bt, c);
}

void Ui::art(const ArtImage* img) {
    const wr::Rect& a = layout_.art;
    D.fillRect(a.x, a.y, a.w, a.h, kWhite);
    bool drawn = false;
    if (img && img->ok()) {
        D.setClipRect(a.x, a.y, a.w, a.h);
        // Échelle 0 : l'image est ajustée au cadre en gardant ses proportions, centrée
        switch (wr::sniffImage(img->data, img->len)) {
            case wr::ImageType::Jpeg:
                drawn = D.drawJpg(img->data, img->len, a.x, a.y, a.w, a.h, 0, 0, 0.0f, 0.0f,
                                  datum_t::middle_center);
                break;
            case wr::ImageType::Png:
                drawn = D.drawPng(img->data, img->len, a.x, a.y, a.w, a.h, 0, 0, 0.0f, 0.0f,
                                  datum_t::middle_center);
                break;
            default: break;
        }
        D.clearClipRect();
        if (drawn) roundCorners(a, kRadius, kWhite);
    }
    if (!drawn) placeholderArt(a);
}

void Ui::info(const UiState& st) {
    const wr::Rect& r = layout_.info;
    const bool roomy = r.h >= 160;  // assez de place pour l'album et la barre de volume
    D.fillRect(r.x, r.y, r.w, r.h, kWhite);

    // --- Ligne d'état : ▶ EN LECTURE · Spotify · 16 bits · 44,1 kHz        85 % [■■■ ]
    D.loadFont(font_ui_24);
    D.setTextDatum(textdatum_t::top_right);
    int right = r.x + r.w;
    if (st.battery >= 0) {
        batteryIcon(right - 42, r.y + 3, st.battery);
        right -= 52;
        D.setTextColor(kGray, kWhite);
        String b = String(st.battery) + " %";
        D.drawString(b.c_str(), right, r.y);
        right -= D.textWidth(b.c_str()) + 16;
    }
    if (!roomy && st.online) {  // bandeau compact : volume en texte
        D.setTextColor(kBlack, kWhite);
        String v = "Vol " + String(st.player.volume);
        D.drawString(v.c_str(), right, r.y);
        right -= D.textWidth(v.c_str()) + 16;
    }

    int x = r.x;
    const int iconH = 16, iconY = r.y + 5;
    std::string state;
    if (!st.online) {
        state = "HORS LIGNE";
    } else if (st.player.playing) {
        state = "EN LECTURE";
        D.fillTriangle(x, iconY, x, iconY + iconH, x + 13, iconY + iconH / 2, kBlack);
        x += 22;
    } else if (!st.player.title.empty()) {
        state = "EN PAUSE";
        D.fillRect(x, iconY, 5, iconH, kBlack);
        D.fillRect(x + 9, iconY, 5, iconH, kBlack);
        x += 22;
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
    D.setTextDatum(textdatum_t::top_left);
    D.setTextColor(kGray, kWhite);
    D.drawString(fit(state, right - x).c_str(), x, r.y);

    // --- Titre, artiste, album (ou message)
    int y = r.y + 36;
    const int bottom = roomy ? r.y + r.h - 36 : r.y + r.h;  // réserve pour la barre de volume

    D.loadFont(roomy ? font_ui_40 : font_ui_32);
    D.setTextColor(kBlack, kWhite);
    std::string title = st.player.title.empty() ? (st.online ? "Rien en lecture" : "\xE2\x80\x94")
                                                : st.player.title;
    if (st.player.title.empty()) D.setTextColor(kGray, kWhite);
    D.drawString(fit(title, r.w).c_str(), r.x, y);
    y += D.fontHeight() + (roomy ? 6 : 2);

    D.loadFont(font_ui_24);
    const int lh = D.fontHeight() + 4;
    std::vector<std::pair<std::string, uint32_t>> lines;
    std::string album = st.player.album.empty() ? st.meta.album : st.player.album;
    if (st.message.length()) {
        // Message prioritaire : il prend la place de l'album (et de l'artiste si besoin)
        int room = std::max(1, (bottom - y) / lh);
        bool keepArtist = room >= 2 && !st.player.artist.empty();
        if (keepArtist) lines.push_back({st.player.artist, kBlack});
        for (auto& l : wr::wrapText(st.message.c_str(), r.w, room - (keepArtist ? 1 : 0), measure))
            lines.push_back({l, kBlack});
    } else {
        if (!st.player.artist.empty()) lines.push_back({fit(st.player.artist, r.w), kBlack});
        if (!album.empty()) lines.push_back({fit(album, r.w), kGray});
    }
    for (auto& l : lines) {
        if (y + lh > bottom + 4) break;
        D.setTextColor(l.second, kWhite);
        D.drawString(l.first.c_str(), r.x, y);
        y += lh;
    }

    // --- Barre de volume
    if (roomy) {
        const int cy = r.y + r.h - 14;
        const uint32_t c = st.online ? kBlack : kGray;
        speakerIcon(r.x, cy, c);
        D.setTextDatum(textdatum_t::middle_right);
        D.setTextColor(c, kWhite);
        String v = st.online ? String(st.player.volume) : String("\xE2\x80\x94");
        D.drawString(v.c_str(), r.x + r.w, cy);
        const int bx = r.x + 40, bw = r.w - 40 - 64, bh = 12;
        D.fillRoundRect(bx, cy - bh / 2, bw, bh, bh / 2, kPale);
        int fill = bw * std::max(0, std::min(100, st.player.volume)) / 100;
        if (st.online && fill > 0) D.fillRoundRect(bx, cy - bh / 2, std::max(bh, fill), bh, bh / 2, kBlack);
    }
}

// ---------------------------------------------------------------------- écrans

void Ui::drawHeader(const UiState& st) {
    beginDraw(epd_mode_t::epd_text);
    info(st);
    endDraw();
}

void Ui::drawArt(const ArtImage* img) {
    beginDraw(epd_mode_t::epd_quality);
    art(img);
    endDraw();
}

void Ui::drawMain(const UiState& st, const ArtImage* img, bool quality) {
    beginDraw(quality ? epd_mode_t::epd_quality : epd_mode_t::epd_text);
    D.fillScreen(kWhite);
    art(img);
    info(st);
    for (size_t i = 0; i < presets_.size(); ++i) button(layout_.presets[i], presets_[i].label, false);
    for (int i = 0; i < wr::kTransportCount; ++i)
        transportIcon(i, layout_.transport[i], false, st.player.playing);
    endDraw(!quality);
    if (quality) partialCount_ = 0;
}

void Ui::showPreset(int index, bool pressed) {
    if (index < 0 || index >= (int)presets_.size()) return;
    beginDraw(pressed ? epd_mode_t::epd_fastest : epd_mode_t::epd_text);
    button(layout_.presets[index], presets_[index].label, pressed);
    endDraw();
}

void Ui::showTransport(int which, bool pressed, bool playing) {
    if (which < 0 || which >= wr::kTransportCount) return;
    beginDraw(pressed ? epd_mode_t::epd_fastest : epd_mode_t::epd_text);
    transportIcon(which, layout_.transport[which], pressed, playing);
    endDraw();
}

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

void Ui::showInfo(const char* title, const String& body, const char* footer) {
    beginDraw(epd_mode_t::epd_quality);
    D.fillScreen(kWhite);
    const int m = 40, w = D.width() - 2 * m;
    D.loadFont(font_ui_40);
    D.setTextDatum(textdatum_t::top_left);
    D.setTextColor(kBlack, kWhite);
    D.drawString(title, m, m);
    D.loadFont(font_ui_24);
    int y = m + 76;
    auto lines = wr::wrapText(body.c_str(), w, 12, measure);
    for (auto& l : lines) {
        D.drawString(l.c_str(), m, y);
        y += D.fontHeight() + 8;
    }
    if (footer) {
        D.setTextColor(kGray, kWhite);
        D.setTextDatum(textdatum_t::bottom_left);
        D.drawString(footer, m, D.height() - m);
    }
    endDraw(false);
    partialCount_ = 0;
}

void Ui::showPortal(const String& apName, const String& apPass, const String& url, const String& reason) {
    beginDraw(epd_mode_t::epd_quality);
    D.fillScreen(kWhite);
    const int m = 32;
    const bool landscape = D.width() > D.height();
    const int qr = landscape ? D.height() - 2 * m - 40 : D.width() - 2 * m - 80;

    // QR code pour rejoindre le réseau de configuration avec un téléphone
    String wifiQr = "WIFI:T:WPA;S:" + apName + ";P:" + apPass + ";;";
    int qx = landscape ? D.width() - m - qr : (D.width() - qr) / 2;
    int qy = landscape ? m + 20 : D.height() - m - qr;
    D.qrcode(wifiQr.c_str(), qx, qy, qr);

    const int textW = landscape ? qx - 2 * m : D.width() - 2 * m;
    int y = m;
    D.setTextDatum(textdatum_t::top_left);
    D.setTextColor(kBlack, kWhite);
    D.loadFont(font_ui_40);
    D.drawString("Configuration", m, y);
    y += 60;

    // Paragraphe avec retrait (sous le numéro d'étape)
    const int indent = 46;
    auto para = [&](const String& s, const uint8_t* font, uint32_t color, int x) {
        D.loadFont(font);
        D.setTextColor(color, kWhite);
        const int lh = D.fontHeight() + 6;
        for (auto& l : wr::wrapText(s.c_str(), textW - (x - m), 3, measure)) {
            D.drawString(l.c_str(), x, y);
            y += lh;
        }
    };
    // Numéro d'étape dans une pastille noire
    auto step = [&](int n) {
        D.fillCircle(m + 15, y + 15, 15, kBlack);
        D.loadFont(font_ui_24);
        D.setTextDatum(textdatum_t::middle_center);
        D.setTextColor(kWhite, kBlack);
        D.drawString(String(n).c_str(), m + 15, y + 16);
        D.setTextDatum(textdatum_t::top_left);
    };

    if (reason.length()) para(reason, font_ui_24, kGray, m);
    y += 14;
    step(1);
    para("Scanne le QR code avec ton téléphone, ou rejoins le Wi-Fi :", font_ui_24, kBlack, m + indent);
    para(apName, font_ui_32, kBlack, m + indent);
    para("mot de passe : " + apPass, font_ui_32, kBlack, m + indent);
    y += 14;
    step(2);
    para("Si la page ne s'ouvre pas, va sur " + url, font_ui_24, kBlack, m + indent);
    y += 14;
    step(3);
    para("Renseigne ton Wi-Fi, l'IP du WiiM et tes presets.", font_ui_24, kBlack, m + indent);
    endDraw(false);
    partialCount_ = 0;
}
