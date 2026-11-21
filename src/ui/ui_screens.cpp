// Écrans plein page : Réglages, Journal, test tactile, choix du WiiM, mise à jour, information, configuration.
#include "ui/ui_internal.h"

// -------------------------------------------------------------- plein écran

// Gros bouton de menu : titre et sous-titre, sur carte
static void menuButton(M5Canvas& C, const wr::Rect& r, const char* title, const char* sub, uint32_t dot, int sc16) {
    C.fillRoundRect(r.x, r.y, r.w, r.h, sc16, kCard);
    C.fillCircle(r.x + sc16 * 2, r.y + r.h / 2, sc16 * 2 / 3, dot);
    // Titre et sous-titre centrés ensemble dans la rangée (le titre débordait au-dessus du bord, vu au simulateur)
    C.setTextDatum(textdatum_t::top_left);
    useFont(C, kSmall);
    const int subH = C.fontHeight();
    useFont(C, kMid);
    const int titleH = C.fontHeight(), gap = sc16 / 4;
    const int top = r.y + (r.h - titleH - gap - subH) / 2;
    C.setTextColor(kText);
    const int tx = r.x + sc16 * 4, tw = r.right() - sc16 - tx;  // textes coupés (…) à la largeur de la rangée
    auto measure = [&](const std::string& t) { return (int)C.textWidth(t.c_str()); };
    C.drawString(wr::wrapText(title, tw, 1, measure)[0].c_str(), tx, top);
    useFont(C, kSmall);
    C.setTextColor(kText2);
    C.drawString(wr::wrapText(sub, tw, 1, measure)[0].c_str(), tx, top + titleH + gap);
}

void Ui::renderMenu(const char* version) {
    beginScreen(View::Other);
    auto& C = canvas_;
    background();
    navRail(1);
    const wr::Rect& A = layout_.content;
    const int W = A.w, H = C.height();
    // Hauteur des rangées : celle des deux lignes de texte (marges comprises dans la police), au moins sc(58)
    useFont(C, kMid);
    const int titleH = C.fontHeight();
    useFont(C, kSmall);
    const int textH = titleH + sc(4) + C.fontHeight();
    const int bw = std::min(W - 2 * sc(40), sc(560)), bh = std::max(sc(58), textH + sc(2)), gap = sc(8);
    // Titre « Réglages », 5 rangées, Fermer, version : le tout tient dans la hauteur (rangées remontées si besoin)
    const int below = sc(14) + sc(44) + sc(40);
    const int y0 = std::min(A.y + sc(92), A.bottom() - below - 5 * bh - 4 * gap);
    const int x = A.x + (W - bw) / 2;
    C.setTextDatum(textdatum_t::bottom_center);
    useFont(C, kBig);
    C.setTextColor(kText);
    C.drawString(wr::L("Settings", "R\xC3\xA9glages"), A.x + W / 2, y0 - sc(20));
    wr::Rect menuRects[6];
    menuRects[MenuConfig] = wr::Rect{x, y0, bw, bh};
    menuRects[MenuUpdate] = wr::Rect{x, y0 + bh + gap, bw, bh};
    menuRects[MenuTouch] = wr::Rect{x, y0 + 2 * (bh + gap), bw, bh};
    menuRects[MenuLog] = wr::Rect{x, y0 + 3 * (bh + gap), bw, bh};
    menuButton(C, menuRects[MenuConfig], wr::L("Setup", "Configuration"),
               wr::L("Wi-Fi, WiiM, presets, voice", "Wi-Fi, WiiM, presets, voix"),
               accent_, sc(16));
    menuButton(C, menuRects[MenuUpdate], wr::L("Update", "Mise \xC3\xA0 jour"),
               wr::L("Install new firmware over Wi-Fi", "Installer un nouveau firmware par le Wi-Fi"), kText2,
               sc(16));
    menuButton(C, menuRects[MenuTouch], wr::L("Touch test", "Test tactile"),
               wr::L("See where the screen detects your finger", "Voir o\xC3\xB9 l'\xC3\xA9" "cran d\xC3\xA9tecte ton doigt"),
               kText2, sc(16));
    menuButton(C, menuRects[MenuLog], wr::L("Log", "Journal"),
               wr::L("Recent events, for troubleshooting", "Derniers \xC3\xA9v\xC3\xA9nements, pour le diagnostic"), kText2,
               sc(16));
    menuRects[MenuSleep] = wr::Rect{x, y0 + 4 * (bh + gap), bw, bh};
    menuButton(C, menuRects[MenuSleep], wr::L("Sleep", "Mise en veille"),
               wr::L("Power off; the power button turns it back on", "\xC3\x89teindre ; le bouton d'alimentation rallume"),
               kText2,
               sc(16));
    const int cw = sc(150), ch = sc(44);
    menuRects[MenuClose] = wr::Rect{A.x + (W - cw) / 2, menuRects[MenuSleep].bottom() + sc(14), cw, ch};
    const wr::Rect& c = menuRects[MenuClose];
    C.fillRoundRect(c.x, c.y, c.w, c.h, c.h / 2, kCard);
    useFont(C, kSmall);
    C.setTextDatum(textdatum_t::middle_center);
    C.setTextColor(kText);
    C.drawString(wr::L("Close", "Fermer"), c.x + c.w / 2, c.y + c.h / 2 + 1);
    C.setTextDatum(textdatum_t::bottom_right);
    C.setTextColor(kText2);
    C.drawString((String("Version ") + version).c_str(), C.width() - sc(20), H - sc(14));
    // Touchables (la barre est inscrite par navRail) : les six choix, dans l'ordre de MenuChoice
    for (int i = 0; i < 6; ++i)
        targets_.add(wr::Act::MenuChoice, i, menuRects[i], i == MenuConfig ? wr::kTargetDefault : 0);
    present();
}

// Menu d'une tuile : en-tête (image, nom, artiste), puis les choix, puis Fermer. Ouvert par un appui long sur la tuile
void Ui::renderTileMenu(int index, bool pinned, bool removable) {
    beginScreen(View::Other);
    auto& C = canvas_;
    background();
    navRail(0);
    const wr::Rect& A = layout_.content;
    const int W = A.w;
    if (index < 0 || index >= (int)presets_.size()) {
        present();
        return;
    }
    const wr::Preset& p = presets_[index];
    useFont(C, kMid);
    const int titleH = C.fontHeight();
    useFont(C, kSmall);
    const int textH = titleH + sc(4) + C.fontHeight();
    const int bw = std::min(W - 2 * sc(40), sc(560)), bh = std::max(sc(58), textH + sc(2)), gap = sc(8);
    const int side = thumbSide(), pad = sc(12);
    const int headH = side + 2 * pad;
    const int rows = removable ? 2 : 1;
    const int cw = sc(150), ch = sc(44);
    // En-tête, rangées et Fermer centrés ensemble dans la hauteur
    const int total = headH + sc(20) + rows * bh + (rows - 1) * gap + sc(14) + ch;
    const int x = A.x + (W - bw) / 2;
    const int y0 = A.y + std::max(sc(16), (A.h - total) / 2);
    // En-tête : image de la tuile et ses deux lignes
    C.fillRoundRect(x, y0, bw, headH, sc(16), kCard);
    tileArt(index, x + pad, y0 + pad, kCard);
    const int tx = x + pad + side + sc(16), tw = x + bw - pad - tx;
    auto measure = [&](const std::string& t) { return (int)C.textWidth(t.c_str()); };
    useFont(C, kMid);
    const int lh = C.fontHeight();
    useFont(C, kSmall);
    const int sh = p.sub.empty() ? 0 : C.fontHeight() + sc(4);
    const int ty = y0 + (headH - lh - sh) / 2;
    C.setTextDatum(textdatum_t::top_left);
    useFont(C, kMid);
    C.setTextColor(kText);
    C.drawString(wr::wrapText(p.label, tw, 1, measure)[0].c_str(), tx, ty);
    if (!p.sub.empty()) {
        useFont(C, kSmall);
        C.setTextColor(kText2);
        C.drawString(wr::wrapText(p.sub, tw, 1, measure)[0].c_str(), tx, ty + lh + sc(4));
    }
    wr::Rect rects[3];
    int y = y0 + headH + sc(20);
    rects[TilePin] = wr::Rect{x, y, bw, bh};
    menuButton(C, rects[TilePin], pinned ? wr::L("Unpin", "D\xC3\xA9s\xC3\xA9pingler") : wr::L("Pin to the front", "\xC3\x89pingler en t\xC3\xAAte"),
               pinned ? wr::L("Back to its usual place", "Retour \xC3\xA0 sa place habituelle")
                      : wr::L("Stays at the front", "Reste en t\xC3\xAAte"),
               accent_, sc(16));
    y += bh + gap;
    if (removable) {
        rects[TileRemove] = wr::Rect{x, y, bw, bh};
        menuButton(C, rects[TileRemove], wr::L("Remove from favorites", "Retirer des favoris"),
                   wr::L("The tile goes away", "La tuile dispara\xC3\xAEt"), kRed, sc(16));
        y += bh + gap;
    }
    rects[TileClose] = wr::Rect{A.x + (W - cw) / 2, y - gap + sc(14), cw, ch};
    const wr::Rect& c = rects[TileClose];
    C.fillRoundRect(c.x, c.y, c.w, c.h, c.h / 2, kCard);
    useFont(C, kSmall);
    C.setTextDatum(textdatum_t::middle_center);
    C.setTextColor(kText);
    C.drawString(wr::L("Close", "Fermer"), c.x + c.w / 2, c.y + c.h / 2 + 1);
    // Touchables (la barre est inscrite par navRail), dans l'ordre de TileChoice
    targets_.add(wr::Act::TileChoice, TilePin, rects[TilePin], wr::kTargetDefault);
    if (removable) targets_.add(wr::Act::TileChoice, TileRemove, rects[TileRemove]);
    targets_.add(wr::Act::TileChoice, TileClose, rects[TileClose]);
    present();
}

// Journal : les lignes les plus récentes en bas, sur deux colonnes si besoin
void Ui::renderLog(const std::vector<String>& lines, const char* version) {
    beginScreen(View::Other);
    auto& C = canvas_;
    C.fillScreen(kBg);
    navRail(1);
    const wr::Rect& A = layout_.content;
    const int W = C.width(), H = C.height(), m = sc(20), X = A.x + m, Y = A.y + m;
    useFont(C, kMid);
    C.setTextDatum(textdatum_t::top_left);
    C.setTextColor(kText);
    C.drawString(wr::L("Log", "Journal"), X, Y);
    const int bw = sc(150), bh = sc(44), qw = sc(170);
    useFont(C, kSmall);
    C.setTextColor(kText2);
    const String ver = String("Version ") + version;
    if (X + sc(150) + C.textWidth(ver.c_str()) < W - m - bw - sc(24) - qw)  // sinon : version dans Réglages
        C.drawString(ver.c_str(), X + sc(150), Y + sc(6));
    const wr::Rect logClose = wr::Rect{W - bw - m, Y, bw, bh};
    C.fillRoundRect(logClose.x, logClose.y, bw, bh, bh / 2, kCardHi);
    C.setTextDatum(textdatum_t::middle_center);
    C.setTextColor(kText);
    C.drawString(wr::L("Close", "Fermer"), logClose.x + bw / 2, logClose.y + bh / 2 + 1);
    // Diagnostic de la file de lecture du WiiM (service UPnP PlayQueue)
    const wr::Rect logQueue = wr::Rect{logClose.x - sc(12) - qw, Y, qw, bh};
    C.fillRoundRect(logQueue.x, logQueue.y, qw, bh, bh / 2, kCard);
    C.drawRoundRect(logQueue.x, logQueue.y, qw, bh, bh / 2, accent_);
    C.drawString(wr::L("WiiM queue", "File du WiiM"), logQueue.x + qw / 2, logQueue.y + bh / 2 + 1);
    targets_.add(wr::Act::LogClose, 0, logClose, wr::kTargetDefault);
    targets_.add(wr::Act::LogQueue, 0, logQueue);
    // Lignes : police fine, pleine largeur ; une ligne longue passe sur 3 lignes au plus.
    // On part de la plus récente (en bas) et on remonte tant qu'il y a de la place.
    useFont(C, kTiny);
    C.setTextDatum(textdatum_t::top_left);
    const int top = Y + bh + sc(14), lh = C.fontHeight() + sc(2);
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
    beginScreen(View::Other);
    auto& C = canvas_;
    C.fillScreen(kBg);
    const int W = C.width(), H = C.height();
    for (int gx = 0; gx <= W; gx += W / 8) C.drawFastVLine(std::min(gx, W - 1), 0, H, kCard);
    for (int gy = 0; gy <= H; gy += H / 6) C.drawFastHLine(0, std::min(gy, H - 1), W, kCard);
    C.drawRect(0, 0, W, H, kText2);
    useFont(C, kMid);
    C.setTextDatum(textdatum_t::top_left);
    C.setTextColor(kText);
    C.drawString(wr::L("Touch test", "Test tactile"), sc(24), sc(20));
    useFont(C, kSmall);
    C.setTextColor(kText2);
    C.drawString(wr::L("Touch the screen: a circle should appear under your finger.",
                       "Touche l'\xC3\xA9" "cran : un rond doit appara\xC3\xAEtre sous ton doigt."),
                 sc(24), sc(76));
    String info = String(wr::L("Screen ", "\xC3\x89" "cran ")) + String(W) + " x " + String(H) + wr::L("   Taps: ", "   Appuis : ") +
                  String(count);
    if (x >= 0) info += String(wr::L("   Last: x ", "   Dernier : x ")) + String(x) + ", y " + String(y);
    C.drawString(info.c_str(), sc(24), sc(110));
    if (x >= 0) {
        C.fillCircle(x, y, sc(34), accent_);
        C.drawFastHLine(0, y, W, accent_);
        C.drawFastVLine(x, 0, H, accent_);
        C.fillCircle(x, y, sc(6), kText);
    }
    const int bw = sc(150), bh = sc(48);
    const wr::Rect touchClose = wr::Rect{W - bw - sc(20), sc(20), bw, bh};
    C.fillRoundRect(touchClose.x, touchClose.y, bw, bh, bh / 2, kCardHi);
    C.setTextDatum(textdatum_t::middle_center);
    C.setTextColor(kText);
    C.drawString(wr::L("Close", "Fermer"), touchClose.x + bw / 2, touchClose.y + bh / 2 + 1);
    targets_.add(wr::Act::TouchClose, 0, touchClose, wr::kTargetDefault);
    present();
}

void Ui::renderPicker(const std::vector<std::pair<std::string, std::string>>& devices) {
    beginScreen(View::Other);
    auto& C = canvas_;
    auto measure = [&](const std::string& t) { return (int)C.textWidth(t.c_str()); };
    background();
    const int W = C.width(), H = C.height(), m = sc(40);
    C.setTextDatum(textdatum_t::top_left);
    useFont(C, kBig);
    C.setTextColor(kText);
    C.drawString(devices.empty() ? wr::L("No WiiM found", "Aucun WiiM trouv\xC3\xA9")
                 : devices.size() == 1 ? wr::L("Use this WiiM?", "Utiliser ce WiiM ?") : wr::L("Which WiiM?", "Quel WiiM ?"), m, m);
    int y = m + C.fontHeight() + sc(16);
    useFont(C, kSmall);
    C.setTextColor(kText2);
    if (devices.empty()) {
        for (auto& l : wr::wrapText(wr::L("Check that the WiiM is on and connected to the same Wi-Fi as the Tab5, then retry. "
                                          "You can also enter its IP address in the setup.",
                                          "V\xC3\xA9rifie que le WiiM est allum\xC3\xA9 et branch\xC3\xA9 sur le m\xC3\xAA" "me "
                                          "Wi-Fi que le Tab5, puis r\xC3\xA9" "essaie. Tu peux aussi saisir son adresse IP "
                                          "dans la configuration."),
                                    W - 2 * m, 4, measure)) {
            C.drawString(l.c_str(), m, y);
            y += C.fontHeight() + sc(6);
        }
    } else {
        C.drawString(devices.size() == 1
                         ? wr::L("One player is on your Wi-Fi: check its name, then tap it.",
                                 "Un lecteur est sur ton Wi-Fi : v\xC3\xA9rifie son nom, puis touche-le.")
                         : wr::L("Several players are on your Wi-Fi: tap the one to control.",
                                 "Plusieurs lecteurs sont sur ton Wi-Fi : touche celui \xC3\xA0 commander."),
                     m, y);
        y += C.fontHeight() + sc(20);
        const int cols = W > H ? 2 : 1, gap = sc(16);
        const int cw = (W - 2 * m - (cols - 1) * gap) / cols, ch = sc(84);
        for (size_t i = 0; i < devices.size() && i < 6; ++i) {
            wr::Rect r{m + (int)(i % cols) * (cw + gap), y + (int)(i / cols) * (ch + gap), cw, ch};
            targets_.add(wr::Act::PickDevice, (int)i, r, i == 0 ? wr::kTargetDefault : 0);
            C.fillRoundRect(r.x, r.y, r.w, r.h, sc(16), kCard);
            C.fillCircle(r.x + sc(34), r.y + r.h / 2, sc(14), accent_);
            useFont(C, kMid);
            C.setTextColor(kText);
            C.setTextDatum(textdatum_t::top_left);
            auto name = wr::wrapText(devices[i].first, r.w - sc(80), 1, measure);
            C.drawString(name.empty() ? "WiiM" : name[0].c_str(), r.x + sc(64), r.y + sc(10));
            useFont(C, kSmall);
            C.setTextColor(kText2);
            C.drawString(devices[i].second.c_str(), r.x + sc(64), r.y + r.h - sc(10) - C.fontHeight());
        }
    }
    // Réessayer / Configuration
    const int bw = sc(190), bh = sc(48), gap = sc(16);
    const wr::Rect pickRetry = wr::Rect{W / 2 - gap / 2 - bw, H - m - bh, bw, bh};
    const wr::Rect pickConfig = wr::Rect{W / 2 + gap / 2, H - m - bh, bw, bh};
    useFont(C, kSmall);
    C.setTextDatum(textdatum_t::middle_center);
    for (auto* b : {&pickRetry, &pickConfig}) {
        C.fillRoundRect(b->x, b->y, b->w, b->h, b->h / 2, b == &pickRetry && devices.empty() ? accent_ : kCard);
        C.setTextColor(b == &pickRetry && devices.empty() ? kBg : kText);
        C.drawString(b == &pickRetry ? wr::L("Retry", "R\xC3\xA9" "essayer") : wr::L("Setup", "Configuration"), b->x + b->w / 2, b->y + b->h / 2 + 1);
    }
    // Les lecteurs (inscrits plus haut), puis Réessayer et Configuration ; focus de départ : le premier lecteur,
    // sinon Réessayer
    targets_.add(wr::Act::PickRetry, 0, pickRetry, devices.empty() ? wr::kTargetDefault : 0);
    targets_.add(wr::Act::PickSetup, 0, pickConfig);
    present();
}

void Ui::showUpdate(const String& url, const String& code, const char* version, int percent, const String& status) {
    beginScreen(View::Other);
    auto& C = canvas_;
    background();
    const int W = C.width(), H = C.height(), m = sc(40);
    const bool landscape = W > H;
    // QR code de l'adresse : le téléphone ouvre la page sans rien taper (et sans « https:// » ajouté par le navigateur)
    const int pad = sc(14);
    const int qr = landscape ? std::min(H - 2 * m - sc(150), W * 3 / 10) : W / 2;
    const int qx = landscape ? W - m - pad - qr : (W - qr) / 2;
    const int qy = landscape ? m + sc(20) + pad : H - sc(24) - sc(44) - sc(40) - pad - qr;
    C.fillRoundRect(qx - pad, qy - pad, qr + 2 * pad, qr + 2 * pad, sc(16), kText);
    C.qrcode(url.c_str(), qx, qy, qr);
    C.setTextDatum(textdatum_t::top_left);
    useFont(C, kBig);
    C.setTextColor(kText);
    C.drawString(wr::L("Update", "Mise \xC3\xA0 jour"), m, m);
    int y = m + C.fontHeight() + sc(24);
    useFont(C, kSmall);
    C.setTextColor(kText2);
    C.drawString(wr::L("1. On the same Wi-Fi, scan the QR code or open:",
                       "1. Sur le m\xC3\xAAme Wi-Fi, scanne le QR code ou ouvre :"),
                 m, y);
    y += C.fontHeight() + sc(8);
    useFont(C, kBig);
    C.setTextColor(accent_);
    C.drawString(url.c_str(), m + sc(24), y);
    y += C.fontHeight() + sc(18);
    useFont(C, kSmall);
    C.setTextColor(kText2);
    C.drawString(wr::L("2. Enter this code, then choose the .bin file:", "2. Saisis ce code, puis choisis le fichier .bin :"), m,
                 y);
    y += C.fontHeight() + sc(8);
    // Code en gros, chiffres espacés
    useFont(C, kBig);
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
    useFont(C, kSmall);
    C.setTextDatum(textdatum_t::top_left);
    if (percent >= 0) {
        const int bw = landscape ? qx - pad - sc(30) - m : W - 2 * m, bh = sc(14);
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
    C.drawString((String(wr::L("Installed version: ", "Version install\xC3\xA9" "e : ")) + version).c_str(), m, H - sc(24));
    const int bw = sc(150), bh = sc(44);
    const wr::Rect cancelRect = wr::Rect{W - m - bw, H - sc(24) - bh, bw, bh};
    C.fillRoundRect(cancelRect.x, cancelRect.y, bw, bh, bh / 2, kCard);
    C.setTextDatum(textdatum_t::middle_center);
    C.setTextColor(kText);
    C.drawString(wr::L("Cancel", "Annuler"), cancelRect.x + bw / 2, cancelRect.y + bh / 2 + 1);
    targets_.add(wr::Act::UpdateCancel, 0, cancelRect, wr::kTargetDefault);
    present();
}


void Ui::showInfo(const char* title, const String& body, const char* footer) {
    beginScreen(View::Other);
    auto& C = canvas_;
    auto measure = [&](const std::string& s) { return (int)C.textWidth(s.c_str()); };
    accent_ = kAccent;
    background();
    const int m = sc(40), w = C.width() - 2 * m;
    useFont(C, kBig);
    C.setTextDatum(textdatum_t::top_left);
    C.setTextColor(kText);
    C.drawString(title, m, m);
    useFont(C, kSmall);
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
    beginScreen(View::Other);
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
    useFont(C, kBig);
    C.setTextColor(kText);
    C.drawString(wr::L("Setup", "Configuration"), m, y);
    y += C.fontHeight() + sc(12);

    // Paragraphe avec retrait (sous le numéro d'étape)
    const int indent = sc(46);
    auto para = [&](const String& s, const uint8_t* font, uint32_t color, int x) {
        useFont(C, font);
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
        useFont(C, kSmall);
        C.setTextDatum(textdatum_t::middle_center);
        C.setTextColor(kBg);
        C.drawString(String(n).c_str(), m + rad, y + rad + sc(3));
        C.setTextDatum(textdatum_t::top_left);
    };

    if (reason.length()) para(reason, kSmall, kText2, m);
    y += sc(14);
    step(1);
    para(wr::L("Scan the QR code with your phone, or join the Wi-Fi:",
               "Scanne le QR code avec ton téléphone, ou rejoins le Wi-Fi :"),
         kSmall, kText, m + indent);
    para(apName, kMid, kText, m + indent);
    para(String(wr::L("password: ", "mot de passe : ")) + apPass, kMid, kText, m + indent);
    y += sc(14);
    step(2);
    para(String(wr::L("If the page does not open, go to ", "Si la page ne s'ouvre pas, va sur ")) + url, kSmall, kText, m + indent);
    y += sc(14);
    step(3);
    para(wr::L("Choose your Wi-Fi and type its password: the Tab5 finds your WiiM by itself.",
               "Choisis ton Wi-Fi et tape son mot de passe : le Tab5 trouve ton WiiM tout seul."),
         kSmall, kText,
         m + indent);
    present();
}
