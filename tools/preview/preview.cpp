// Aperçu de l'interface sur PC : dessine les écrans avec le vrai src/ui/ui.cpp et
// les enregistre en PNG.
//
//   pio run -e preview
//   .pio/build/preview/program dossier_images/ dossier_sortie/
//     dossier_images : cover.jpg, et preset1..6 en .jpg ou .png (facultatifs) ; tools/preview/images/
//   python tools/preview/topng.py dossier_sortie/*.ppm
//
// Variables d'environnement : PREVIEW_LANG=fr (écrans en français), PREVIEW_SIZE=800x480 (taille de l'écran en
// paysage, 1280x720 par défaut ; le portrait prend la même taille tournée).
// Les captures ne dépendent pas de l'heure (millis() figé ci-dessous) : tools/preview/check_golden.py compare
// leurs empreintes à tools/preview/golden.sha256.
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <vector>

#include "M5Unified.h"
#include "net/art.h"
#include "ui/ui.h"
#include "i18n.h"

PreviewM5 M5;

// Horloge figée : l'anneau d'attente (Ui::transport, millis()) est dessiné au même angle à chaque exécution
uint32_t millis() { return 0; }

ArtImage& ArtImage::operator=(ArtImage&& o) noexcept {
    if (this != &o) {
        reset();
        url = std::move(o.url);
        data = o.data;
        len = o.len;
        o.data = nullptr;
        o.len = 0;
    }
    return *this;
}
void ArtImage::reset() {
    free(data);
    data = nullptr;
    len = 0;
}

static bool loadFile(const char* path, ArtImage& out) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.data = static_cast<uint8_t*>(malloc(n));
    out.len = fread(out.data, 1, n, f);
    fclose(f);
    return out.len == (size_t)n;
}

// Image brute PPM (convertie en PNG par tools/preview/topng.py)
static void save(const std::string& dir, const char* name) {
    auto& D = M5.Display;
    std::string path = dir + "/" + name;
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", (int)D.width(), (int)D.height());
    std::vector<uint8_t> row(D.width() * 3);
    for (int y = 0; y < D.height(); ++y) {
        for (int x = 0; x < D.width(); ++x) {
            auto c = D.readPixelRGB(x, y);
            row[x * 3] = c.R8();
            row[x * 3 + 1] = c.G8();
            row[x * 3 + 2] = c.B8();
        }
        fwrite(row.data(), 1, row.size(), f);
    }
    fclose(f);
    printf("%s\n", path.c_str());
}

static bool loadAny(const std::string& base, ArtImage& out) {
    for (const char* ext : {".jpg", ".png"})
        if (loadFile((base + ext).c_str(), out)) return true;
    return false;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s dossier_images dossier_sortie\n", argv[0]);
        return 1;
    }
    const std::string in = argv[1], out = argv[2];
    // Langue de l'interface : PREVIEW_LANG=fr pour les écrans en français (anglais par défaut)
    if (const char* lg = getenv("PREVIEW_LANG")) wr::setLang(wr::langFromCode(lg));
    // Taille de l'écran en paysage (PREVIEW_SIZE=LxH) ; le portrait est la même taille tournée
    int W = 1280, H = 720;
    if (const char* sz = getenv("PREVIEW_SIZE")) {
        if (sscanf(sz, "%dx%d", &W, &H) != 2 || W < H || H < 240 || W > 4096) {
            fprintf(stderr, "PREVIEW_SIZE invalide : %s (attendu : 1280x720, 800x480...)\n", sz);
            return 1;
        }
    }
    ArtImage art;
    if (!loadFile((in + "/cover.jpg").c_str(), art)) {
        fprintf(stderr, "image illisible : %s/cover.jpg\n", in.c_str());
        return 1;
    }
    std::vector<ArtImage> thumbs(6);
    for (int i = 0; i < 6; ++i) loadAny(in + "/preset" + std::to_string(i + 1), thumbs[i]);

    auto& D = M5.Display;
    D.setColorDepth(24);
    if (!D.createSprite(W, H)) {
        fprintf(stderr, "canevas impossible à créer\n");
        return 1;
    }

    UiState st;
    st.online = true;
    st.battery = 85;
    st.player.ok = true;
    st.player.playing = true;
    st.player.volume = 42;
    st.player.mode = 31;
    st.player.positionMs = 83000;
    st.player.durationMs = 562000;
    st.player.title = "So What";
    st.player.artist = "Miles Davis";
    st.player.album = "Kind of Blue (Legacy Edition)";
    st.meta.sampleRate = 44100;
    st.meta.bitDepth = 16;

    // Presets repris du WiiM, avec leurs images
    std::vector<wr::Preset> six;
    const char* names[] = {"FIP", "Radio Swiss Jazz", "France Inter", "Couleur 3", "Playlist du dimanche", "Nova"};
    for (int i = 0; i < 6; ++i) six.push_back(wr::Preset{i + 1, names[i], "x"});
    Settings s;
    Ui ui;
    ui.begin(s);
    ui.setPresets(six);
    for (int i = 0; i < 6; ++i) ui.setPresetArt(i, &thumbs[i]);
    ui.setArt(&art);
    ui.setActivePreset(1);
    ui.setVoiceEnabled(true);
    ui.setFavorite(1);  // ♥ vide : ajouter l'album en cours
    ui.render(st);
    save(out, "1-lecture.ppm");
    {
        // Presets du WiiM suivis de favoris de la télécommande (petit cœur, titre court et artiste, comme
        // refreshTiles()), album en cours déjà favori, puis la tuile de la liste « Favoris Qobuz »
        std::vector<wr::Preset> mix(six.begin(), six.begin() + 2);
        wr::Preset f1{101, wr::shortTitle("Kind of Blue (Legacy Edition)"), "x"};
        f1.favId = "a1";
        f1.sub = "Miles Davis";
        wr::Preset f2{102, wr::shortTitle("Babylon (Music from the Motion Picture)"), ""};
        f2.favId = "a2";
        f2.sub = "Justin Hurwitz";
        wr::Preset f3{99, wr::L("Qobuz favorites", "Favoris Qobuz"), ""};
        f3.favId = "qobuz-favoris";
        f3.list = true;
        mix.push_back(f1);
        mix.push_back(f2);
        mix.push_back(f3);
        ui.setPresets(mix);
        for (int i = 0; i < 2; ++i) ui.setPresetArt(i, &thumbs[i]);
        ui.setPresetArt(2, &thumbs[3]);
        ui.setActivePreset(2);
        ui.setFavorite(2);
        ui.render(st);
        save(out, "23-favoris.ppm");
        // Menu d'une tuile (appui long) : favori de la télécommande (Retirer proposé), puis preset épinglé
        ui.renderTileMenu(2, false, true);
        save(out, "49-menu-tuile.ppm");
        ui.renderTileMenu(0, true, false);
        save(out, "50-menu-tuile-epinglee.ppm");
        // Bibliothèque : toutes les tuiles, filtres des sortes présentes (presets, favoris, Qobuz)
        ui.renderLibrary({0, 1, 2, 3, 4}, {-1, 0, 1, 2, 3}, -1, 0);  // les quatre sortes : 800x480 aussi
        save(out, "51-bibliotheque.ppm");
        ui.renderLibrary({2, 3}, {-1, 0, 1, 2}, 1, 0, 2);  // filtre Favoris, tuile enfoncée
        save(out, "52-bibliotheque-favoris.ppm");
        UiState wait = st;
        wait.pending = wr::kNext;  // ⏭ envoyé, le WiiM n'a pas encore changé de morceau
        ui.render(wait);
        save(out, "28-attente.ppm");
        ui.render(st);
        ui.updateTransport(wait);  // même attente, rangée seule redessinée
        save(out, "30-attente-partielle.ppm");
        ui.setFavorite(1);
        ui.setPresets(six);
        for (int i = 0; i < 6; ++i) ui.setPresetArt(i, &thumbs[i]);
        ui.setActivePreset(1);
    }

    ui.render(st, 3);
    save(out, "2-appui-preset.ppm");

    // Vue ambiance (après 30 s sans appui), avec le visualiseur
    ui.setVisualizer(true);
    {
        std::vector<float> lv(24), pk(24);
        for (int i = 0; i < 24; ++i) {
            float t = i / 23.0f;
            lv[i] = 0.15f + 0.75f * std::exp(-8 * (t - 0.25f) * (t - 0.25f)) + 0.25f * std::exp(-30 * (t - 0.7f) * (t - 0.7f));
            pk[i] = std::min(1.0f, lv[i] + 0.08f);
        }
        ui.showLevels(lv, pk);
    }
    ui.renderAmbient(st);
    save(out, "3-ambiance.ppm");

    // Radio en chargement, sans pochette, son coupé, notification
    UiState radio = st;
    radio.player.mode = 10;
    radio.player.loading = true;
    radio.player.durationMs = 0;
    radio.player.muted = true;
    radio.player.title = "Le Temps de l'amour";
    radio.player.artist = "Françoise Hardy";
    radio.player.album = "";
    radio.meta = wr::TrackMeta();
    radio.message = wr::L("WiiM certificate saved", "Certificat du WiiM enregistré");
    radio.battery = -1;
    ui.setArt(nullptr);
    ui.setActivePreset(0);
    ui.render(radio);
    save(out, "4-radio-chargement.ppm");

    // 12 presets saisis à la main (sans images : monogrammes), page 2
    Ui ui12;
    std::string err;
    Settings s12;
    wr::parsePresets("1=FIP\n2=FIP Jazz\n3=FIP Groove\n4=Radio Swiss Jazz\n5=Radio Swiss Classic\n"
                     "6=France Inter\n7=France Culture\n8=Couleur 3\n9=Nova\n10=TSF Jazz\n"
                     "11=Playlist du dimanche\n12=Découvertes",
                     s12.presets, err);
    ui12.begin(s12);
    ui12.setArt(&art);
    ui12.setPage(1);
    UiState paused = st;
    paused.player.playing = false;
    ui12.render(paused);
    save(out, "5-douze-presets-page2.ppm");

    // Rien en lecture
    UiState idle;
    idle.online = true;
    idle.battery = 60;
    ui12.setArt(nullptr);
    ui12.setPage(0);
    ui12.render(idle);
    save(out, "6-rien-en-lecture.ppm");

    // Aucun preset sur le WiiM (cas fréquent au début)
    {
        Ui& u0 = *new Ui();
        Settings s0;
        u0.begin(s0);
        UiState none;
        none.online = true;
        none.player.volume = 49;
        u0.render(none);
        save(out, "17-aucun-preset.ppm");
        // Même écran pendant la lecture (ce que l'on voit sans preset sur le WiiM)
        u0.setArt(&art);
        u0.setFavorite(1);
        u0.render(st);
        save(out, "19-lecture-sans-preset.ppm");
        u0.renderAmbient(st);
        save(out, "20-ambiance-sans-visualiseur.ppm");
    }

    // Hors ligne
    UiState off = st;
    off.online = false;
    off.message = wr::L("WiiM unreachable: check that it is on and its IP", "WiiM injoignable : vérifie qu'il est allumé et son IP");
    ui.setArt(&art);
    ui.render(off);
    save(out, "7-hors-ligne.ppm");

    // Portrait (rotation 0) : même interface, pochette en haut, presets sur deux rangées
    {
        D.deleteSprite();
        D.createSprite(H, W);
        Ui& up = *new Ui();  // jamais détruite (voir la sortie par _exit)
        Settings sp;
        up.begin(sp);
        up.setPresets(six);
        for (int i = 0; i < 6; ++i) up.setPresetArt(i, &thumbs[i]);
        up.setArt(&art);
        up.setActivePreset(1);
        up.render(st);
        save(out, "9-portrait.ppm");
        std::vector<String> jl = {
            "  812.4 s  Appui File du WiiM",
            "  812.4 s  File du WiiM : recherche du service UPnP\xE2\x80\xA6",
            "  812.6 s  File : http://192.168.1.50:49152, 6 services",
            "  812.6 s    AVTransport:1, ConnectionManager:1, RenderingControl:1, PlayQueue:1, QPlay:1",
            "  812.8 s  File : actions PlayQueue",
            "  812.8 s    BrowseQueue CreateQueue DeleteQueue PlayQueueWithIndex AppendTracksInQueue",
            "  813.1 s  File : 2 file(s) sur le WiiM",
            "  813.1 s    Kind of Blue | USBDiskQueue",
            "  813.4 s  File \xC2\xAB Kind of Blue \xC2\xBB : source Qobuz, 5 piste(s), qualit\xC3\xA9 6",
            "  813.4 s    SearchUrl https://www.qobuz.com/api.json/0.2/album/get?album_id=",
            "  813.4 s      0886443927087&extra=tracks",
            "  813.4 s    compte ar***@gm***",
            "  813.4 s    - So What",
            "  813.5 s  File du WiiM : termin\xC3\xA9 (XML complet sur le port s\xC3\xA9rie)"};
        up.renderLog(jl, "57-219c0bc");
        save(out, "22-journal-portrait.ppm");
        {
            std::vector<wr::QueueTrack> found = {
            {"Respect", "Aretha Franklin", "I Never Loved a Man the Way I Love You", ""},
            {"Think (with The Royal Philharmonic Orchestra)", "Aretha Franklin", "A Brand New Me", ""},
            {"Climbing Higher Mountains (Live at New Temple Missionary Baptist Church, Los Angeles, CA, 01/13/72)",
             "Aretha Franklin", "Amazing Grace: The Complete Recordings", ""},
            {"(You Make Me Feel Like) A Natural Woman", "Aretha Franklin", "Lady Soul", ""},
            {"Chain of Fools", "Aretha Franklin", "Lady Soul", ""},
            {"I Say a Little Prayer", "Aretha Franklin", "Aretha Now", ""},
            {"Day Dreaming", "Aretha Franklin", "Young, Gifted and Black", ""}};
            up.renderSearch("", "", false);
            save(out, "26-recherche-portrait.ppm");
            up.renderResults("aretha franklin", found, 0, "");
            save(out, "27-resultats-portrait.ppm");
            up.renderMenu("57-219c0bc");
            save(out, "37-menu-portrait.ppm");
            up.renderSearch("miles", "", false, -1, 2, true);  // Qobuz | Albums | Deezer
            save(out, "33-albums-portrait.ppm");
            up.renderResults("miles", found, 0, "", -1, {1, 0, 0, 0, 0, 0, 0});
            save(out, "34-albums-resultats-portrait.ppm");
            // Bibliothèque en portrait : les cinq filtres tiennent sur la ligne, le dernier (Deezer) choisi
            up.renderLibrary({0, 1, 2, 3, 4, 5}, {-1, 0, 1, 2, 3}, 3, 0);
            save(out, "53-bibliotheque-portrait.ppm");
        }
        D.deleteSprite();
        D.createSprite(W, H);
    }

    ui.renderPicker({{"Salon", "192.168.1.50"}, {"Cuisine", "192.168.1.61"}, {"Bureau", "192.168.1.73"}});
    save(out, "15-choix-wiim.ppm");
    ui.renderPicker({});
    save(out, "16-aucun-wiim.ppm");
    ui.renderPicker({{"Salon", "192.168.1.50"}});
    save(out, "17-un-wiim.ppm");

    ui.renderMenu("57-219c0bc");
    save(out, "13-menu.ppm");
    {
        std::vector<wr::QueueTrack> found = {
            {"Respect", "Aretha Franklin", "I Never Loved a Man the Way I Love You", ""},
            {"Think (with The Royal Philharmonic Orchestra)", "Aretha Franklin", "A Brand New Me", ""},
            {"Climbing Higher Mountains (Live at New Temple Missionary Baptist Church, Los Angeles, CA, 01/13/72)",
             "Aretha Franklin", "Amazing Grace: The Complete Recordings", ""},
            {"(You Make Me Feel Like) A Natural Woman", "Aretha Franklin", "Lady Soul", ""},
            {"Chain of Fools", "Aretha Franklin", "Lady Soul", ""},
            {"I Say a Little Prayer", "Aretha Franklin", "Aretha Now", ""},
            {"Day Dreaming", "Aretha Franklin", "Young, Gifted and Black", ""}};
        ui.renderSearch("aretha frank", "", false, 20, 1, true);
        save(out, "24-recherche.ppm");
        ui.renderSearch("", "", false, -1, 0, true, 1, 0.6f);  // dictée en cours
        save(out, "29-dictee.ppm");
        ui.renderResults("aretha franklin", found, 0, "");
        save(out, "25-resultats.ppm");
        ui.renderResults(wr::L("Qobuz favorites", "Favoris Qobuz"), {}, 0, wr::L("Loading your Qobuz favorites\xE2\x80\xA6", "Chargement de tes favoris Qobuz\xE2\x80\xA6"), -1, {}, {}, true);
        save(out, "31-favoris-qobuz-chargement.ppm");
        ui.renderResults(wr::L("Qobuz favorites", "Favoris Qobuz"), found, 0, "", -1, {}, {}, true);  // Tout lire, Aléatoire
        save(out, "32-favoris-qobuz.ppm");
        ui.renderSearch("miles", "", false, -1, 2, false);  // Qobuz | Albums (pas de Deezer)
        save(out, "35-albums.ppm");
        const std::vector<wr::QueueTrack> albums = {
            {"Kind of Blue", "Miles Davis", wr::L("5 tracks", "5 pistes"), ""},
            {"Bitches Brew", "Miles Davis", wr::L("6 tracks", "6 pistes"), ""},
            {"Sketches of Spain", "Miles Davis", wr::L("5 tracks", "5 pistes"), ""},
            {"'Round About Midnight", "Miles Davis Quintet", wr::L("6 tracks", "6 pistes"), ""},
            {"In a Silent Way", "Miles Davis", wr::L("2 tracks", "2 pistes"), ""},
            {"Ascenseur pour l'\xC3\xA9" "chafaud", "Miles Davis", wr::L("26 tracks", "26 pistes"), ""},
            {"Milestones", "Miles Davis", wr::L("6 tracks", "6 pistes"), ""}};
        ui.renderResults("miles", albums, 0, wr::L("7 albums \xC2\xB7 tap to play, the heart makes it a tile", "7 albums \xC2\xB7 touche pour \xC3\xA9" "couter, le c\xC5\x93ur en fait une tuile"),
                         -1, {1, 0, 1, 0, 0, 0, 0});
        save(out, "36-albums-resultats.ppm");
    }
    {
            std::vector<String> jl = {
            "  812.4 s  Appui File du WiiM",
            "  812.4 s  File du WiiM : recherche du service UPnP\xE2\x80\xA6",
            "  812.6 s  File : http://192.168.1.50:49152, 6 services",
            "  812.6 s    AVTransport:1, ConnectionManager:1, RenderingControl:1, PlayQueue:1, QPlay:1",
            "  812.8 s  File : actions PlayQueue",
            "  812.8 s    BrowseQueue CreateQueue DeleteQueue PlayQueueWithIndex AppendTracksInQueue",
            "  813.1 s  File : 2 file(s) sur le WiiM",
            "  813.1 s    Kind of Blue | USBDiskQueue",
            "  813.4 s  File \xC2\xAB Kind of Blue \xC2\xBB : source Qobuz, 5 piste(s), qualit\xC3\xA9 6",
            "  813.4 s    SearchUrl https://www.qobuz.com/api.json/0.2/album/get?album_id=",
            "  813.4 s      0886443927087&extra=tracks",
            "  813.4 s    compte ar***@gm***",
            "  813.4 s    - So What",
            "  813.5 s  File du WiiM : termin\xC3\xA9 (XML complet sur le port s\xC3\xA9rie)"};
        ui.renderLog(jl, "57-219c0bc");
        save(out, "21-journal.ppm");
    }
    ui.renderTouchTest(820, 470, 7);
    save(out, "18-test-tactile.ppm");
    ui.showUpdate("http://192.168.1.42", "481203", "57-219c0bc", 63, wr::L("Receiving the firmware\xE2\x80\xA6", "R\xC3\xA9" "ception du firmware\xE2\x80\xA6"));
    save(out, "14-mise-a-jour.ppm");

    ui.showPortal("WiiM-Remote-3F2A", "k7Qp2xRm9w", "http://192.168.4.1", wr::L("Setup requested. Touch the Tab5 screen to cancel.", "Reconfiguration demand\xC3\xA9" "e. Touche l'\xC3\xA9" "cran du Tab5 pour annuler."));
    save(out, "8-configuration.ppm");
    {
        // Focus de la télécommande (anneau) : sur lecture, une tuile, un bouton de la barre, un choix du menu
        static wr::FocusSpot spot;
        ui.setFocusSource([](wr::FocusSpot& s) {
            s = spot;
            return true;
        });
        ui.setPresets(six);
        for (int i = 0; i < 6; ++i) ui.setPresetArt(i, &thumbs[i]);
        ui.setActivePreset(1);
        ui.setFavorite(1);
        spot = wr::FocusSpot{wr::Act::Transport, wr::kPlayPause};
        ui.render(st);
        save(out, "43-focus-lecture.ppm");
        spot = wr::FocusSpot{wr::Act::Preset, 2};
        ui.render(st);
        save(out, "44-focus-tuile.ppm");
        spot = wr::FocusSpot{wr::Act::Search, 0};
        ui.render(st);
        save(out, "45-focus-barre.ppm");
        spot = wr::FocusSpot{wr::Act::MenuChoice, 3};
        ui.renderMenu("57-219c0bc");
        save(out, "46-focus-menu.ppm");
        // Recherche au clavier ou au D-pad : anneau sur une touche, puis sur une ligne des résultats
        spot = wr::FocusSpot{wr::Act::Key, 24};
        ui.renderSearch("daft pu", "", false, -1, 0, true);
        save(out, "47-focus-touche.ppm");
        spot = wr::FocusSpot{wr::Act::ResultRow, 1};
        ui.renderResults("daft punk", {{"One More Time", "Daft Punk", "Discovery", ""},
                                       {"Aerodynamic", "Daft Punk", "Discovery", ""},
                                       {"Digital Love", "Daft Punk", "Discovery", ""},
                                       {"Get Lucky", "Daft Punk", "Random Access Memories", ""}},
                         0, "");
        save(out, "48-focus-resultat.ppm");
        ui.setFocusSource(nullptr);
    }
    // Sortie immédiate : M5GFX plante en détruisant un canevas qui a une police chargée
    // (sans conséquence sur l'appareil, où l'interface n'est jamais détruite)
    fflush(stdout);
    _exit(0);
}
