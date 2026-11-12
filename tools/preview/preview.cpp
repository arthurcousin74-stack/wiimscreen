// Aperçu de l'interface sur PC : dessine les écrans avec le vrai src/ui.cpp et
// les enregistre en PNG.
//
//   pio run -e preview
//   .pio/build/preview/program dossier_images/ dossier_sortie/
//     dossier_images : cover.jpg, et preset1..6 en .jpg ou .png (facultatifs)
//   python tools/preview/topng.py dossier_sortie/*.ppm
#include <unistd.h>

#include <cmath>
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
    ArtImage art;
    if (!loadFile((in + "/cover.jpg").c_str(), art)) {
        fprintf(stderr, "image illisible : %s/cover.jpg\n", in.c_str());
        return 1;
    }
    std::vector<ArtImage> thumbs(6);
    for (int i = 0; i < 6; ++i) loadAny(in + "/preset" + std::to_string(i + 1), thumbs[i]);

    auto& D = M5.Display;
    D.setColorDepth(24);
    if (!D.createSprite(1280, 720)) {
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
    ui.render(st);
    save(out, "1-lecture.ppm");

    // Commande vocale : écoute, puis transcription
    ui.renderVoice(Ui::VoicePhase::Listening, 1.0f);  // pleine voix : anneaux au maximum
    save(out, "10-voix-ecoute.ppm");
    ui.renderVoice(Ui::VoicePhase::Thinking, 0);
    save(out, "11-voix-reflexion.ppm");
    {
        UiState done = st;
        done.message = "\xC2\xAB Mets France Culture \xC2\xBB \xE2\x80\x94 Radio France Culture";
        ui.render(done);
        save(out, "12-voix-resultat.ppm");
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
    radio.message = "Certificat du WiiM enregistré";
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
    }

    // Hors ligne
    UiState off = st;
    off.online = false;
    off.message = "WiiM injoignable : vérifie qu'il est allumé et son IP";
    ui.setArt(&art);
    ui.render(off);
    save(out, "7-hors-ligne.ppm");

    // Portrait (rotation 0) : même interface, pochette en haut, presets sur deux rangées
    {
        D.deleteSprite();
        D.createSprite(720, 1280);
        Ui& up = *new Ui();  // jamais détruite (voir la sortie par _exit)
        Settings sp;
        up.begin(sp);
        up.setPresets(six);
        for (int i = 0; i < 6; ++i) up.setPresetArt(i, &thumbs[i]);
        up.setArt(&art);
        up.setActivePreset(1);
        up.render(st);
        save(out, "9-portrait.ppm");
        D.deleteSprite();
        D.createSprite(1280, 720);
    }

    ui.renderPicker({{"Salon", "192.168.1.50"}, {"Cuisine", "192.168.1.61"}, {"Bureau", "192.168.1.73"}});
    save(out, "15-choix-wiim.ppm");
    ui.renderPicker({});
    save(out, "16-aucun-wiim.ppm");

    ui.renderMenu("57-219c0bc");
    save(out, "13-menu.ppm");
    ui.renderTouchTest(820, 470, 7);
    save(out, "18-test-tactile.ppm");
    ui.showUpdate("http://192.168.1.42", "481203", "57-219c0bc", 63, "R\xC3\xA9" "ception du firmware\xE2\x80\xA6");
    save(out, "14-mise-a-jour.ppm");

    ui.showPortal("WiiM-Remote-3F2A", "k7Qp2xRm9w", "http://192.168.4.1", "Première configuration.");
    save(out, "8-configuration.ppm");
    // Sortie immédiate : M5GFX plante en détruisant un canevas qui a une police chargée
    // (sans conséquence sur l'appareil, où l'interface n'est jamais détruite)
    fflush(stdout);
    _exit(0);
}
