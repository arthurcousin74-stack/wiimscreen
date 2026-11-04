#include "ui.h"

#include <algorithm>

#include "fonts/font_ui_24.h"
#include "fonts/font_ui_32.h"

static auto& D = M5.Display;

// Couleurs en RGB888 (uint32_t) : M5GFX interpréterait un uint16_t comme du RGB565
static constexpr uint32_t kWhite = 0xFFFFFFu;
static constexpr uint32_t kBlack = 0x000000u;
static uint32_t gray() { return D.color888(0x70, 0x70, 0x70); }
static uint32_t light() { return D.color888(0xD8, 0xD8, 0xD8); }

static int measure(const std::string& s) { return D.textWidth(s.c_str()); }

void Ui::begin(const Settings& s) {
    D.setRotation(s.rotation);
    D.setAutoDisplay(false);
    presets_ = s.presets;
    layout_ = wr::computeLayout(D.width(), D.height(), s.columns, (int)presets_.size());
}

void Ui::push(bool fast) {
    D.waitDisplay();  // attendre la fin du rafraîchissement précédent avant de changer de mode
    D.setEpdMode(fast ? epd_mode_t::epd_fastest : epd_mode_t::epd_text);
    D.display();
    ++partialCount_;
}

// --------------------------------------------------------------------- éléments

void Ui::button(const wr::Rect& r, const std::string& label, bool inverted) {
    uint32_t bg = inverted ? kBlack : kWhite, fg = inverted ? kWhite : kBlack;
    D.fillRoundRect(r.x, r.y, r.w, r.h, 14, bg);
    for (int i = 0; i < 3; ++i) D.drawRoundRect(r.x + i, r.y + i, r.w - 2 * i, r.h - 2 * i, 14 - i, kBlack);

    D.loadFont(font_ui_32);
    D.setTextColor(fg, bg);
    D.setTextDatum(textdatum_t::middle_center);
    auto lines = wr::wrapText(label, r.w - 28, 2, measure);
    int lineH = D.fontHeight() + 4;
    int y = r.y + r.h / 2 - (int)(lines.size() - 1) * lineH / 2;
    for (auto& l : lines) {
        D.drawString(l.c_str(), r.x + r.w / 2, y);
        y += lineH;
    }
}

void Ui::transportIcon(int which, const wr::Rect& r, bool inverted, bool playing) {
    uint32_t bg = inverted ? kBlack : kWhite, fg = inverted ? kWhite : kBlack;
    D.fillRoundRect(r.x, r.y, r.w, r.h, 14, bg);
    for (int i = 0; i < 3; ++i) D.drawRoundRect(r.x + i, r.y + i, r.w - 2 * i, r.h - 2 * i, 14 - i, kBlack);

    const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    const int s = std::min(r.w, r.h) / 4;  // demi-taille de l'icône
    const int t = std::max(4, s / 4);      // épaisseur des traits
    switch (which) {
        case wr::kVolDown:
            D.fillRect(cx - s, cy - t / 2, 2 * s, t, fg);
            break;
        case wr::kVolUp:
            D.fillRect(cx - s, cy - t / 2, 2 * s, t, fg);
            D.fillRect(cx - t / 2, cy - s, t, 2 * s, fg);
            break;
        case wr::kPrev:
            D.fillTriangle(cx, cy - s, cx, cy + s, cx - s, cy, fg);
            D.fillTriangle(cx + s, cy - s, cx + s, cy + s, cx, cy, fg);
            break;
        case wr::kNext:
            D.fillTriangle(cx, cy - s, cx, cy + s, cx + s, cy, fg);
            D.fillTriangle(cx - s, cy - s, cx - s, cy + s, cx, cy, fg);
            break;
        case wr::kPlayPause:
            if (playing) {  // pause
                D.fillRect(cx - s * 2 / 3, cy - s, s / 2, 2 * s, fg);
                D.fillRect(cx + s / 6, cy - s, s / 2, 2 * s, fg);
            } else {        // lecture
                D.fillTriangle(cx - s * 2 / 3, cy - s, cx - s * 2 / 3, cy + s, cx + s, cy, fg);
            }
            break;
    }
}

// ---------------------------------------------------------------------- écrans

void Ui::drawHeader(const UiState& st) {
    const wr::Rect& h = layout_.header;
    D.startWrite();
    D.fillRect(h.x, h.y, h.w, h.h, kWhite);

    // À droite : volume / état réseau, batterie
    D.loadFont(font_ui_32);
    D.setTextDatum(textdatum_t::top_right);
    D.setTextColor(st.online ? kBlack : gray(), kWhite);
    // Pas de ternaire : avec le String Arduino, "a" + String(b) est un StringSumHelper (ambigu)
    String right = "Hors ligne";
    if (st.online) right = "Vol " + String(st.player.volume);
    D.drawString(right.c_str(), h.x + h.w, h.y + 4);
    int rightW = D.textWidth(right.c_str());
    if (st.battery >= 0) {
        D.loadFont(font_ui_24);
        D.setTextColor(gray(), kWhite);
        String b = String(st.battery) + " %";
        D.drawString(b.c_str(), h.x + h.w, h.y + 50);
        rightW = std::max(rightW, (int)D.textWidth(b.c_str()));
    }
    const int textW = h.w - rightW - 24;

    // À gauche : titre puis artiste (ou message)
    D.loadFont(font_ui_32);
    D.setTextDatum(textdatum_t::top_left);
    D.setTextColor(kBlack, kWhite);
    std::string title = st.player.title.empty() ? (st.online ? "—" : "") : st.player.title;
    auto t = wr::wrapText(title, textW, 1, measure);
    if (!t.empty()) D.drawString(t[0].c_str(), h.x, h.y + 4);

    D.loadFont(font_ui_24);
    bool msg = st.message.length() > 0;
    D.setTextColor(msg ? kBlack : gray(), kWhite);
    std::string second = msg ? std::string(st.message.c_str()) : st.player.artist;
    auto a = wr::wrapText(second, textW, 1, measure);
    if (!a.empty()) D.drawString(a[0].c_str(), h.x, h.y + 50);

    // Filet de séparation
    D.fillRect(h.x, h.y + h.h - 2, h.w, 2, light());
    D.endWrite();
    push(false);
}

void Ui::drawMain(const UiState& st, bool quality) {
    D.waitDisplay();
    D.startWrite();
    D.fillScreen(kWhite);
    for (size_t i = 0; i < presets_.size(); ++i) button(layout_.presets[i], presets_[i].label, false);
    for (int i = 0; i < wr::kTransportCount; ++i)
        transportIcon(i, layout_.transport[i], false, st.player.playing);
    D.endWrite();
    if (quality) {
        D.setEpdMode(epd_mode_t::epd_quality);
        D.display();
        partialCount_ = 0;
    } else {
        push(false);
    }
    drawHeader(st);
}

void Ui::showPreset(int index, bool pressed) {
    if (index < 0 || index >= (int)presets_.size()) return;
    D.startWrite();
    button(layout_.presets[index], presets_[index].label, pressed);
    D.endWrite();
    push(pressed);
}

void Ui::showTransport(int which, bool pressed, bool playing) {
    if (which < 0 || which >= wr::kTransportCount) return;
    D.startWrite();
    transportIcon(which, layout_.transport[which], pressed, playing);
    D.endWrite();
    push(pressed);
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
    D.waitDisplay();
    D.startWrite();
    D.fillScreen(kWhite);
    const int m = 40, w = D.width() - 2 * m;
    D.loadFont(font_ui_32);
    D.setTextDatum(textdatum_t::top_left);
    D.setTextColor(kBlack, kWhite);
    D.drawString(title, m, m);
    D.loadFont(font_ui_24);
    int y = m + 70;
    auto lines = wr::wrapText(body.c_str(), w, 12, measure);
    for (auto& l : lines) {
        D.drawString(l.c_str(), m, y);
        y += D.fontHeight() + 8;
    }
    if (footer) {
        D.setTextColor(gray(), kWhite);
        D.setTextDatum(textdatum_t::bottom_left);
        D.drawString(footer, m, D.height() - m);
    }
    D.endWrite();
    D.setEpdMode(epd_mode_t::epd_quality);
    D.display();
    partialCount_ = 0;
}

void Ui::showPortal(const String& apName, const String& apPass, const String& url, const String& reason) {
    D.waitDisplay();
    D.startWrite();
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
    D.loadFont(font_ui_32);
    D.drawString("Configuration", m, y);
    y += 60;

    D.loadFont(font_ui_24);
    const int lh = D.fontHeight() + 10;
    auto line = [&](const String& s, uint32_t color) {
        D.setTextColor(color, kWhite);
        for (auto& l : wr::wrapText(s.c_str(), textW, 3, measure)) {
            D.drawString(l.c_str(), m, y);
            y += lh;
        }
    };
    if (reason.length()) line(reason, gray());
    y += 8;
    line("1. Avec ton téléphone, scanne le QR code", kBlack);
    line("   ou rejoins le Wi-Fi :", kBlack);
    line("   " + apName, kBlack);
    line("   mot de passe : " + apPass, kBlack);
    y += 8;
    line("2. Ouvre " + url + " si la page", kBlack);
    line("   ne s'affiche pas toute seule.", kBlack);
    y += 8;
    line("3. Renseigne ton Wi-Fi, l'IP du WiiM et tes presets.", kBlack);
    D.endWrite();
    D.setEpdMode(epd_mode_t::epd_quality);
    D.display();
    partialCount_ = 0;
}
