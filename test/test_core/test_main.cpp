// Tests de la logique pure : pio test -e native
#include <unity.h>

#include <string>

#include "core.h"
#include "dsp.h"
#include "voice.h"
#include "firmware.h"

#include <cmath>

using namespace wr;

void setUp() {}
void tearDown() {}

static void test_presets_ok() {
    std::vector<Preset> p;
    std::string err;
    TEST_ASSERT_TRUE(parsePresets("1=Radio Swiss Jazz\r\n\n# commentaire\n 2 = Couleur 3 \n", p, err));
    TEST_ASSERT_EQUAL(2, (int)p.size());
    TEST_ASSERT_EQUAL(1, p[0].slot);
    TEST_ASSERT_EQUAL_STRING("Radio Swiss Jazz", p[0].label.c_str());
    TEST_ASSERT_EQUAL(2, p[1].slot);
    TEST_ASSERT_EQUAL_STRING("Couleur 3", p[1].label.c_str());
}

static void test_presets_errors() {
    std::vector<Preset> p;
    std::string err;
    TEST_ASSERT_FALSE(parsePresets("Radio", p, err));
    TEST_ASSERT_FALSE(parsePresets("0=Radio", p, err));
    TEST_ASSERT_FALSE(parsePresets("21=Radio", p, err));
    TEST_ASSERT_FALSE(parsePresets("x=Radio", p, err));
    TEST_ASSERT_FALSE(parsePresets("1=", p, err));
    TEST_ASSERT_FALSE(parsePresets("\n\n", p, err));
    std::string many;
    for (int i = 1; i <= 13; ++i) many += std::to_string(i) + "=P\n";
    TEST_ASSERT_FALSE(parsePresets(many, p, err));
}

static void test_hex_text() {
    // "La Vie en Rose"
    TEST_ASSERT_EQUAL_STRING("La Vie en Rose", decodeHexText("4c612056696520656e20526f7365").c_str());
    // "Été" en UTF-8
    TEST_ASSERT_EQUAL_STRING("\xC3\x89t\xC3\xA9", decodeHexText("c38974c3a9").c_str());
    TEST_ASSERT_EQUAL_STRING("Unknown", decodeHexText("Unknown").c_str());
    TEST_ASSERT_EQUAL_STRING("0102", decodeHexText("0102").c_str());  // caractères de contrôle : brut
    TEST_ASSERT_EQUAL_STRING("", decodeHexText("").c_str());
}

static void test_player_status() {
    PlayerStatus st = parsePlayerStatus(
        "{\"type\":\"0\", \"status\" : \"play\",\"vol\":\"42\","
        "\"Title\":\"4c612056696520656e20526f7365\",\"Artist\":\"45646974682050696166\"}");
    TEST_ASSERT_TRUE(st.ok);
    TEST_ASSERT_TRUE(st.playing);
    TEST_ASSERT_EQUAL(42, st.volume);
    TEST_ASSERT_EQUAL_STRING("La Vie en Rose", st.title.c_str());
    TEST_ASSERT_EQUAL_STRING("Edith Piaf", st.artist.c_str());

    PlayerStatus stop = parsePlayerStatus("{\"status\":\"stop\",\"Title\":\"unknow\"}");
    TEST_ASSERT_TRUE(stop.ok);
    TEST_ASSERT_FALSE(stop.playing);
    TEST_ASSERT_EQUAL_STRING("", stop.title.c_str());
    TEST_ASSERT_FALSE(parsePlayerStatus("Failed").ok);

    PlayerStatus full = parsePlayerStatus(
        "{\"status\":\"pause\",\"mode\":\"31\",\"Album\":\"4c6120566965\",\"Artist\":\"un_known\"}");
    TEST_ASSERT_EQUAL(31, full.mode);
    TEST_ASSERT_EQUAL_STRING("La Vie", full.album.c_str());
    TEST_ASSERT_EQUAL_STRING("", full.artist.c_str());
    TEST_ASSERT_EQUAL_STRING("Spotify", sourceName(full.mode).c_str());
    TEST_ASSERT_EQUAL_STRING("", sourceName(12345).c_str());
}

static void test_meta_info() {
    TrackMeta m = parseMetaInfo(
        "{ \"metaData\": { \"album\": \"Kind of Blue\", \"title\": \"So What\", "
        "\"albumArtURI\": \"https:\\/\\/i.scdn.co\\/image\\/ab67616d\", "
        "\"sampleRate\": \"44100\", \"bitDepth\": \"16\" } }");
    TEST_ASSERT_TRUE(m.ok);
    TEST_ASSERT_EQUAL_STRING("Kind of Blue", m.album.c_str());
    TEST_ASSERT_EQUAL_STRING("https://i.scdn.co/image/ab67616d", m.artUrl.c_str());
    TEST_ASSERT_EQUAL(44100, m.sampleRate);
    TEST_ASSERT_EQUAL(16, m.bitDepth);

    TrackMeta none = parseMetaInfo(
        "{\"metaData\":{\"album\":\"unknow\",\"albumArtURI\":\"un_known\",\"sampleRate\":\"x\"}}");
    TEST_ASSERT_TRUE(none.ok);
    TEST_ASSERT_EQUAL_STRING("", none.album.c_str());
    TEST_ASSERT_EQUAL_STRING("", none.artUrl.c_str());
    TEST_ASSERT_EQUAL(0, none.sampleRate);
    TEST_ASSERT_FALSE(parseMetaInfo("Failed").ok);
    TEST_ASSERT_EQUAL_STRING("", parseMetaInfo("{\"metaData\":{\"albumArtURI\":\"file:///etc/x\"}}").artUrl.c_str());
}

static void test_wiim_doc_examples() {
    // Exemples tirés de la documentation WiiM (HTTP API v1.2)
    PlayerStatus st = parsePlayerStatus(
        "{\"type\":\"0\",\"ch\":\"2\",\"mode\":\"10\",\"loop\":\"4\",\"eq\":\"0\",\"status\":\"play\","
        "\"curpos\":\"184919\",\"offset_pts\":\"184919\",\"totlen\":\"0\",\"alarmflag\":\"0\","
        "\"plicount\":\"0\",\"plicurr\":\"0\",\"vol\":\"39\",\"mute\":\"0\"}");
    TEST_ASSERT_TRUE(st.ok && st.playing);
    TEST_ASSERT_EQUAL(39, st.volume);
    TEST_ASSERT_EQUAL(10, st.mode);
    TEST_ASSERT_EQUAL_UINT32(184919, st.positionMs);
    TEST_ASSERT_EQUAL_UINT32(0, st.durationMs);
    TEST_ASSERT_FALSE(st.muted);
    TEST_ASSERT_EQUAL_STRING("", st.title.c_str());  // pas de titre dans getPlayerStatus

    TrackMeta m = parseMetaInfo(
        "{ \"metaData\": { \"album\": \"Country Heat\", \"title\": \"Old Dirt Roads\", "
        "\"artist\": \"Owen Riegling\", "
        "\"albumArtURI \": \"https://m.media-amazon.com/images/I/51iU0odzJwL.jpg\", "
        "\"sampleRate \": \"44100\", \"bitDepth\": \"16\" } }");
    TEST_ASSERT_EQUAL_STRING("Old Dirt Roads", m.title.c_str());
    TEST_ASSERT_EQUAL_STRING("Owen Riegling", m.artist.c_str());
    TEST_ASSERT_EQUAL_STRING("https://m.media-amazon.com/images/I/51iU0odzJwL.jpg", m.artUrl.c_str());
    TEST_ASSERT_EQUAL(44100, m.sampleRate);

    std::vector<Preset> p;
    TEST_ASSERT_TRUE(parsePresetInfo(
        "{ \"preset_num\": 3, \"preset_list\": [{ \"number\": 1, \"name\": \"BBC Radio Norfolk\", "
        "\"url\": \"http:\\/\\/as-hls-ww-live.akamaized.net\\/x.m3u8\", \"source\": \"Linkplay Radio\", "
        "\"picurl\": \"http:\\/\\/cdn-profiles.tunein.com\\/s6852\\/images\\/logoq.jpg?t=6383\" }, "
        "{ \"number\": 8, \"name\": \"1. Country Heat\", \"url\": \"unknow\", \"source\": \"Prime\", "
        "\"picurl\": \"https:\\/\\/m.media-amazon.com\\/images\\/I\\/51uR6AJUAQL.jpg\" }, "
        "{ \"number\": 6, \"name\": \"Radio Paradise\", \"url\": \"http:\\/\\/stream.radioparadise.com\\/flacm\", "
        "\"source\": \"RadioParadise\", \"picurl\": \"\" }] }",
        p));
    TEST_ASSERT_EQUAL(3, (int)p.size());
    TEST_ASSERT_EQUAL(1, p[0].slot);  // triés par numéro
    TEST_ASSERT_EQUAL(6, p[1].slot);
    TEST_ASSERT_EQUAL(8, p[2].slot);
    TEST_ASSERT_EQUAL_STRING("BBC Radio Norfolk", p[0].label.c_str());
    TEST_ASSERT_EQUAL_STRING("http://cdn-profiles.tunein.com/s6852/images/logoq.jpg?t=6383", p[0].picUrl.c_str());
    TEST_ASSERT_EQUAL_STRING("", p[1].picUrl.c_str());
    TEST_ASSERT_FALSE(parsePresetInfo("Failed", p));
    TEST_ASSERT_TRUE(parsePresetInfo("{\"preset_num\":0,\"preset_list\":[]}", p));
    TEST_ASSERT_EQUAL(0, (int)p.size());

    TEST_ASSERT_EQUAL_STRING("0:00", formatTime(0).c_str());
    TEST_ASSERT_EQUAL_STRING("3:04", formatTime(184919).c_str());
    TEST_ASSERT_EQUAL_STRING("1:02:03", formatTime(3723000).c_str());
    TEST_ASSERT_EQUAL_STRING("TIDAL", sourceName(32).c_str());
}

static void test_discovery() {
    std::string ip;
    TEST_ASSERT_TRUE(ssdpHost("HTTP/1.1 200 OK\r\nCACHE-CONTROL: max-age=1800\r\nEXT:\r\n"
                              "LOCATION: http://192.168.1.50:49152/description.xml\r\n"
                              "ST: urn:schemas-upnp-org:device:MediaRenderer:1\r\n\r\n",
                              ip));
    TEST_ASSERT_EQUAL_STRING("192.168.1.50", ip.c_str());
    TEST_ASSERT_TRUE(ssdpHost("HTTP/1.1 200 OK\r\nlocation: http://10.0.0.7/upnp.xml\r\n", ip));
    TEST_ASSERT_EQUAL_STRING("10.0.0.7", ip.c_str());
    TEST_ASSERT_FALSE(ssdpHost("HTTP/1.1 200 OK\r\nLOCATION: http://wiim.local:49152/d.xml\r\n", ip));
    TEST_ASSERT_FALSE(ssdpHost("HTTP/1.1 200 OK\r\nLOCATION: http://192.168.1.300/d.xml\r\n", ip));
    TEST_ASSERT_FALSE(ssdpHost("HTTP/1.1 200 OK\r\nST: x\r\n", ip));

    // Exemple de la documentation WiiM (getStatusEx, extrait)
    WiimIdentity w = parseStatusEx(
        "{\"language\": \"en_us\", \"ssid\": \"WiiM Mini-8FA2\", \"firmware\": \"Linkplay.4.6.425351\", "
        "\"project\": \"Muzo_Mini\", \"uuid\": \"FF970016A6FE22C1660AB4D8\", \"DeviceName\": \"Salon\", "
        "\"GroupName\": \"Salon\"}");
    TEST_ASSERT_TRUE(w.ok);
    TEST_ASSERT_EQUAL_STRING("Salon", w.name.c_str());
    TEST_ASSERT_EQUAL_STRING("FF970016A6FE22C1660AB4D8", w.uuid.c_str());
    WiimIdentity n = parseStatusEx("{\"project\":\"Muzo_Mini\",\"ssid\":\"WiiM Mini-8FA2\"}");
    TEST_ASSERT_TRUE(n.ok);
    TEST_ASSERT_EQUAL_STRING("WiiM Mini-8FA2", n.name.c_str());  // sans DeviceName : ssid
    TEST_ASSERT_FALSE(parseStatusEx("<html>TV</html>").ok);           // autre appareil UPnP
}

static void test_http_frame() {
    // Content-Length : complet seulement quand tout le corps est là
    std::string h = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 2\r\n\r\n";
    TEST_ASSERT_FALSE(httpFrame("HTTP/1.1 200 OK\r\nContent-Len").complete);
    TEST_ASSERT_FALSE(httpFrame(h + "O").complete);
    HttpFrame f = httpFrame(h + "OK");
    TEST_ASSERT_TRUE(f.complete && f.reusable);
    // Le serveur ferme : complet mais pas réutilisable
    f = httpFrame("HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 2\r\n\r\nOK");
    TEST_ASSERT_TRUE(f.complete);
    TEST_ASSERT_FALSE(f.reusable);
    // HTTP/1.0 sans keep-alive explicite : fermeture
    TEST_ASSERT_FALSE(httpFrame("HTTP/1.0 200 OK\r\nContent-Length: 2\r\n\r\nOK").reusable);
    TEST_ASSERT_TRUE(httpFrame("HTTP/1.0 200 OK\r\nConnection: Keep-Alive\r\nContent-Length: 2\r\n\r\nOK").reusable);
    // Chunked : complet au bloc de taille 0
    std::string c = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\nOK\r\n";
    TEST_ASSERT_FALSE(httpFrame(c).complete);
    TEST_ASSERT_TRUE(httpFrame(c + "0\r\n\r\n").complete);
    TEST_ASSERT_TRUE(httpFrame("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n").complete);
    // Sans longueur : lire jusqu'à la fermeture
    f = httpFrame("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nOK");
    TEST_ASSERT_FALSE(f.complete);
    TEST_ASSERT_FALSE(f.reusable);
    // Corps vide (Content-Length: 0)
    TEST_ASSERT_TRUE(httpFrame("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n").complete);
}

static void test_quality() {
    TEST_ASSERT_EQUAL_STRING("16 bits \xC2\xB7 44,1 kHz", formatQuality(44100, 16).c_str());
    TEST_ASSERT_EQUAL_STRING("24 bits \xC2\xB7 96 kHz", formatQuality(96000, 24).c_str());
    TEST_ASSERT_EQUAL_STRING("48 kHz", formatQuality(48000, 0).c_str());
    TEST_ASSERT_EQUAL_STRING("24 bits", formatQuality(0, 24).c_str());
    TEST_ASSERT_EQUAL_STRING("", formatQuality(0, 0).c_str());
    TEST_ASSERT_EQUAL_STRING("88,2 kHz", formatQuality(88200, 0).c_str());
}

static void test_urls_and_images() {
    TEST_ASSERT_TRUE(isHttpUrl("https://i.scdn.co/image/ab67616d"));
    TEST_ASSERT_TRUE(isHttpUrl("http://192.168.1.20:8080/cover.jpg"));
    TEST_ASSERT_FALSE(isHttpUrl("https://"));
    TEST_ASSERT_FALSE(isHttpUrl("https:///path"));
    TEST_ASSERT_FALSE(isHttpUrl("ftp://host/x"));
    TEST_ASSERT_FALSE(isHttpUrl("/tmp/cover.jpg"));
    TEST_ASSERT_FALSE(isHttpUrl("http://host/a b"));
    TEST_ASSERT_FALSE(isHttpUrl("http://host/\r\nX: y"));

    const uint8_t jpg[] = {0xFF, 0xD8, 0xFF, 0xE0};
    const uint8_t png[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0};
    const uint8_t gif[] = {'G', 'I', 'F', '8', '9', 'a'};
    TEST_ASSERT_TRUE(sniffImage(jpg, sizeof jpg) == ImageType::Jpeg);
    TEST_ASSERT_TRUE(sniffImage(png, sizeof png) == ImageType::Png);
    TEST_ASSERT_TRUE(sniffImage(gif, sizeof gif) == ImageType::Unknown);
    TEST_ASSERT_TRUE(sniffImage(jpg, 2) == ImageType::Unknown);
}

static void test_json_escapes() {
    std::string v;
    TEST_ASSERT_TRUE(jsonString("{\"a\":\"x\\\"y\\u00e9\"}", "a", v));
    TEST_ASSERT_EQUAL_STRING("x\"y\xC3\xA9", v.c_str());
    TEST_ASSERT_FALSE(jsonString("{\"a\":1}", "a", v));
    TEST_ASSERT_FALSE(jsonString("{\"b\":\"1\"}", "a", v));
}

static void test_http_response() {
    HttpResponse r;
    TEST_ASSERT_TRUE(parseHttpResponse("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nOK", r));
    TEST_ASSERT_EQUAL(200, r.status);
    TEST_ASSERT_EQUAL_STRING("OK", r.body.c_str());

    TEST_ASSERT_TRUE(parseHttpResponse(
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: Chunked\r\n\r\n4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n", r));
    TEST_ASSERT_EQUAL_STRING("Wikipedia", r.body.c_str());

    TEST_ASSERT_TRUE(parseHttpResponse("HTTP/1.0 404 Not Found\r\n\r\n", r));
    TEST_ASSERT_EQUAL(404, r.status);
    TEST_ASSERT_FALSE(parseHttpResponse("garbage", r));
    TEST_ASSERT_FALSE(parseHttpResponse("HTTP/1.1 200 OK\r\nno-end", r));
}

static void test_fingerprint() {
    uint8_t fp[32], other[32];
    std::string hex(64, 'a');
    TEST_ASSERT_TRUE(parseFingerprint(hex, fp));
    TEST_ASSERT_EQUAL_HEX8(0xaa, fp[0]);
    std::string colon;
    for (int i = 0; i < 32; ++i) colon += (i ? ":AA" : "AA");
    TEST_ASSERT_TRUE(parseFingerprint(colon, other));
    TEST_ASSERT_TRUE(constantTimeEq(fp, other, 32));
    other[31] ^= 1;
    TEST_ASSERT_FALSE(constantTimeEq(fp, other, 32));
    TEST_ASSERT_FALSE(parseFingerprint("abcd", fp));
    TEST_ASSERT_FALSE(parseFingerprint(std::string(64, 'z'), fp));
    TEST_ASSERT_EQUAL_STRING(hex.c_str(), toHex(fp, 32).c_str());
}

static void test_hosts() {
    TEST_ASSERT_TRUE(isValidHost("192.168.1.50"));
    TEST_ASSERT_TRUE(isValidHost("wiim.local"));
    TEST_ASSERT_TRUE(isValidHost("192.168.1.50:8443"));
    TEST_ASSERT_FALSE(isValidHost(""));
    TEST_ASSERT_FALSE(isValidHost("192.168.1.50:99999"));
    TEST_ASSERT_FALSE(isValidHost("evil host"));
    TEST_ASSERT_FALSE(isValidHost("a/b"));
    std::string name;
    uint16_t port;
    splitHostPort("10.0.0.2:8443", name, port, 443);
    TEST_ASSERT_EQUAL_STRING("10.0.0.2", name.c_str());
    TEST_ASSERT_EQUAL(8443, port);
    splitHostPort("10.0.0.2", name, port, 443);
    TEST_ASSERT_EQUAL(443, port);
}

static void test_html_escape() {
    TEST_ASSERT_EQUAL_STRING("&lt;script&gt;&amp;&quot;&#39;",
                             htmlEscape("<script>&\"'").c_str());
}

// Mesure factice : 10 px par caractère affiché (UTF-8 compté en points de code)
static int fakeMeasure(const std::string& s) {
    int n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++n;
    return n * 10;
}

static void test_wrap() {
    auto l = wrapText("Radio Swiss Jazz", 200, 2, fakeMeasure);
    TEST_ASSERT_EQUAL(1, (int)l.size());
    l = wrapText("Playlist du dimanche matin", 120, 2, fakeMeasure);
    TEST_ASSERT_EQUAL(2, (int)l.size());
    TEST_ASSERT_EQUAL_STRING("Playlist du", l[0].c_str());
    TEST_ASSERT_EQUAL_STRING("dimanche\xE2\x80\xA6", l[1].c_str());
    l = wrapText("Un nom vraiment beaucoup trop long pour le bouton", 120, 2, fakeMeasure);
    TEST_ASSERT_EQUAL(2, (int)l.size());
    TEST_ASSERT_TRUE(fakeMeasure(l[1]) <= 120);
    TEST_ASSERT_TRUE(l[1].size() >= 3 && l[1].compare(l[1].size() - 3, 3, "\xE2\x80\xA6") == 0);
    // Mot unique trop long, avec accents : coupé proprement sur une frontière UTF-8
    l = wrapText("Éééééééééééééé", 60, 1, fakeMeasure);
    TEST_ASSERT_EQUAL(1, (int)l.size());
    TEST_ASSERT_TRUE(fakeMeasure(l[0]) <= 60);
}

static void checkLayout(int W, int H, int n) {
    Layout L = computeLayout(W, H, n);
    const Rect screen{0, 0, W, H};
    auto inside = [&](const Rect& r) { return r.x >= 0 && r.y >= 0 && r.right() <= W && r.bottom() <= H; };
    // Blocs principaux : dans l'écran, sans chevauchement
    std::vector<Rect> blocks = {L.art, L.info, L.progress, L.transport[kPrev], L.transport[kPlayPause],
                                L.transport[kNext], L.mic, L.volume, L.navHome, L.navSettings};
    if (n > 0) blocks.push_back(L.strip);
    for (size_t i = 0; i < blocks.size(); ++i) {
        TEST_ASSERT_TRUE(inside(blocks[i]));
        TEST_ASSERT_TRUE(blocks[i].w > 0 && blocks[i].h > 0);
        for (size_t j = i + 1; j < blocks.size(); ++j) TEST_ASSERT_FALSE(blocks[i].intersects(blocks[j]));
    }
    TEST_ASSERT_EQUAL(L.art.w, L.art.h);
    // Barre de navigation : boutons dedans, rien d'autre ne la touche
    for (const Rect& r : {L.navHome, L.navSettings}) {
        TEST_ASSERT_TRUE(r.x >= L.rail.x && r.right() <= L.rail.right() && r.y >= L.rail.y && r.bottom() <= L.rail.bottom());
        TEST_ASSERT_TRUE(r.h >= 44 * std::min(W, H) / 540);
    }
    for (const Rect& r : {L.art, L.info, L.progress, L.volume, L.strip, L.transport[kPrev], L.mic})
        TEST_ASSERT_FALSE(r.intersects(L.rail));
    // Éléments de la pilule de volume : dedans, dans l'ordre, sans chevauchement
    for (const Rect& r : {L.transport[kVolDown], L.mute, L.volBar, L.transport[kVolUp]}) {
        TEST_ASSERT_TRUE(r.x >= L.volume.x && r.right() <= L.volume.right());
        TEST_ASSERT_TRUE(r.y >= L.volume.y && r.bottom() <= L.volume.bottom());
    }
    TEST_ASSERT_TRUE(L.transport[kVolDown].right() <= L.mute.x);
    TEST_ASSERT_TRUE(L.mute.right() <= L.volBar.x);
    TEST_ASSERT_TRUE(L.volBar.right() <= L.transport[kVolUp].x);
    TEST_ASSERT_TRUE(L.volBar.w > W / 6);
    // Cibles tactiles : au moins 44 px à l'échelle 540 (≈ 7 mm sur le Tab5)
    const int minTouch = 44 * std::min(W, H) / 540;
    for (int t = 0; t < kTransportCount; ++t) TEST_ASSERT_TRUE(L.transport[t].h >= minTouch * 9 / 10);
    TEST_ASSERT_TRUE(L.transport[kPlayPause].w >= L.transport[kPrev].w);  // lecture mise en avant
    // Presets : une page de tuiles dans la bande, sans chevauchement
    TEST_ASSERT_EQUAL(std::min(n, L.perPage), (int)L.presets.size());
    for (size_t i = 0; i < L.presets.size(); ++i) {
        const Rect& r = L.presets[i];
        TEST_ASSERT_TRUE(r.x >= L.strip.x && r.right() <= L.strip.right() && r.y >= L.strip.y &&
                         r.bottom() <= L.strip.bottom());
        TEST_ASSERT_TRUE(r.h >= minTouch * 2);
        for (size_t j = i + 1; j < L.presets.size(); ++j) TEST_ASSERT_FALSE(r.intersects(L.presets[j]));
    }
    // Textes : assez de place pour puces + titre + artiste
    TEST_ASSERT_TRUE(L.info.h >= 140 * std::min(W, H) / 540);
}

static void test_layout() {
    for (int n : {0, 1, 6, 12}) {
        checkLayout(1280, 720, n);  // Tab5 paysage
        checkLayout(720, 1280, n);  // Tab5 portrait
        checkLayout(960, 540, n);
    }
    Layout L = computeLayout(1280, 720, 12);
    TEST_ASSERT_EQUAL(6, L.perPage);  // 12 presets : 2 pages de 6
    TEST_ASSERT_TRUE(L.art.w >= 400);  // la pochette domine
    L = computeLayout(720, 1280, 12);
    TEST_ASSERT_EQUAL(6, L.perPage);  // portrait : 2 rangées de 3
    TEST_ASSERT_TRUE(nameColor("FIP") == nameColor("FIP"));
    TEST_ASSERT_TRUE(nameColor("FIP") != nameColor("Nova"));
    // Statut "loading" : en lecture et en chargement
    PlayerStatus s = parsePlayerStatus("{\"status\":\"loading\"}");
    TEST_ASSERT_TRUE(s.playing && s.loading);
}

static void test_accent() {
    const uint32_t fb = 0x123456;
    std::vector<uint8_t> px;
    auto fill = [&](int n, uint8_t r, uint8_t g, uint8_t b) {
        for (int i = 0; i < n; ++i) px.insert(px.end(), {r, g, b});
    };
    // Noir, blanc, gris : pas de teinte, couleur par défaut
    fill(100, 0, 0, 0);
    fill(100, 255, 255, 255);
    fill(100, 128, 128, 128);
    TEST_ASSERT_EQUAL_HEX32(fb, accentColor(px.data(), px.size() / 3, fb));
    TEST_ASSERT_EQUAL_HEX32(fb, accentColor(nullptr, 0, fb));
    // Pochette surtout grise avec une tache bleue : l'accent est bleu
    fill(60, 30, 60, 220);
    uint32_t c = accentColor(px.data(), px.size() / 3, fb);
    int r = c >> 16, g = (c >> 8) & 0xFF, b = c & 0xFF;
    TEST_ASSERT_TRUE(b > r && b > g);
    // Rouge sombre : éclairci pour rester lisible sur fond sombre
    px.clear();
    fill(50, 90, 10, 10);
    c = accentColor(px.data(), px.size() / 3, fb);
    r = c >> 16;
    g = (c >> 8) & 0xFF;
    TEST_ASSERT_TRUE(r > 180 && r > g + 60);
}

static void test_spectrum() {
    const int n = 512, sr = 16000, bands = 24;
    std::vector<int16_t> x(n);
    float db[bands];
    // Silence : tout très bas
    spectrumBands(x.data(), n, sr, bands, db);
    for (float d : db) TEST_ASSERT_TRUE(d < -90);
    // Sinusoïde 1 kHz à pleine échelle : une seule bande forte, proche de 0 dB
    for (int i = 0; i < n; ++i) x[i] = (int16_t)(32000 * std::sin(2 * 3.14159265 * 1000 * i / sr));
    spectrumBands(x.data(), n, sr, bands, db);
    int best = 0;
    for (int b = 1; b < bands; ++b)
        if (db[b] > db[best]) best = b;
    float lo = 60 * std::pow(8000.0f / 60, float(best) / bands), hi = 60 * std::pow(8000.0f / 60, float(best + 1) / bands);
    TEST_ASSERT_TRUE(lo <= 1060 && hi >= 940);
    TEST_ASSERT_TRUE(db[best] > -3 && db[best] < 1);
    TEST_ASSERT_TRUE(db[0] < db[best] - 30);  // basses loin derrière

    // Lissage : une bande forte monte, puis redescend doucement dans le silence
    Visualizer v(bands);
    for (int k = 0; k < 20; ++k) v.update(db);
    TEST_ASSERT_TRUE(v.levels()[best] > 0.8f);
    TEST_ASSERT_TRUE(v.levels()[0] < 0.2f);
    float quiet[bands];
    for (float& q : quiet) q = -100;
    v.update(quiet);
    TEST_ASSERT_TRUE(v.levels()[best] > 0.5f);  // pas de chute brutale
    TEST_ASSERT_TRUE(v.peaks()[best] > 0.8f);   // crête maintenue
    for (int k = 0; k < 200; ++k) v.update(quiet);
    TEST_ASSERT_TRUE(v.levels()[best] < 0.05f);
}

static void test_motion() {
    MotionDetector m;
    // Posé à plat : rien, même avec une petite vibration (0,03 g)
    for (int i = 0; i < 300; ++i) TEST_ASSERT_FALSE(m.feed(0.01f * (i % 3), 0.03f * ((i % 2) ? 1 : -1), 1.0f));
    // Soulevé : écart franc et soutenu, un seul déclenchement
    int triggers = 0;
    for (int i = 0; i < 10; ++i) triggers += m.feed(0.3f, 0.2f, 0.85f);
    TEST_ASSERT_EQUAL(1, triggers);
    // Choc isolé d'un échantillon (porte qui claque) : ignoré
    MotionDetector m2;
    for (int i = 0; i < 100; ++i) m2.feed(0, 0, 1);
    TEST_ASSERT_FALSE(m2.feed(0, 0, 1.6f));
    TEST_ASSERT_FALSE(m2.feed(0, 0, 1.0f));
    TEST_ASSERT_FALSE(m2.feed(0, 0, 1.0f));
}

static void test_voice_text() {
    TEST_ASSERT_EQUAL_STRING("joue fip", normalizeText("Joue FIP !").c_str());
    TEST_ASSERT_EQUAL_STRING("mets france culture", normalizeText("  Mets \xC2\xAB France Culture \xC2\xBB.").c_str());
    TEST_ASSERT_EQUAL_STRING("ecoute l heure des reves", normalizeText("\xC3\x89""coute l'Heure des r\xC3\xAA""ves").c_str());
    TEST_ASSERT_EQUAL_STRING("coeur", normalizeText("C\xC5\x93ur").c_str());
    TEST_ASSERT_TRUE(similarity("radio swiss jazz", "radio swiss jazz") == 1);
    TEST_ASSERT_TRUE(similarity("radio swiss jaz", "radio swiss jazz") > 0.8f);
    TEST_ASSERT_TRUE(similarity("nova", "france inter") < 0.3f);
}

static void test_voice_intents() {
    std::vector<Preset> p = {{1, "FIP", ""}, {2, "FIP Jazz", ""}, {3, "Radio Swiss Jazz", ""},
                             {4, "Couleur 3", ""}, {8, "Playlist du dimanche", ""}};
    auto k = [&](const char* t) { return (int)parseVoice(t, p).kind; };
    TEST_ASSERT_EQUAL(VoiceIntent::Pause, k("Pause."));
    TEST_ASSERT_EQUAL(VoiceIntent::Pause, k("Arr\xC3\xAA""te"));
    TEST_ASSERT_EQUAL(VoiceIntent::Play, k("Reprends"));
    TEST_ASSERT_EQUAL(VoiceIntent::Play, k("Joue"));
    TEST_ASSERT_EQUAL(VoiceIntent::Next, k("Chanson suivante"));
    TEST_ASSERT_EQUAL(VoiceIntent::Prev, k("Pr\xC3\xA9""c\xC3\xA9""dent"));
    TEST_ASSERT_EQUAL(VoiceIntent::VolUp, k("Plus fort s'il te pla\xC3\xAEt"));
    TEST_ASSERT_EQUAL(VoiceIntent::VolDown, k("Baisse un peu"));
    TEST_ASSERT_EQUAL(VoiceIntent::Mute, k("Coupe le son"));
    TEST_ASSERT_EQUAL(VoiceIntent::Unmute, k("Remets le son"));

    VoiceIntent v = parseVoice("Volume \xC3\xA0 30", p);
    TEST_ASSERT_EQUAL(VoiceIntent::SetVolume, v.kind);
    TEST_ASSERT_EQUAL(30, v.volume);
    TEST_ASSERT_EQUAL(45, parseVoice("volume quarante cinq", p).volume);
    TEST_ASSERT_EQUAL(17, parseVoice("volume dix-sept", p).volume);
    TEST_ASSERT_EQUAL(21, parseVoice("volume vingt et un", p).volume);
    TEST_ASSERT_EQUAL(72, parseVoice("volume soixante-douze", p).volume);
    TEST_ASSERT_EQUAL(100, parseVoice("volume 150", p).volume);

    v = parseVoice("Joue FIP", p);  // exact : FIP, pas FIP Jazz
    TEST_ASSERT_EQUAL(VoiceIntent::Preset, v.kind);
    TEST_ASSERT_EQUAL(0, v.preset);
    TEST_ASSERT_EQUAL(1, parseVoice("Mets FIP Jazz", p).preset);
    TEST_ASSERT_EQUAL(2, parseVoice("Joue Radio Swiss Jazz", p).preset);
    TEST_ASSERT_EQUAL(2, parseVoice("Je veux \xC3\xA9""couter Swiss Jazz", p).preset);
    TEST_ASSERT_EQUAL(3, parseVoice("Mets la radio Couleur 3", p).preset);
    TEST_ASSERT_EQUAL(4, parseVoice("Lance la playlist du dimanche", p).preset);
    TEST_ASSERT_EQUAL(4, parseVoice("Preset num\xC3\xA9ro 8", p).preset);  // numéro du WiiM, pas la position

    v = parseVoice("Mets France Culture", p);
    TEST_ASSERT_EQUAL(VoiceIntent::Radio, v.kind);
    TEST_ASSERT_EQUAL_STRING("france culture", v.query.c_str());
    TEST_ASSERT_EQUAL_STRING("nova", parseVoice("Je voudrais \xC3\xA9""couter la radio Nova", p).query.c_str());
    TEST_ASSERT_EQUAL(VoiceIntent::None, k(""));
}

static void test_voice_radio_and_audio() {
    // Extrait réel de Radio Browser (champs inutiles retirés)
    const char* json =
        "[{\"name\":\"France Culture (sans pub)\",\"url\":\"x\",\"url_resolved\":\"https://stream.radiofrance.fr/"
        "franceculture/franceculture.m3u8\",\"favicon\":\"\",\"hls\":1,\"clickcount\":8},"
        "{\"name\":\"France Culture\",\"url\":\"http://icecast.radiofrance.fr/franceculture-hifi.aac\","
        "\"url_resolved\":\"http://icecast.radiofrance.fr/franceculture-hifi.aac\",\"favicon\":"
        "\"https://www.radiofrance.fr/favicon.png\",\"tags\":\"news,{culture}\",\"hls\":0,\"clickcount\":277},"
        "{\"name\":\"Cass\xC3\xA9""e\",\"url_resolved\":\"file:///etc/passwd\",\"hls\":0,\"clickcount\":9999}]";
    auto st = parseRadioBrowser(json);
    TEST_ASSERT_EQUAL(2, (int)st.size());  // l'adresse non http est écartée
    TEST_ASSERT_TRUE(st[0].hls);
    TEST_ASSERT_EQUAL_STRING("https://www.radiofrance.fr/favicon.png", st[1].favicon.c_str());
    TEST_ASSERT_EQUAL(1, bestStation(st, "france culture"));
    TEST_ASSERT_EQUAL(-1, bestStation({}, "x"));

    TEST_ASSERT_EQUAL_STRING("france%20culture", urlEncode("france culture").c_str());
    TEST_ASSERT_EQUAL_STRING("http://a.b/c%3Fd%3D1%26e", urlEncode("http://a.b/c?d=1&e", true).c_str());

    uint8_t h[44];
    wavHeader(h, 32000, 16000);
    TEST_ASSERT_EQUAL_MEMORY("RIFF", h, 4);
    TEST_ASSERT_EQUAL_MEMORY("WAVEfmt ", h + 8, 8);
    TEST_ASSERT_EQUAL(36 + 32000, h[4] | h[5] << 8 | h[6] << 16 | h[7] << 24);
    TEST_ASSERT_EQUAL(16000, h[24] | h[25] << 8 | h[26] << 16);
    TEST_ASSERT_EQUAL(32000, h[40] | h[41] << 8 | h[42] << 16);

    // Fin de parole : bruit, voix, puis silence
    SpeechEndpoint e;
    for (int i = 0; i < 20; ++i) TEST_ASSERT_EQUAL(SpeechEndpoint::Waiting, e.feed(0.005f));
    for (int i = 0; i < 30; ++i) e.feed(0.1f);
    TEST_ASSERT_EQUAL(SpeechEndpoint::Speaking, e.state());
    for (int i = 0; i < 20; ++i) e.feed(0.1f * (i % 2));  // pauses courtes entre les mots
    TEST_ASSERT_EQUAL(SpeechEndpoint::Speaking, e.state());
    for (int i = 0; i < 40; ++i) e.feed(0.006f);
    TEST_ASSERT_EQUAL(SpeechEndpoint::Done, e.state());
    // Rien dit : abandon au bout de 4 s
    SpeechEndpoint quiet;
    for (int i = 0; i < 200; ++i) quiet.feed(0.004f);
    TEST_ASSERT_EQUAL(SpeechEndpoint::NothingHeard, quiet.state());
    // Parole sans fin : coupée à 7 s
    SpeechEndpoint talk;
    talk.feed(0.004f);
    for (int i = 0; i < 300; ++i) talk.feed(0.2f);
    TEST_ASSERT_EQUAL(SpeechEndpoint::Done, talk.state());
}

static void test_firmware_stream() {
    // Application seule : tout est écrit
    std::vector<uint8_t> app(5000, 0x11);
    app[0] = 0xE9;
    FirmwareStream a;
    const uint8_t* out;
    size_t total = 0;
    for (size_t i = 0; i < app.size(); i += 1436) total += a.feed(app.data() + i, std::min<size_t>(1436, app.size() - i), &out);
    TEST_ASSERT_EQUAL(FirmwareStream::App, a.kind());
    TEST_ASSERT_EQUAL(5000, (int)total);

    // Image complète : seule la partie à partir de 0x10000 est écrite, octets identiques
    std::vector<uint8_t> merged(0x10000 + 3000, 0xFF);
    merged[0x2000] = 0xE9;  // chargeur de démarrage
    for (size_t i = 0x10000; i < merged.size(); ++i) merged[i] = (uint8_t)(i * 7);
    merged[0x10000] = 0xE9;
    FirmwareStream m;
    std::vector<uint8_t> written;
    for (size_t i = 0; i < merged.size(); i += 1436) {
        size_t k = m.feed(merged.data() + i, std::min<size_t>(1436, merged.size() - i), &out);
        written.insert(written.end(), out, out + k);
    }
    TEST_ASSERT_EQUAL(FirmwareStream::Merged, m.kind());
    TEST_ASSERT_EQUAL(3000, (int)written.size());
    TEST_ASSERT_TRUE(std::equal(written.begin(), written.end(), merged.begin() + 0x10000));

    // Image complète sans application valide à 0x10000 : refusée
    std::vector<uint8_t> bad(0x10000 + 100, 0xFF);
    FirmwareStream b;
    for (size_t i = 0; i < bad.size(); i += 1436) b.feed(bad.data() + i, std::min<size_t>(1436, bad.size() - i), &out);
    TEST_ASSERT_EQUAL(FirmwareStream::Invalid, b.kind());
    TEST_ASSERT_EQUAL(0, (int)b.written());

    // Autre fichier (zip, image…) : refusé dès le premier octet
    const uint8_t zip[] = {'P', 'K', 3, 4};
    FirmwareStream z;
    TEST_ASSERT_EQUAL(0, (int)z.feed(zip, sizeof zip, &out));
    TEST_ASSERT_EQUAL(FirmwareStream::Invalid, z.kind());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_presets_ok);
    RUN_TEST(test_presets_errors);
    RUN_TEST(test_hex_text);
    RUN_TEST(test_player_status);
    RUN_TEST(test_meta_info);
    RUN_TEST(test_quality);
    RUN_TEST(test_http_frame);
    RUN_TEST(test_discovery);
    RUN_TEST(test_wiim_doc_examples);
    RUN_TEST(test_urls_and_images);
    RUN_TEST(test_json_escapes);
    RUN_TEST(test_http_response);
    RUN_TEST(test_fingerprint);
    RUN_TEST(test_hosts);
    RUN_TEST(test_html_escape);
    RUN_TEST(test_wrap);
    RUN_TEST(test_layout);
    RUN_TEST(test_accent);
    RUN_TEST(test_spectrum);
    RUN_TEST(test_motion);
    RUN_TEST(test_voice_text);
    RUN_TEST(test_voice_intents);
    RUN_TEST(test_voice_radio_and_audio);
    RUN_TEST(test_firmware_stream);
    return UNITY_END();
}
