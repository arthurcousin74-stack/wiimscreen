// Aperçu de l'interface sur PC : dessine les écrans avec le vrai src/ui.cpp et
// les enregistre en PNG.
//
//   pio run -e preview
//   .pio/build/preview/program pochette.jpg dossier_sortie/
//   python tools/preview/eink.py dossier_sortie/*.ppm   # PNG en 16 niveaux de gris
#include <cstdio>
#include <vector>

#include "M5Unified.h"
#include "art.h"
#include "ui.h"

PreviewM5 M5;

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

// Image brute PPM (convertie en PNG par tools/preview/eink.py)
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

static Settings settings(int columns, const char* presets) {
    Settings s;
    s.columns = columns;
    std::string err;
    wr::parsePresets(presets, s.presets, err);
    return s;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s pochette.jpg dossier_sortie\n", argv[0]);
        return 1;
    }
    ArtImage art;
    if (!loadFile(argv[1], art)) {
        fprintf(stderr, "image illisible : %s\n", argv[1]);
        return 1;
    }
    const std::string out = argv[2];
    auto& D = M5.Display;
    D.setColorDepth(24);
    if (!D.createSprite(960, 540)) {
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
    st.player.title = "So What";
    st.player.artist = "Miles Davis";
    st.player.album = "Kind of Blue (Legacy Edition)";
    st.meta.sampleRate = 44100;
    st.meta.bitDepth = 16;

    const char* six = "1=FIP\n2=Radio Swiss Jazz\n3=France Inter\n4=Couleur 3\n5=Playlist du dimanche\n6=Nova";
    Ui ui;
    ui.begin(settings(3, six));
    ui.drawMain(st, &art, true);
    save(out, "1-lecture.ppm");

    // Bouton enfoncé (retour immédiat d'un appui)
    ui.showPreset(1, true);
    ui.showTransport(wr::kPlayPause, true, true);
    save(out, "2-appui.ppm");

    // Pause, sans pochette, avec un message
    st.player.playing = false;
    st.player.mode = 0;
    st.player.title = "Le Temps de l'amour";
    st.player.artist = "Françoise Hardy";
    st.player.album = "";
    st.meta = wr::TrackMeta();
    st.message = "Certificat du WiiM enregistré";
    ui.drawMain(st, nullptr, true);
    save(out, "3-pause-sans-pochette.ppm");

    // 12 presets : bandeau compact
    st.message = "";
    st.player.playing = true;
    Ui ui12;
    ui12.begin(settings(3, "1=FIP\n2=FIP Jazz\n3=FIP Groove\n4=Radio Swiss Jazz\n5=Radio Swiss Classic\n"
                           "6=France Inter\n7=France Culture\n8=Couleur 3\n9=Nova\n10=TSF Jazz\n"
                           "11=Playlist du dimanche\n12=Découvertes"));
    ui12.drawMain(st, &art, true);
    save(out, "4-douze-presets.ppm");

    // Hors ligne
    st.online = false;
    st.message = "WiiM injoignable : vérifie qu'il est allumé et son IP";
    ui.drawMain(st, &art, true);
    save(out, "5-hors-ligne.ppm");

    ui.showPortal("WiiM-Remote-3F2A", "k7Qp2xRm9w", "http://192.168.4.1", "Première configuration.");
    save(out, "6-configuration.ppm");
    D.unloadFont();  // avant la destruction des objets statiques
    D.deleteSprite();
    return 0;
}
