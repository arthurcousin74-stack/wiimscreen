// Recherche : clavier, champ de saisie, choix de la source, liste des résultats ; mises à jour partielles et
// zones tactiles.
#include "ui/ui_internal.h"

// Clavier de recherche : champ de texte, état, touches
void Ui::renderSearch(const std::string& query, const String& status, bool busy, int pressedKey, int source,
                      bool sources, int mic, float level) {
    beginScreen(View::Other);
    background();
    navRail(2);
    searchField(query, status, busy, source, sources, mic, level);
    // Touches : d'abord les lettres (une seule police), puis les autres
    auto& C = canvas_;
    const wr::Rect& A = layout_.content;
    const int m = sc(18);
    keys_ = wr::keyboardLayout(wr::Rect{A.x + m, searchArea_.bottom(), A.w - 2 * m, A.bottom() - m - searchArea_.bottom()});
    keysBusy_ = busy;
    useFont(C, kMid);
    for (size_t i = 0; i < keys_.size(); ++i)
        if (keys_[i].kind == wr::Key::Char) drawKey((int)i, (int)i == pressedKey);
    useFont(C, kSmall);
    for (size_t i = 0; i < keys_.size(); ++i)
        if (keys_[i].kind != wr::Key::Char) drawKey((int)i, (int)i == pressedKey);
    // Touchables (la barre est inscrite par navRail) : micro (sc(8) autour), sources, touches (sc(4) autour : les
    // espaces entre touches comptent pour la plus proche ; deux voisines agrandies se recouvrent d'un pixel au plus,
    // la première dans l'ordre de keys() l'emporte, comme avant). updateSearchField() ne change pas cette liste :
    // ses rectangles ne bougent qu'avec `sources`, que l'application ne change qu'avec un rendu complet.
    if (voiceOn_ && micRect_.w > 0) {
        const int pad = sc(8);
        targets_.add(wr::Act::Mic, 0, micRect_, wr::Rect{micRect_.x - pad, micRect_.y - pad, micRect_.w + 2 * pad, micRect_.h + 2 * pad});
    }
    for (int i = 0; i < 3; ++i)
        if (srcRects_[i].w > 0) targets_.add(wr::Act::Source, i, srcRects_[i]);
    const int pad = sc(4);
    for (size_t i = 0; i < keys_.size(); ++i) {
        const wr::Rect& r = keys_[i].r;
        // Focus de départ : la première lettre (deuxième rangée)
        targets_.add(wr::Act::Key, (int)i, r, wr::Rect{r.x - pad, r.y - pad, r.w + 2 * pad, r.h + 2 * pad},
                     i == 10 ? wr::kTargetDefault : 0);
    }
    present();
}

// Une touche (la police doit déjà être chargée : grande pour les lettres, petite pour les autres)
void Ui::drawKey(int i, bool down) {
    auto& C = canvas_;
    const wr::Key& k = keys_[i];
    const bool enter = k.kind == wr::Key::Enter;
    const uint32_t bg = down ? kCardHi : (enter ? (keysBusy_ ? kCardHi : accent_) : kCard);
    C.fillRoundRect(k.r.x, k.r.y, k.r.w, k.r.h, sc(10), bg);
    const int cx = k.r.x + k.r.w / 2, cy = k.r.y + k.r.h / 2;
    if (k.kind == wr::Key::Back) {  // flèche d'effacement
        const int s = std::min(k.r.h, k.r.w) / 4;
        C.fillTriangle(cx - 2 * s, cy, cx - s, cy - s, cx - s, cy + s, kText);
        C.fillRect(cx - s, cy - s, 3 * s, 2 * s, kText);
        C.drawLine(cx - s / 3, cy - s / 2, cx + s, cy + s / 2, bg);
        C.drawLine(cx - s / 3, cy + s / 2, cx + s, cy - s / 2, bg);
        return;
    }
    C.setTextDatum(textdatum_t::middle_center);
    C.setTextColor(enter && !keysBusy_ ? kBg : (k.kind == wr::Key::Space || k.kind == wr::Key::Clear ? kText2 : kText));
    C.drawString(enter && keysBusy_ ? "\xE2\x80\xA6" : k.label.c_str(), cx, cy + 1);
}

// Appui sur une touche : seule cette touche est redessinée et envoyée à l'écran (quelques ms)
void Ui::pressKey(int i, bool down) {
    if (i < 0 || i >= (int)keys_.size()) return;
    auto& C = canvas_;
    const wr::Rect& r = keys_[i].r;
    C.setClipRect(r.x, r.y, r.w, r.h);
    bg_.pushSprite(&C, 0, 0);
    useFont(C, keys_[i].kind == wr::Key::Char ? kMid : kSmall);
    drawKey(i, down);
    C.clearClipRect();
    presentRect(r);
}

// Champ, choix de la source et ligne d'état seuls : à chaque lettre tapée et pendant la dictée
void Ui::updateSearchField(const std::string& query, const String& status, bool busy, int source, bool sources,
                           int mic, float level) {
    auto& C = canvas_;
    C.setClipRect(searchArea_.x, searchArea_.y, searchArea_.w, searchArea_.h);
    bg_.pushSprite(&C, 0, 0);
    searchField(query, status, busy, source, sources, mic, level);
    C.clearClipRect();
    presentRect(searchArea_);
}

void Ui::searchField(const std::string& query, const String& status, bool busy, int source, bool sources, int mic,
                     float level) {
    auto& C = canvas_;
    const wr::Rect& A = layout_.content;
    const int m = sc(18), X = A.x + m, W = A.w - 2 * m;
    auto measure = [&](const std::string& s) { return (int)C.textWidth(s.c_str()); };
    // Champ de texte
    const int fh = sc(64), fy = A.y + m;
    C.fillRoundRect(X, fy, W, fh, fh / 2, kCard);
    C.drawRoundRect(X, fy, W, fh, fh / 2, accent_);
    searchIcon(X + fh / 2, fy + fh / 2, sc(12), kText2, kCard);
    // Micro (dictée) au bout du champ : touche pour écouter, touche encore (■) pour finir
    micRect_ = wr::Rect{};
    int micW = 0;
    if (voiceOn_) {
        const int d = fh - sc(10);
        micRect_ = wr::Rect{X + W - sc(5) - d, fy + sc(5), d, d};
        const int cx = micRect_.x + d / 2, cy = micRect_.y + d / 2;
        if (mic == 1) {  // écoute : carré « stop » rouge, anneau qui suit la voix
            const int ring = d / 2 + (int)(sc(10) * std::min(1.0f, level));
            C.fillCircle(cx, cy, ring, mix(kCard, kRed, 0.35f));
            C.fillCircle(cx, cy, d / 2, kRed);
            C.fillRoundRect(cx - d / 6, cy - d / 6, d / 3, d / 3, sc(3), kText);
        } else {
            C.fillCircle(cx, cy, d / 2, mic == 2 ? kCardHi : accent_);
            micIcon(cx, cy, d / 4, mic == 2 ? kText2 : kBg, mic == 2 ? kCardHi : accent_);
        }
        micW = d + sc(10);
    }
    useFont(C, kMid);
    C.setTextDatum(textdatum_t::middle_left);
    const int tx = X + fh, tw = W - fh - sc(24) - micW;
    if (mic == 1 || mic == 2) {
        C.setTextColor(mic == 1 ? kText : kText2);
        const char* say = mic == 1 ? wr::L("Listening\xE2\x80\xA6 tap the red button when you are done",
                                         "Je t'\xC3\xA9" "coute\xE2\x80\xA6 touche le bouton rouge quand tu as fini")
                                   : wr::L("Transcribing\xE2\x80\xA6", "Transcription\xE2\x80\xA6");
        useFont(C, kSmall);
        C.drawString(wr::wrapText(say, tw, 1, measure)[0].c_str(), tx, fy + fh / 2 + 1);
    } else if (query.empty()) {
        C.setTextColor(kText2);
        C.drawString(wr::wrapText(wr::L("Artist, track, album\xE2\x80\xA6", "Artiste, morceau, album\xE2\x80\xA6"), tw, 1, measure)[0].c_str(), tx, fy + fh / 2 + 1);
    } else {
        std::string shown = query;  // le début coupé si le texte déborde : on voit la fin, là où l'on tape
        while (shown.size() > 1 && measure(shown) > tw - sc(10)) shown = shown.substr(1);
        C.setTextColor(kText);
        C.drawString(shown.c_str(), tx, fy + fh / 2 + 1);
        if (!busy) C.fillRect(tx + measure(shown) + sc(3), fy + sc(14), sc(3), fh - sc(28), accent_);  // curseur
    }
    // Choix de la source (à droite), puis état (recherche en cours, aucun résultat, erreur)
    useFont(C, kSmall);
    int lineW = W;
    srcRects_[0] = srcRects_[1] = srcRects_[2] = wr::Rect{};
    {
        // Qobuz (morceaux) | Albums (Qobuz) | Deezer (si une file Deezer est connue), à droite de la ligne d'état
        std::vector<std::pair<int, const char*>> chips = {{0, "Qobuz"}};
        if (albumsOn_) chips.push_back({2, "Albums"});
        if (sources) chips.push_back({1, "Deezer"});
        const int n = (int)chips.size(), gap = sc(8), ch = sc(40), cy = fy + fh + sc(6);
        const int cw = std::min(sc(124), (W * 60 / 100 - (n - 1) * gap) / n);
        for (int k = 0; k < n; ++k) {
            wr::Rect& r = srcRects_[chips[k].first];
            r = wr::Rect{X + W - (n - k) * cw - (n - 1 - k) * gap, cy, cw, ch};
            const bool on = chips[k].first == source;
            C.fillRoundRect(r.x, r.y, r.w, r.h, ch / 2, on ? accent_ : kCard);
            C.setTextDatum(textdatum_t::middle_center);
            C.setTextColor(on ? kBg : kText2);
            C.drawString(chips[k].second, r.x + r.w / 2, r.y + r.h / 2 + 1);
        }
        lineW = srcRects_[0].x - X - sc(16);
    }
    C.setTextDatum(textdatum_t::top_left);
    C.setTextColor(busy ? accent_ : kText2);
    const String line = status.length()
                            ? status
                            : String(source == 1   ? wr::L("Deezer tracks \xC2\xB7 your WiiM plays the album",
                                                   "Morceaux Deezer \xC2\xB7 l'album est jou\xC3\xA9 par ton WiiM")
                                     : source == 2 ? wr::L("Qobuz albums \xC2\xB7 the heart makes it a tile",
                                                           "Albums Qobuz \xC2\xB7 le c\xC5\x93ur en fait une tuile")
                                                   : wr::L("Qobuz tracks, searched by your WiiM",
                                                           "Morceaux Qobuz, cherch\xC3\xA9s par ton WiiM"));
    C.drawString(wr::wrapText(line.c_str(), lineW, 1, measure)[0].c_str(), X + sc(8), fy + fh + sc(10));
    searchArea_ = wr::Rect{A.x, A.y, A.w, fy + fh + sc(10) + C.fontHeight() + sc(14) - A.y};
}

// Résultats : une ligne par morceau (titre, artiste · album), par pages
void Ui::renderResults(const std::string& query, const std::vector<wr::QueueTrack>& items, int page,
                       const String& status, int pressed, const std::vector<int>& hearts,
                       const std::vector<const ArtImage*>& covers, bool listActions) {
    beginScreen(View::Other);
    auto& C = canvas_;
    background();
    navRail(2);
    const wr::Rect& A = layout_.content;
    const int m = sc(18), X = A.x + m, W = A.w - 2 * m;
    auto measure = [&](const std::string& s) { return (int)C.textWidth(s.c_str()); };
    // En-tête : « texte » · nombre, bouton Modifier ; liste de favoris : Tout lire et Aléatoire à sa place (la
    // recherche reste dans la barre), à la largeur de leur texte (les polices ne suivent pas la taille de l'écran)
    const int bh = sc(52), bw = sc(170);
    const bool actions = listActions && !items.empty();
    wr::Rect resultsEdit, playAll, shuffle;
    int headW = W - bw - sc(20);
    useFont(C, kSmall);
    C.setTextDatum(textdatum_t::middle_center);
    if (!actions) {
        resultsEdit = wr::Rect{X + W - bw, A.y + m, bw, bh};
        C.fillRoundRect(resultsEdit.x, resultsEdit.y, bw, bh, bh / 2, kCard);
        searchIcon(resultsEdit.x + sc(30), resultsEdit.y + bh / 2, sc(9), kText, kCard);
        C.setTextColor(kText);
        C.drawString(wr::L("Edit", "Modifier"), resultsEdit.x + bw / 2 + sc(14), resultsEdit.y + bh / 2 + 1);
    } else {
        const char* playText = wr::L("Play all", "Tout lire");
        const char* mixText = wr::L("Shuffle", "Al\xC3\xA9" "atoire");
        const int pad = sc(22), ts = sc(9), icon = 2 * ts + sc(10), g = sc(10);
        const int mw = (int)C.textWidth(mixText) + 2 * pad, pw = (int)C.textWidth(playText) + 2 * pad + icon;
        shuffle = wr::Rect{X + W - mw, A.y + m, mw, bh};
        playAll = wr::Rect{shuffle.x - g - pw, A.y + m, pw, bh};
        headW = playAll.x - X - sc(20);
        C.fillRoundRect(playAll.x, playAll.y, pw, bh, bh / 2, accent_);
        const int tx = playAll.x + pad + ts * 2 / 3, ty = playAll.y + bh / 2;
        C.fillTriangle(tx - ts * 2 / 3, ty - ts, tx - ts * 2 / 3, ty + ts, tx + ts, ty, kBg);
        C.setTextColor(kBg);
        C.drawString(playText, playAll.x + icon + (pw - icon) / 2, ty + 1);
        C.fillRoundRect(shuffle.x, shuffle.y, mw, bh, bh / 2, kCard);
        C.setTextColor(kText);
        C.drawString(mixText, shuffle.x + mw / 2, shuffle.y + bh / 2 + 1);
    }
    useFont(C, kMid);
    C.setTextDatum(textdatum_t::middle_left);
    C.setTextColor(kText);
    // Texte cherché entre guillemets ; une liste de favoris a un nom, pas une recherche : sans guillemets
    const std::string head = listActions ? query : std::string(wr::L("\xE2\x80\x9C", "\xC2\xAB ")) + query + wr::L("\xE2\x80\x9D", " \xC2\xBB");
    C.drawString(wr::wrapText(head, std::max(sc(40), headW), 1, measure)[0].c_str(), X, A.y + m + bh / 2);
    useFont(C, kSmall);
    C.setTextDatum(textdatum_t::top_left);
    C.setTextColor(kText2);
    String sub = status.length() ? status
                                 : String((int)items.size()) +
                                       (items.size() > 1 ? wr::L(" tracks", " morceaux") : wr::L(" track", " morceau")) +
                                       wr::L(" \xC2\xB7 tap one to play it", " \xC2\xB7 touche celui \xC3\xA0 \xC3\xA9" "couter");
    C.drawString(wr::wrapText(sub.c_str(), W, 1, measure)[0].c_str(), X, A.y + m + bh + sc(8));
    // Lignes
    const int top = A.y + m + bh + sc(8) + C.fontHeight() + sc(14), navH = sc(56);
    const int rowH = sc(72), gap = sc(8);
    resultsPerPage_ = std::max(1, (A.bottom() - m - navH - sc(10) - top + gap) / (rowH + gap));
    const int pages = std::max(1, ((int)items.size() + resultsPerPage_ - 1) / resultsPerPage_);
    page = std::max(0, std::min(pages - 1, page));
    resultsFirst_ = page * resultsPerPage_;
    coverRects_.clear();
    for (int k = 0; k < resultsPerPage_ && resultsFirst_ + k < (int)items.size(); ++k) {
        const int i = resultsFirst_ + k;
        const wr::Rect r{X, top + k * (rowH + gap), W, rowH};
        const uint32_t bg = i == pressed ? kCardHi : kCard;
        C.fillRoundRect(r.x, r.y, r.w, r.h, sc(12), bg);
        // Pochette (ou numéro), titre, artiste · album
        int tx = r.x + sc(60), tw = r.w - sc(80);
        if (!items[i].art.empty()) {
            const int d = r.h - sc(12);
            const wr::Rect cr{r.x + sc(6), r.y + sc(6), d, d};
            coverRects_.push_back(cr);
            drawResultCover(cr, i < (int)covers.size() ? covers[i] : nullptr);
            tx = cr.right() + sc(14);
            tw = r.right() - sc(20) - tx;
        } else {
            coverRects_.push_back(wr::Rect{});
            useFont(C, kSmall);
            C.setTextDatum(textdatum_t::middle_center);
            C.setTextColor(kText2);
            C.drawString(String(i + 1).c_str(), r.x + sc(30), r.y + r.h / 2 + 1);
        }
        if (i < (int)hearts.size()) {  // cœur à droite : vide (ajouter en tuile) ou plein (retirer)
            const int d = r.h - sc(16);
            const wr::Rect h{r.x + r.w - sc(8) - d, r.y + sc(8), d, d};
            // Cœur (sc(6) autour) avant sa ligne, qui le contient : il l'emporte, comme dans main.cpp
            const int pad = sc(6);
            targets_.add(wr::Act::ResultHeart, i, h, wr::Rect{h.x - pad, h.y - pad, h.w + 2 * pad, h.h + 2 * pad});
            const bool on = hearts[i] == 1;
            C.fillCircle(h.x + d / 2, h.y + d / 2, d / 2, on ? accent_ : kCardHi);
            heartIcon(h.x + d / 2, h.y + d / 2, d / 4, on ? kBg : kText);
            if (!on) heartIcon(h.x + d / 2, h.y + d / 2 - 1, d / 4 - sc(4), kCardHi);
            tw -= d + sc(12);
        }
        targets_.add(wr::Act::ResultRow, i, r, k == 0 ? wr::kTargetDefault : 0);
        const wr::QueueTrack& t = items[i];
        C.setTextDatum(textdatum_t::top_left);
        useFont(C, kSmall);
        C.setTextColor(kText);
        C.drawString(wr::wrapText(t.title.empty() ? "?" : t.title, tw, 1, measure)[0].c_str(), tx, r.y + sc(8));
        std::string under = t.artist;
        if (!t.album.empty()) under += (under.empty() ? "" : " \xC2\xB7 ") + t.album;
        useFont(C, kTiny);
        C.setTextColor(kText2);
        if (!under.empty()) C.drawString(wr::wrapText(under, tw, 1, measure)[0].c_str(), tx, r.y + r.h / 2 + sc(4));
    }
    // Pages
    wr::Rect resultsPrev, resultsNext;
    if (pages > 1) {
        const int ny = A.bottom() - m - navH, nw = sc(200);
        useFont(C, kSmall);
        C.setTextDatum(textdatum_t::middle_center);
        if (page > 0) {
            resultsPrev = wr::Rect{X, ny, nw, navH};
            C.fillRoundRect(X, ny, nw, navH, navH / 2, kCard);
            C.setTextColor(kText);
            C.drawString(wr::L("Previous", "Pr\xC3\xA9" "c\xC3\xA9" "dents"), X + nw / 2, ny + navH / 2 + 1);
        }
        if (page < pages - 1) {
            resultsNext = wr::Rect{X + W - nw, ny, nw, navH};
            C.fillRoundRect(X + W - nw, ny, nw, navH, navH / 2, kCard);
            C.setTextColor(kText);
            C.drawString(wr::L("Next", "Suivants"), X + W - nw / 2, ny + navH / 2 + 1);
        }
        C.setTextColor(kText2);
        C.drawString((String(page + 1) + " / " + String(pages)).c_str(), X + W / 2, ny + navH / 2 + 1);
    }
    // Pages, puis Modifier (la barre est inscrite par navRail, les lignes et leurs cœurs plus haut). Sans ligne,
    // le focus de départ est Modifier.
    if (resultsPrev.w > 0) targets_.add(wr::Act::ResultsPage, -1, resultsPrev);
    if (resultsNext.w > 0) targets_.add(wr::Act::ResultsPage, 1, resultsNext);
    if (playAll.w > 0) {
        targets_.add(wr::Act::ResultsPlayAll, 0, playAll);
        targets_.add(wr::Act::ResultsPlayAll, 1, shuffle);
    }
    if (resultsEdit.w > 0) targets_.add(wr::Act::ResultsEdit, 0, resultsEdit, items.empty() ? wr::kTargetDefault : 0);
    present();
}

// Pochette d'une ligne : décodée une fois (cache par adresse), case sombre tant qu'elle n'est pas arrivée
void Ui::drawResultCover(const wr::Rect& r, const ArtImage* img) {
    auto& C = canvas_;
    if (img && img->ok()) {
        auto it = coverThumbs_.find(img->url);
        if (it == coverThumbs_.end()) {
            if (coverThumbs_.size() >= 48) coverThumbs_.clear();  // ~48 × 60×60×2 o au plus, en PSRAM
            auto t = std::unique_ptr<M5Canvas>(new M5Canvas());
            t->setPsram(true);
            t->setColorDepth(16);
            if (t->createSprite(r.w, r.h) && decode(*t, img, kCardHi)) it = coverThumbs_.emplace(img->url, std::move(t)).first;
            else it = coverThumbs_.emplace(img->url, nullptr).first;  // illisible : pas de nouvel essai
        }
        if (it->second && it->second->width() == r.w) {
            roundedPush(*it->second, r.x, r.y, sc(6), false, kCard);
            return;
        }
    }
    C.fillRoundRect(r.x, r.y, r.w, r.h, sc(6), kCardHi);
}

void Ui::updateResultCover(int index, const ArtImage* img) {
    const int k = index - resultsFirst_;
    if (k < 0 || k >= (int)coverRects_.size() || coverRects_[k].w <= 0) return;
    const wr::Rect& r = coverRects_[k];
    drawResultCover(r, img);
    presentRect(r);
}

// Micro de la recherche (le seul). Garde son propre calcul : micRect_ reste valable après un passage du clavier
// à la liste des résultats, et main.cpp l'interroge aussi sur cette liste pendant une dictée (une liste qui
// arriverait pendant l'écoute) ; l'élément Mic n'est inscrit que sur le clavier.
bool Ui::hitMic(int x, int y) const {
    if (!voiceOn_ || micRect_.w == 0) return false;
    const int pad = sc(8);
    return wr::Rect{micRect_.x - pad, micRect_.y - pad, micRect_.w + 2 * pad, micRect_.h + 2 * pad}.contains(x, y);
}
