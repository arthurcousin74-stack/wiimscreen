// Vue commandes (pochette, textes, progression, lecture, volume, tuiles des presets) et vue ambiance :
// dessin, mises à jour partielles et zones tactiles.
#include "ui/ui_internal.h"

#include <cctype>

static bool endsWithEllipsis(const std::vector<std::string>& lines) {
    static const std::string ell = "\xE2\x80\xA6";
    return !lines.empty() && lines.back().size() >= ell.size() &&
           lines.back().compare(lines.back().size() - ell.size(), ell.size(), ell) == 0;
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
// Puce : petite pilule (point coloré facultatif + texte)
void Ui::chip(int& x, int y, const std::string& text, uint32_t dot, uint32_t textColor) {
    auto& C = canvas_;
    useFont(C, kSmall);
    const int h = sc(28), padX = sc(12), dotD = dot ? sc(10) : 0, dotGap = dot ? sc(8) : 0;
    const int w = padX * 2 + dotD + dotGap + (int)C.textWidth(text.c_str());
    if (x + w > (chipRight_ > 0 ? chipRight_ : layout_.info.right())) return;  // pas sur la batterie
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
        useFont(C, kSmall);
        const int bw = sc(34), bh = sc(17), by = r.y + (sc(28) - bh) / 2;
        int bx = right - bw - sc(4);
        C.drawRoundRect(bx, by, bw, bh, sc(4), kText2);
        C.drawRoundRect(bx + 1, by + 1, bw - 2, bh - 2, sc(4) - 1, kText2);
        C.fillRect(bx + bw, by + bh / 2 - sc(4), sc(4), sc(8), kText2);
        int inner = (bw - sc(8)) * std::max(0, std::min(100, st.battery)) / 100;
        if (inner > 0) C.fillRect(bx + sc(4), by + sc(4), inner, bh - sc(8), st.battery <= 15 ? kRed : kText);
        C.setTextDatum(textdatum_t::middle_right);
        C.setTextColor(kText2);
        const String pct = String(st.battery) + " %";
        C.drawString(pct.c_str(), bx - sc(10), r.y + sc(14) + 1);
        right = bx - sc(10) - (int)C.textWidth(pct.c_str()) - sc(12);
    }
    chipRight_ = right;  // les puces s'arrêtent avant la batterie (vu dans le simulateur : « Chargement… » la couvrait)
    int x = r.x;
    const int chipY = r.y;
    if (!st.online) {
        chip(x, chipY, wr::L("Offline", "Hors ligne"), kRed, kText);
    } else {
        if (st.player.loading) chip(x, chipY, wr::L("Loading\xE2\x80\xA6", "Chargement\xE2\x80\xA6"), accent_, kText);
        else if (st.player.playing && st.player.durationMs == 0 && !st.player.title.empty())
            chip(x, chipY, wr::L("LIVE", "EN DIRECT"), accent_, kText);
        std::string src = wr::sourceName(st.player.mode);
        if (!src.empty()) chip(x, chipY, src, 0, kText2);
        std::string q = wr::formatQuality(st.meta.sampleRate, st.meta.bitDepth);
        if (!q.empty()) chip(x, chipY, q, 0, kText2);
    }
    chipRight_ = 0;

    // --- Titre (deux lignes si la place le permet), artiste, album
    int y = chipY + sc(28) + sc(8);
    const bool empty = st.player.title.empty();
    std::string album = st.player.album.empty() ? st.meta.album : st.player.album;
    C.setTextDatum(textdatum_t::top_left);
    useFont(C, kBig);
    const int titleH = C.fontHeight() + sc(2);
    useFont(C, kMid);
    const int artistH = C.fontHeight() + sc(4);
    useFont(C, kSmall);
    const int albumH = C.fontHeight();
    const int needOne = titleH + artistH + albumH;
    const int titleLines = (r.bottom() - y >= needOne + titleH) ? 2 : 1;

    useFont(C, kBig);
    if (empty) {
        C.setTextColor(kText2);
        C.drawString(st.online ? wr::L("Nothing playing", "Rien en lecture") : wr::L("WiiM unreachable", "WiiM injoignable"), r.x, y);
        y += titleH;
        useFont(C, kSmall);
        if (presets_.empty()) {
            C.drawString(fit(wr::L("Start the music from your phone.", "Lance la musique depuis ton t\xC3\xA9l\xC3\xA9phone."), r.w).c_str(), r.x, y + sc(6));
        } else {
            C.drawString(fit(wr::L("Choose a preset below.", "Choisis un preset ci-dessous."), r.w).c_str(), r.x, y + sc(6));
        }
        return;
    }
    C.setTextColor(kText);
    for (auto& l : wr::wrapText(st.player.title, r.w, titleLines, measure)) {
        C.drawString(l.c_str(), r.x, y);
        y += titleH;
    }
    if (!st.player.artist.empty() && y + artistH <= r.bottom()) {
        useFont(C, kMid);
        C.setTextColor(accent_);
        C.drawString(fit(st.player.artist, r.w).c_str(), r.x, y + sc(2));
        y += artistH;
    }
    if (!album.empty() && y + albumH <= r.bottom()) {
        useFont(C, kSmall);
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
        chip(x, cy - sc(14), wr::L("LIVE", "EN DIRECT"), accent_, kText2);
        return;
    }
    useFont(C, kSmall);
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
    useFont(C, kSmall);
    C.setTextDatum(textdatum_t::middle_right);
    C.setTextColor(st.online ? kText : kText2);
    String t = !st.online ? String("\xE2\x80\x94") : st.player.muted ? String(wr::L("Muted", "Muet")) : String(vol);
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
        useFont(C, kMid);
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
    useFont(C, kSmall);
    lines = wr::wrapText(label, r.w - sc(16), 1, measure);
    if (endsWithEllipsis(lines)) {
        useFont(C, kTiny);
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
    useFont(C, kSmall);
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
    beginScreen(View::Controls);
    progDuration_ = st.player.durationMs;
    background();
    roundedPush(art_, layout_.art.x, layout_.art.y, sc(16), true, 0);
    info(st);
    progressRow(st, layout_.progress, &progHit_);
    transport(st, pressedTransport);
    navRail(0);
    favButton();
    volumePill(st, pressedTransport);
    for (size_t slot = 0; slot < layout_.presets.size(); ++slot) {
        int i = page_ * layout_.perPage + (int)slot;
        if (i < (int)presets_.size()) tile((int)slot, i, i == pressedPreset);
    }
    if (presets_.empty()) emptyStrip();
    pageDots();
    toast(st.message);
    controlsTargets();
    present();
}

static wr::Rect padded(const wr::Rect& r, int pad) { return wr::Rect{r.x - pad, r.y - pad, r.w + 2 * pad, r.h + 2 * pad}; }

// Éléments touchables de la vue commandes (le premier qui contient le point l'emporte, dans l'ordre des anciens
// tests de main.cpp) : barres (volume, haut-parleur, progression), en-tête, points de page, tuiles puis bande, ♥, lecture.
// La barre de navigation est déjà inscrite (navRail) : elle ne touche rien d'autre. Chevauchements réels :
// progression (agrandie de sc(10)) sur les textes et sur le bouton lecture agrandi, points de page (sc(6) de plus)
// sur le bas des tuiles ; les tuiles sont dans la bande.
void Ui::controlsTargets() {
    using wr::Act;
    const int pad = sc(10);
    targets_.add(Act::Volume, 0, layout_.volBar, wr::kTargetNoFocus);
    targets_.add(Act::Mute, 0, layout_.mute);
    if (progDuration_ > 0 && progHit_.w > 0) targets_.add(Act::Progress, 0, layout_.progress, progHit_, wr::kTargetNoFocus);
    targets_.add(Act::Header, 0, layout_.art, wr::kTargetNoFocus);
    targets_.add(Act::Header, 1, layout_.info, wr::kTargetNoFocus);
    if (pageCount() >= 2) {  // moitié gauche : page précédente, moitié droite : suivante
        const wr::Rect& d = layout_.dots;
        const int y = d.y - sc(6), h = d.h + sc(12), half = d.w / 2;
        targets_.add(Act::Dots, -1, wr::Rect{d.x, y, half, h}, wr::kTargetNoFocus);
        targets_.add(Act::Dots, 1, wr::Rect{d.x + half, y, d.w - half, h}, wr::kTargetNoFocus);
    }
    for (size_t slot = 0; slot < layout_.presets.size(); ++slot) {
        int i = page_ * layout_.perPage + (int)slot;
        if (i < (int)presets_.size()) targets_.add(Act::Preset, i, layout_.presets[slot]);
    }
    if (!presets_.empty()) targets_.add(Act::Strip, 0, layout_.strip, wr::kTargetNoFocus);
    if (favState_ > 0) targets_.add(Act::Fav, 0, layout_.fav, padded(layout_.fav, pad));
    for (int i = 0; i < wr::kTransportCount; ++i) {
        const wr::Rect& r = layout_.transport[i];
        // Cercles un peu agrandis (le doigt touche souvent à côté), sauf vol−/vol+ : collés au curseur
        const bool vol = i == wr::kVolDown || i == wr::kVolUp;
        targets_.add(Act::Transport, i, r, vol ? r : padded(r, pad), i == wr::kPlayPause ? wr::kTargetDefault : 0);
    }
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
    useFont(C, kBig);
    C.setTextColor(kTrack);
    C.drawString("+", ix + d / 2, iy + d / 2);
    const int tx = ix + d + sc(22), tw = r.right() - sc(20) - tx;
    auto measure = [&](const std::string& s) { return (int)C.textWidth(s.c_str()); };
    C.setTextDatum(textdatum_t::top_left);
    useFont(C, kMid);
    C.setTextColor(kText);
    int y = r.y + sc(22);
    C.drawString(wr::wrapText(wr::L("No favorites yet", "Aucun favori pour l'instant"), tw, 1, measure)[0].c_str(), tx, y);
    y += C.fontHeight() + sc(8);
    useFont(C, kSmall);
    C.setTextColor(kText2);
    for (auto& l : wr::wrapText(wr::L("While an album or a playlist plays, tap the favorite button (left of the controls) "
                                      "to add it here.",
                                      "Pendant qu'un album ou une playlist joue, touche le bouton favori (\xC3\xA0 gauche des "
                                      "commandes) pour l'ajouter ici."),
                                tw, 3, measure)) {
        if (y + C.fontHeight() > r.bottom() - sc(10)) break;
        C.drawString(l.c_str(), tx, y);
        y += C.fontHeight() + sc(4);
    }
}

void Ui::favButton() {
    if (favState_ <= 0) return;
    const wr::Rect& f = layout_.fav;
    const bool on = favState_ == 2;
    canvas_.fillCircle(f.x + f.w / 2, f.y + f.h / 2, f.w / 2, on ? accent_ : kCard);
    heartIcon(f.x + f.w / 2, f.y + f.h / 2, f.w / 4, on ? kBg : (favState_ == 3 ? kTrack : kText));
    if (!on) heartIcon(f.x + f.w / 2, f.y + f.h / 2 - 1, f.w / 4 - sc(4), favState_ == 3 ? kText2 : kCard);
}

// Rangée des boutons de lecture seule (anneaux qui tournent pendant une attente) : bien moins coûteux qu'un
// écran complet 20 fois par seconde, et la boucle reste libre pour les appuis
bool Ui::updateTransport(const UiState& st, int pressed) {
    if (view_ != View::Controls) return false;
    const wr::Rect& play = layout_.transport[wr::kPlayPause];
    const int pad = sc(10);
    const int x0 = std::min(layout_.fav.x, layout_.transport[wr::kPrev].x) - pad;
    const int x1 = layout_.transport[wr::kNext].right() + pad;
    const wr::Rect r{x0, play.y - pad, x1 - x0, play.h + 2 * pad};
    auto& C = canvas_;
    C.setClipRect(r.x, r.y, r.w, r.h);
    bg_.pushSprite(&C, 0, 0);
    transport(st, pressed);
    favButton();
    C.clearClipRect();
    presentRect(r);
    return true;
}

// Pilule du volume seule : un appui sur −/+, le curseur glissé ou le son coupé ne renvoient pas l'écran entier
bool Ui::updateVolume(const UiState& st, int pressed) {
    if (view_ != View::Controls) return false;
    const wr::Rect& r = layout_.volume;
    if (r.w <= 0 || r.h <= 0) return false;
    auto& C = canvas_;
    C.setClipRect(r.x, r.y, r.w, r.h);
    bg_.pushSprite(&C, 0, 0);
    volumePill(st, pressed);
    C.clearClipRect();
    presentRect(r);
    return true;
}

// Une tuile seule : enfoncée sous le doigt, puis relâchée (le reste de l'écran ne change pas)
bool Ui::updateTile(int index, bool pressed) {
    if (view_ != View::Controls || layout_.perPage <= 0 || index < 0 || index >= (int)presets_.size()) return false;
    const int slot = index - page_ * layout_.perPage;
    if (slot < 0 || slot >= (int)layout_.presets.size()) return false;
    const wr::Rect& r = layout_.presets[slot];
    auto& C = canvas_;
    C.setClipRect(r.x, r.y, r.w, r.h);
    bg_.pushSprite(&C, 0, 0);
    tile(slot, index, pressed);
    C.clearClipRect();
    presentRect(r);
    return true;
}

void Ui::renderAmbient(const UiState& st) {
    beginScreen(View::Ambient);
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

    std::string title = st.player.title.empty() ? (st.online ? wr::L("Nothing playing", "Rien en lecture") : wr::L("Offline", "Hors ligne"))
                                                : st.player.title;
    useFont(C, kBig);
    auto titleLines = wr::wrapText(title, tw, 3, measure);
    const int titleH = C.fontHeight() + sc(6);
    useFont(C, kMid);
    const int artistH = C.fontHeight() + sc(6);
    useFont(C, kSmall);
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
    useFont(C, kBig);
    C.setTextColor(st.player.title.empty() ? kText2 : kText);
    for (auto& l : titleLines) {
        C.drawString(l.c_str(), tx, y);
        y += titleH;
    }
    if (!st.player.artist.empty()) {
        useFont(C, kMid);
        C.setTextColor(accent_);
        C.drawString(wr::wrapText(st.player.artist, tw, 1, measure)[0].c_str(), tx, y);
        y += artistH;
    }
    useFont(C, kSmall);
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
    // Touchables : le bouton Accueil, puis tout l'écran (un appui n'importe où ramène les commandes)
    targets_.add(wr::Act::Home, 0, homeRect_, wr::kTargetDefault);
    targets_.add(wr::Act::AmbientExit, 0, wr::Rect{0, 0, W, H}, wr::kTargetNoFocus);
    if (visEnabled_) drawBars();
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
    if (view_ != View::Ambient || visRect_.w == 0) return;
    const wr::Rect& r = visRect_;
    canvas_.setClipRect(r.x, r.y, r.w, r.h);
    bg_.pushSprite(&canvas_, 0, 0);
    drawBars();
    canvas_.clearClipRect();
    presentRect(r);
}

// -------------------------------------------------------------------- zones
// Position visée par un appui sur le curseur de volume ou la barre de progression (éléments Volume / Progress de
// targets()). La zone de la progression suit aussi updateProgress() (progHit_).

bool Ui::hitVolume(int x, int y, int& volume) const {
    const wr::Rect& b = layout_.volBar;
    if (!b.contains(x, y)) return false;
    volume = std::max(0, std::min(100, (x - b.x) * 100 / std::max(1, b.w)));
    return true;
}

int Ui::volumeAt(int x) const {
    const wr::Rect& b = layout_.volBar;
    return std::max(0, std::min(100, (x - b.x) * 100 / std::max(1, b.w)));
}

bool Ui::hitProgress(int x, int y, uint32_t& posMs) const {
    if (!progHit_.contains(x, y) || progDuration_ == 0) return false;
    int dx = std::max(0, std::min(progHit_.w, x - progHit_.x));
    posMs = (uint32_t)((uint64_t)progDuration_ * dx / std::max(1, progHit_.w));
    return true;
}
