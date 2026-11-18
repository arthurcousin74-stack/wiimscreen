// Tests de la logique pure : pio test -e native
#include <unity.h>

#include <string>

#include <algorithm>

#include "core.h"
#include "i18n.h"
#include "dsp.h"
#include "voice.h"
#include "firmware.h"
#include "decisions.h"
#include "upnp.h"
#include "qobuz.h"

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
    // Espaces en plus après les deux-points (Qobuz : « Transfer-Encoding:  chunked », vu le 2026-11-17)
    TEST_ASSERT_TRUE(httpFrame("HTTP/1.1 200 OK\r\nTransfer-Encoding:  chunked\r\n\r\n0\r\n\r\n").complete);
    TEST_ASSERT_TRUE(httpFrame("HTTP/1.1 200 OK\r\nContent-Length:2\r\n\r\nOK").reusable);
    TEST_ASSERT_FALSE(httpFrame("HTTP/1.1 200 OK\r\nConnection:  close\r\nContent-Length: 2\r\n\r\nOK").reusable);
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

static void test_json_unicode() {
    std::string s;
    TEST_ASSERT_TRUE(jsonString("{\"t\":\"Caf\\u00e9 \\ud83d\\ude00!\"}", "t", s));
    TEST_ASSERT_EQUAL_STRING("Caf\xC3\xA9 \xF0\x9F\x98\x80!", s.c_str());  // paire UTF-16 : un seul caractère
    TEST_ASSERT_TRUE(jsonString("{\"t\":\"a\\ud83db\"}", "t", s));
    TEST_ASSERT_EQUAL_STRING("a\xEF\xBF\xBD" "b", s.c_str());  // moitié seule : U+FFFD
    TEST_ASSERT_EQUAL_STRING("\xF0\x9F\x8E\xB5 x", jsonText("\"\\uD83C\\uDFB5 x\"").c_str());
    TEST_ASSERT_EQUAL_STRING("12", jsonText("\"\\u12\"").c_str());  // chiffres incomplets : pas de caractère inventé
}

static void test_decisions() {
    PlayerStatus now;
    now.playing = true;
    now.title = "B";
    TEST_ASSERT_TRUE(pendingConfirmed(Awaited::Track, now, "A", 0, true));
    now.title = "";
    TEST_ASSERT_FALSE(pendingConfirmed(Awaited::Track, now, "A", 0, true));    // titre manqué : pas un changement
    TEST_ASSERT_FALSE(pendingConfirmed(Awaited::Music, now, "A", 0, true));
    now.title = "A";
    now.positionMs = 1000;
    TEST_ASSERT_TRUE(pendingConfirmed(Awaited::Track, now, "A", 60000, true));  // « précédent » : morceau repris
    TEST_ASSERT_TRUE(pendingConfirmed(Awaited::Playing, now, "A", 0, true));
    TEST_ASSERT_FALSE(pendingConfirmed(Awaited::Playing, now, "A", 0, false));
    now.title = "B";
    now.loading = true;
    TEST_ASSERT_FALSE(pendingConfirmed(Awaited::Music, now, "A", 0, true));      // encore en chargement
    TEST_ASSERT_FALSE(pendingConfirmed(Awaited::None, now, "A", 0, true));

    PlaybackBefore before;
    PlayerStatus after;
    TEST_ASSERT_EQUAL((int)RestoreAction::Nothing, (int)restoreAction(before, after));
    after.title = "Search result";  // rien ne jouait ; le WiiM est en pause sur la file de recherche
    TEST_ASSERT_EQUAL((int)RestoreAction::StopSearchQueue, (int)restoreAction(before, after));
    after.playing = true;
    TEST_ASSERT_EQUAL((int)RestoreAction::Stop, (int)restoreAction(before, after));
    before.playing = true;
    before.title = "Song";
    before.posMs = 90000;
    TEST_ASSERT_EQUAL((int)RestoreAction::Pause, (int)restoreAction(before, after));  // hors file (AirPlay…)
    before.inQueue = true;
    TEST_ASSERT_EQUAL((int)RestoreAction::PutBack, (int)restoreAction(before, after));
    after.title = "Song";
    after.positionMs = 92000;
    TEST_ASSERT_EQUAL((int)RestoreAction::Nothing, (int)restoreAction(before, after));
    after.positionMs = 2000;  // même morceau, reparti du début : la recherche l'a relancé
    TEST_ASSERT_EQUAL((int)RestoreAction::PutBack, (int)restoreAction(before, after));

    // En pause dans une file connue (vu sur un WiiM, 2026-11-17 : le morceau en pause était perdu)
    PlaybackBefore paused;
    paused.title = "Song";
    paused.posMs = 240000;
    paused.inQueue = true;
    PlayerStatus searchQ;
    searchQ.title = "Search result";
    TEST_ASSERT_EQUAL((int)RestoreAction::PutBackPaused, (int)restoreAction(paused, searchQ));
    searchQ.playing = true;
    TEST_ASSERT_EQUAL((int)RestoreAction::PutBackPaused, (int)restoreAction(paused, searchQ));
    PlayerStatus same;
    same.title = "Song";
    TEST_ASSERT_EQUAL((int)RestoreAction::Nothing, (int)restoreAction(paused, same));  // rien n'a bougé
    paused.inQueue = false;  // hors file : rien à remettre
    searchQ.playing = false;
    TEST_ASSERT_EQUAL((int)RestoreAction::StopSearchQueue, (int)restoreAction(paused, searchQ));

    // Écriture suivante de la recherche : file vide = une seule autre écriture, et seulement avec plusieurs mots
    TEST_ASSERT_FALSE(searchTryNext(true, true, 0, true));
    TEST_ASSERT_TRUE(searchTryNext(true, false, 0, false));   // résultats sans rapport : requête peut-être mal lue
    TEST_ASSERT_FALSE(searchTryNext(false, false, 1, false));  // un mot, file vide : inutile d'insister (43 s sinon)
    TEST_ASSERT_TRUE(searchTryNext(false, false, 1, true));
    TEST_ASSERT_FALSE(searchTryNext(false, false, 2, true));

    SearchUndo undo;
    TEST_ASSERT_FALSE(parseSearchUndo("", undo));
    TEST_ASSERT_FALSE(parseSearchUndo("n'importe quoi", undo));
    TEST_ASSERT_FALSE(parseSearchUndo(searchUndoText(SearchUndo{}), undo));  // rien à remettre
    SearchUndo both;
    both.unmute = true;
    both.loop = 3;
    TEST_ASSERT_TRUE(parseSearchUndo(searchUndoText(both), undo));
    TEST_ASSERT_TRUE(undo.unmute);
    TEST_ASSERT_EQUAL(3, undo.loop);
    TEST_ASSERT_TRUE(parseSearchUndo("unmute 0 loop 2\n", undo));
    TEST_ASSERT_FALSE(undo.unmute);
    TEST_ASSERT_EQUAL(2, undo.loop);
    TEST_ASSERT_TRUE(parseSearchUndo("unmute 1 loop 99", undo));  // mode inconnu : le son seulement
    TEST_ASSERT_EQUAL(-1, undo.loop);

    TEST_ASSERT_EQUAL(4, loopWithoutShuffle(3));  // aléatoire sans répétition (vu sur un WiiM, 2026-11-17)
    TEST_ASSERT_EQUAL(0, loopWithoutShuffle(2));
    TEST_ASSERT_EQUAL(1, loopWithoutShuffle(5));
    TEST_ASSERT_EQUAL(-1, loopWithoutShuffle(4));
    TEST_ASSERT_EQUAL(-1, loopWithoutShuffle(-1));

    QueueStability st;
    TEST_ASSERT_FALSE(st.feed(0, 700));
    TEST_ASSERT_FALSE(st.feed(2, 1400));
    for (int k = 0; k < 4; ++k) TEST_ASSERT_FALSE(st.feed(7, 2100 + 700 * k));
    TEST_ASSERT_TRUE(st.feed(7, 4900));  // 4 répétitions du même nombre, plus de 3 s
    QueueStability quick;
    for (int k = 0; k < 4; ++k) TEST_ASSERT_FALSE(quick.feed(3, 100 * k));  // trop tôt
    TEST_ASSERT_TRUE(quick.feed(3, 3000));

    QueueInfo q;
    q.items = {{"So What", "", "", ""}, {"Blue in Green", "", "", ""}};
    TEST_ASSERT_EQUAL(2, queueIndexOf(q, "Blue in Green", false));
    TEST_ASSERT_EQUAL(0, queueIndexOf(q, "blue in green", false));
    TEST_ASSERT_EQUAL(2, queueIndexOf(q, "BLUE IN GREEN!", true));
    TEST_ASSERT_EQUAL(0, queueIndexOf(q, "", true));

    TEST_ASSERT_EQUAL_STRING("Miles Davis", cleanTranscript(" \"Miles Davis.\" ").c_str());
    TEST_ASSERT_EQUAL_STRING("caf", cleanTranscript("caf\xC3\xA9", 4).c_str());  // pas de demi-caractère
    TEST_ASSERT_EQUAL_STRING("", cleanTranscript("...").c_str());
}

static void test_http_response() {
    HttpResponse r;
    TEST_ASSERT_TRUE(parseHttpResponse("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nOK", r));
    TEST_ASSERT_EQUAL(200, r.status);
    TEST_ASSERT_EQUAL_STRING("OK", r.body.c_str());

    TEST_ASSERT_TRUE(parseHttpResponse(
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: Chunked\r\n\r\n4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n", r));
    TEST_ASSERT_EQUAL_STRING("Wikipedia", r.body.c_str());
    // Qobuz (vu le 2026-11-17) : deux espaces, tailles de bloc avec des zéros devant
    TEST_ASSERT_TRUE(parseHttpResponse(
        "HTTP/1.1 200 OK\r\nTransfer-Encoding:  chunked\r\nConnection: close\r\n\r\n00000002\r\n{}\r\n0\r\n\r\n", r));
    TEST_ASSERT_EQUAL_STRING("{}", r.body.c_str());

    TEST_ASSERT_TRUE(parseHttpResponse("HTTP/1.0 404 Not Found\r\n\r\n", r));
    TEST_ASSERT_EQUAL(404, r.status);
    TEST_ASSERT_FALSE(parseHttpResponse("garbage", r));
    TEST_ASSERT_FALSE(parseHttpResponse("HTTP/1.1 200 OK\r\nno-end", r));
    // Longueur de morceau hostile (débordement si additionnée) : se termine, corps borné par l'entrée
    const std::string evil = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\nab\r\nffffffffffffffec\r\nxyz\r\n0\r\n\r\n";
    parseHttpResponse(evil, r);
    TEST_ASSERT_TRUE(r.body.size() <= evil.size());
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
                                L.transport[kNext], L.fav, L.volume, L.navHome, L.navSettings, L.navSearch};
    blocks.push_back(L.strip);  // toujours là, même sans preset
    for (size_t i = 0; i < blocks.size(); ++i) {
        TEST_ASSERT_TRUE(inside(blocks[i]));
        TEST_ASSERT_TRUE(blocks[i].w > 0 && blocks[i].h > 0);
        for (size_t j = i + 1; j < blocks.size(); ++j) TEST_ASSERT_FALSE(blocks[i].intersects(blocks[j]));
    }
    TEST_ASSERT_EQUAL(L.art.w, L.art.h);
    // Barre de navigation : boutons dedans, rien d'autre ne la touche
    for (const Rect& r : {L.navHome, L.navSettings, L.navSearch}) {
        TEST_ASSERT_TRUE(r.x >= L.rail.x && r.right() <= L.rail.right() && r.y >= L.rail.y && r.bottom() <= L.rail.bottom());
        TEST_ASSERT_TRUE(r.h >= 44 * std::min(W, H) / 540);
    }
    for (const Rect& r : {L.art, L.info, L.progress, L.volume, L.strip, L.transport[kPrev]})
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
        checkLayout(800, 480, n);   // écran officiel 7" du Raspberry Pi (version ordinateur)
        checkLayout(480, 800, n);
        checkLayout(1920, 1080, n);  // écran d'ordinateur
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
    s = parsePlayerStatus("{\"status\":\"load\"}");  // WiiM, chargement d'une file
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
}

static void test_deezer_user() {
    TEST_ASSERT_EQUAL_STRING("123456", deezerUserId(" 123456 ").c_str());
    TEST_ASSERT_EQUAL_STRING("123456", deezerUserId("https://www.deezer.com/fr/profile/123456").c_str());
    TEST_ASSERT_EQUAL_STRING("42", deezerUserId("https://www.deezer.com/profile/42?utm=x").c_str());
    TEST_ASSERT_EQUAL_STRING("", deezerUserId("https://link.deezer.com/s/abc").c_str());  // lien court : pas de numéro
    TEST_ASSERT_EQUAL_STRING("", deezerUserId("12a").c_str());
    TEST_ASSERT_EQUAL_STRING("", deezerUserId("").c_str());
    // user/<id>/tracks : même forme que la recherche
    auto t = parseDeezerTracks("{\"data\":[{\"id\":4276762,\"readable\":false,\"title\":\"Little Bit\",\"artist\":"
                               "{\"id\":1,\"name\":\"A\"},\"album\":{\"id\":391,\"title\":\"B\",\"cover_medium\":"
                               "\"https://e/c.jpg\"},\"time_add\":1}],\"total\":1}");
    TEST_ASSERT_EQUAL(1, (int)t.size());
    TEST_ASSERT_EQUAL(391, (int)t[0].albumId);
    TEST_ASSERT_EQUAL_STRING("A", t[0].artist.c_str());
}

static void test_voice_audio() {

    uint8_t h[44];
    wavHeader(h, 32000, 16000);
    TEST_ASSERT_EQUAL_MEMORY("RIFF", h, 4);
    TEST_ASSERT_EQUAL_MEMORY("WAVEfmt ", h + 8, 8);
    TEST_ASSERT_EQUAL(36 + 32000, h[4] | h[5] << 8 | h[6] << 16 | h[7] << 24);
    TEST_ASSERT_EQUAL(16000, h[24] | h[25] << 8 | h[26] << 16);
    TEST_ASSERT_EQUAL(32000, h[40] | h[41] << 8 | h[42] << 16);

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

static void test_upnp_xml() {
    TEST_ASSERT_EQUAL_STRING("a<b>&\"c'", xmlUnescape("a&lt;b&gt;&amp;&quot;c&apos;").c_str());
    TEST_ASSERT_EQUAL_STRING("\xC3\xA9 \xE2\x82\xAC", xmlUnescape("&#233; &#x20AC;").c_str());
    TEST_ASSERT_EQUAL_STRING("&unknown; & x", xmlUnescape("&unknown; & x").c_str());
    TEST_ASSERT_EQUAL_STRING("&lt;a&gt; &amp;", xmlEscape("<a> &").c_str());
    std::string v;
    TEST_ASSERT_TRUE(xmlElement("<s:Body><u:X a=\"1\"><Q>v1</Q></u:X></s:Body>", "X", v));
    TEST_ASSERT_EQUAL_STRING("<Q>v1</Q>", v.c_str());
    TEST_ASSERT_TRUE(xmlElement("<a><Empty/><b>2</b></a>", "Empty", v));
    TEST_ASSERT_EQUAL_STRING("", v.c_str());
    TEST_ASSERT_FALSE(xmlElement("<a>open", "a", v));
    TEST_ASSERT_FALSE(xmlElement("<ab>1</ab>", "a", v));  // nom exact
    auto all = xmlElements("<l><i>1</i><i>2</i><j>x</j><i>3</i></l>", "i");
    TEST_ASSERT_EQUAL(3, (int)all.size());
    TEST_ASSERT_EQUAL_STRING("3", all[2].c_str());
}

static void test_upnp_description() {
    const std::string desc =
        "<?xml version=\"1.0\"?><root xmlns=\"urn:schemas-upnp-org:device-1-0\"><device><serviceList>"
        "<service><serviceType>urn:schemas-upnp-org:service:AVTransport:1</serviceType>"
        "<SCPDURL>/upnp/rendertransportSCPD.xml</SCPDURL><controlURL>/upnp/control/rendertransport1</controlURL></service>"
        "<service>\n  <serviceType>urn:schemas-wiimu-com:service:PlayQueue:1</serviceType>\n"
        "  <serviceId>urn:wiimu-com:serviceId:PlayQueue1</serviceId>\n  <SCPDURL>/upnp/PlayQueueSCPD.xml</SCPDURL>\n"
        "  <controlURL>/upnp/control/PlayQueue1</controlURL>\n</service></serviceList></device></root>";
    auto sv = parseUpnpServices(desc);
    TEST_ASSERT_EQUAL(2, (int)sv.size());
    TEST_ASSERT_EQUAL_STRING("urn:schemas-wiimu-com:service:PlayQueue:1", sv[1].type.c_str());
    TEST_ASSERT_EQUAL_STRING("/upnp/control/PlayQueue1", sv[1].controlUrl.c_str());
    TEST_ASSERT_EQUAL_STRING("/upnp/PlayQueueSCPD.xml", sv[1].scpdUrl.c_str());
    TEST_ASSERT_EQUAL_STRING("http://10.0.0.5:49152/upnp/x", upnpUrl("http://10.0.0.5:49152", "/upnp/x").c_str());
    TEST_ASSERT_EQUAL_STRING("http://10.0.0.5:49152/upnp/x", upnpUrl("http://10.0.0.5:49152", "upnp/x").c_str());
    TEST_ASSERT_EQUAL_STRING("http://h/y", upnpUrl("http://10.0.0.5:49152", "http://h/y").c_str());

    const std::string scpd =
        "<scpd><actionList><action><name>BrowseQueue</name><argumentList><argument><name>QueueName</name>"
        "<direction>in</direction></argument></argumentList></action><action><name>CreateQueue</name></action>"
        "</actionList></scpd>";
    auto acts = parseScpdActions(scpd);
    TEST_ASSERT_EQUAL(2, (int)acts.size());
    TEST_ASSERT_EQUAL_STRING("BrowseQueue", acts[0].c_str());
    TEST_ASSERT_EQUAL_STRING("CreateQueue", acts[1].c_str());
    auto args = parseScpdArguments(
        "<actionList><action><name>GetQueueIndex</name><argumentList><argument><name>QueueName</name>"
        "<direction>in</direction></argument><argument><name>CurrentIndex</name><direction>out</direction>"
        "</argument></argumentList></action></actionList>",
        "GetQueueIndex");
    TEST_ASSERT_EQUAL(2, (int)args.size());
    TEST_ASSERT_EQUAL_STRING("QueueName", args[0].c_str());
    TEST_ASSERT_EQUAL_STRING("->CurrentIndex", args[1].c_str());
    TEST_ASSERT_TRUE(parseScpdArguments(scpd, "Nope").empty());

    std::string body = soapBody("urn:schemas-wiimu-com:service:PlayQueue:1", "BrowseQueue", {{"QueueName", "A&B"}});
    TEST_ASSERT_TRUE(body.find("<u:BrowseQueue xmlns:u=\"urn:schemas-wiimu-com:service:PlayQueue:1\">") !=
                     std::string::npos);
    TEST_ASSERT_TRUE(body.find("<QueueName>A&amp;B</QueueName>") != std::string::npos);
}

static void test_upnp_queue() {
    // Réponse BrowseQueue : QueueContext échappé, métadonnées DIDL-Lite échappées deux fois
    const std::string soap =
        "<s:Envelope><s:Body><u:BrowseQueueResponse><QueueContext>&lt;?xml version=\"1.0\"?&gt;&lt;PlayList&gt;"
        "&lt;ListName&gt;Album Kind of Blue&lt;/ListName&gt;&lt;ListInfo&gt;&lt;SourceName&gt;Qobuz&lt;/SourceName&gt;"
        "&lt;requestQuality&gt;6&lt;/requestQuality&gt;&lt;TrackNumber&gt;2&lt;/TrackNumber&gt;"
        "&lt;SearchUrl&gt;https://www.qobuz.com/api.json/0.2/album/get?album_id=abc123&amp;amp;user_auth_token=SECRET"
        "&lt;/SearchUrl&gt;&lt;Login_username&gt;someone@example.com&lt;/Login_username&gt;&lt;/ListInfo&gt;"
        "&lt;Tracks&gt;&lt;Track1&gt;&lt;URL&gt;u1&lt;/URL&gt;&lt;Metadata&gt;&amp;lt;DIDL-Lite&amp;gt;&amp;lt;item&amp;gt;"
        "&amp;lt;song:albumid&amp;gt;y5ze2lnjhm3ze&amp;lt;/song:albumid&amp;gt;"
        "&amp;lt;dc:title&amp;gt;So What&amp;lt;/dc:title&amp;gt;&amp;lt;upnp:artist&amp;gt;Miles Davis&amp;lt;/upnp:artist&amp;gt;"
        "&amp;lt;upnp:album&amp;gt;Kind of Blue&amp;lt;/upnp:album&amp;gt;&amp;lt;upnp:albumArtURI&amp;gt;https://a/b.jpg"
        "&amp;lt;/upnp:albumArtURI&amp;gt;&amp;lt;/item&amp;gt;&amp;lt;/DIDL-Lite&amp;gt;"
        "&lt;/Metadata&gt;&lt;/Track1&gt;&lt;Track2&gt;&lt;URL&gt;u2&lt;/URL&gt;&lt;/Track2&gt;&lt;/Tracks&gt;"
        "&lt;/PlayList&gt;</QueueContext></u:BrowseQueueResponse></s:Body></s:Envelope>";
    std::string ctx;
    TEST_ASSERT_TRUE(xmlElement(soap, "QueueContext", ctx));
    QueueInfo q = parseQueueContext(xmlUnescape(ctx));
    TEST_ASSERT_TRUE(q.ok);
    TEST_ASSERT_EQUAL_STRING("Album Kind of Blue", q.listName.c_str());
    TEST_ASSERT_EQUAL_STRING("Qobuz", q.sourceName.c_str());
    TEST_ASSERT_EQUAL_STRING("6", q.quality.c_str());
    TEST_ASSERT_EQUAL(2, q.declaredTracks);
    TEST_ASSERT_EQUAL(2, q.tracks);
    TEST_ASSERT_EQUAL(1, (int)q.titles.size());
    TEST_ASSERT_EQUAL_STRING("So What", q.titles[0].c_str());
    TEST_ASSERT_EQUAL(2, (int)q.items.size());
    TEST_ASSERT_EQUAL_STRING("Miles Davis", q.items[0].artist.c_str());
    TEST_ASSERT_EQUAL_STRING("Kind of Blue", q.items[0].album.c_str());
    TEST_ASSERT_EQUAL_STRING("https://a/b.jpg", q.items[0].art.c_str());
    TEST_ASSERT_EQUAL_STRING("y5ze2lnjhm3ze", q.items[0].albumId.c_str());  // <song:albumid> du WiiM
    TEST_ASSERT_EQUAL_STRING("", q.items[1].albumId.c_str());
    TEST_ASSERT_EQUAL_STRING("", q.items[1].title.c_str());  // piste sans métadonnées
    TEST_ASSERT_EQUAL_STRING("someone@example.com", q.user.c_str());
    TEST_ASSERT_EQUAL_STRING("https://www.qobuz.com/api.json/0.2/album/get?album_id=abc123&user_auth_token=***",
                             maskSecrets(q.searchUrl).c_str());
    TEST_ASSERT_EQUAL_STRING("a=1&mytoken=x", maskSecrets("a=1&mytoken=x").c_str());  // pas un paramètre "token"
    TEST_ASSERT_EQUAL_STRING("{\"clientId\":\"***\",\"accessToken\" : \"***\",\"userName\":\"x\"}",
                             maskSecrets("{\"clientId\":\"123\",\"accessToken\" : \"a\\\"b\",\"userName\":\"x\"}").c_str());
    TEST_ASSERT_EQUAL_STRING("{\"token\":12}", maskSecrets("{\"token\":12}").c_str());  // pas une chaîne : inchangé
    TEST_ASSERT_EQUAL_STRING("x?app_id=***&uid=***&eid=***&hmac=***&album_id=42",
                             maskSecrets("x?app_id=123&uid=9&eid=abc&hmac=f00&album_id=42").c_str());
    TEST_ASSERT_EQUAL_STRING("so***@ex***", maskAccount(q.user).c_str());
    TEST_ASSERT_EQUAL_STRING("ar***", maskAccount("arthur").c_str());
    TEST_ASSERT_FALSE(parseQueueContext("<nothing/>").ok);
    // Flow Deezer (forme vue sur un WiiM, 2026-11-16) : file « station », image de la liste, une piste à la fois
    {
        const QueueInfo flow = parseQueueContext(
            "<?xml version=\"1.0\"?><PlayList><ListName>Flow</ListName><ListInfo><SourceName>Deezer2</SourceName>"
            "<SearchUrl>https://abc.airable.io/deezer/mix/XyZ</SearchUrl><PicUrl>http://assets.airable.io/p.png</PicUrl>"
            "<ContentType>station</ContentType><TrackNumber>1</TrackNumber></ListInfo><Tracks></Tracks></PlayList>");
        TEST_ASSERT_TRUE(isLiveQueue(flow));
        TEST_ASSERT_EQUAL_STRING("http://assets.airable.io/p.png", flow.picUrl.c_str());
        QueueInfo mix;  // sans ContentType : reconnue par l'adresse
        mix.searchUrl = "https://abc.airable.io/deezer/mix/XyZ";
        TEST_ASSERT_TRUE(isLiveQueue(mix));
        QueueInfo album;
        album.searchUrl = "https://abc.airable.io/deezer/album/123";
        TEST_ASSERT_FALSE(isLiveQueue(album));
        album.searchUrl = "https://www.qobuz.com/api.json/0.2/track/search?query=/mix/";
        TEST_ASSERT_FALSE(isLiveQueue(album));  // « /mix/ » dans la requête, pas dans le chemin : pas une file vivante
    }
}

static void test_favorites() {
    QueueInfo q;
    q.searchUrl = "https://www.qobuz.com/api.json/0.2/album/get?album_id=mlw3bkxusda&offset=0&limit=50";
    TEST_ASSERT_EQUAL_STRING("https://www.qobuz.com/api.json/0.2/album/get?album_id=mlw3bkxusda", queueKey(q).c_str());
    q.searchUrl = "https://x/playlist/get?limit=50&playlist_id=7&extra=tracks";
    TEST_ASSERT_EQUAL_STRING("https://x/playlist/get?playlist_id=7&extra=tracks", queueKey(q).c_str());
    QueueInfo r;
    r.sourceName = "OnlineMusic";
    r.listName = "custom_radio";
    TEST_ASSERT_EQUAL_STRING("OnlineMusic|custom_radio", queueKey(r).c_str());

    std::vector<Favorite> list = {{"f1", "k1", "Babylon (Music from\tthe Motion Picture)", "Justin Hurwitz", "Qobuz",
                                   "https://static.qobuz.com/a.jpg"},
                                  {"f2", "k2", "Kind of Blue", "", "Qobuz", ""}};
    std::string text = favoritesToText(list);
    auto back = favoritesFromText(text + "broken line\n\n");
    TEST_ASSERT_EQUAL(2, (int)back.size());
    TEST_ASSERT_EQUAL_STRING("Babylon (Music from the Motion Picture)", back[0].title.c_str());  // tabulation retirée
    TEST_ASSERT_EQUAL_STRING("Justin Hurwitz", back[0].artist.c_str());
    TEST_ASSERT_EQUAL_STRING("https://static.qobuz.com/a.jpg", back[0].artUrl.c_str());
    TEST_ASSERT_EQUAL_STRING("", back[1].artist.c_str());
    TEST_ASSERT_EQUAL_STRING("k2", back[1].key.c_str());
}

static void test_queue_template() {
    const std::string templ =
        "<?xml version=\"1.0\"?><PlayList><ListName>Babylon</ListName><ListInfo><Radio>0</Radio>"
        "<SourceName>Qobuz</SourceName><TrackNumber>40</TrackNumber><requestQuality>6</requestQuality>"
        "<SearchUrl>https://www.qobuz.com/api.json/0.2/album/get?album_id=a1&amp;offset=0&amp;limit=50</SearchUrl>"
        "<Login_username>someone@example.com</Login_username></ListInfo><Tracks><Track1><URL>u</URL></Track1>"
        "</Tracks></PlayList>";
    std::string q = queueFromTemplate(templ, "wiimscreen-recherche",
                                      "https://www.qobuz.com/api.json/0.2/track/search?query=miles%20davis&limit=50");
    QueueInfo i = parseQueueContext(q);
    TEST_ASSERT_TRUE(i.ok);
    TEST_ASSERT_EQUAL_STRING("wiimscreen-recherche", i.listName.c_str());
    TEST_ASSERT_EQUAL_STRING("Qobuz", i.sourceName.c_str());
    TEST_ASSERT_EQUAL_STRING("https://www.qobuz.com/api.json/0.2/track/search?query=miles%20davis&limit=50",
                             i.searchUrl.c_str());
    TEST_ASSERT_EQUAL_STRING("someone@example.com", i.user.c_str());  // compte et qualité gardés
    TEST_ASSERT_EQUAL_STRING("6", i.quality.c_str());
    TEST_ASSERT_EQUAL(0, i.tracks);                // pistes retirées
    TEST_ASSERT_EQUAL(-1, i.declaredTracks);       // TrackNumber retiré
    TEST_ASSERT_TRUE(q.find("&amp;limit=50") != std::string::npos || q.find("limit=50") != std::string::npos);
    TEST_ASSERT_EQUAL_STRING("", queueFromTemplate("<nothing/>", "x", "y").c_str());
    TEST_ASSERT_EQUAL_STRING("Miles%20Davis%20%C3%A9", urlQueryEncode("Miles Davis \xC3\xA9").c_str());
}

static void test_keyboard() {
    // Langue : QWERTY et textes anglais par défaut, AZERTY et textes français en français ; mêmes touches
    TEST_ASSERT_TRUE(lang() == Lang::En);
    {
        auto en = keyboardLayout(Rect{100, 200, 1160, 500});
        TEST_ASSERT_EQUAL_STRING("q", en[10].text.c_str());
        TEST_ASSERT_EQUAL_STRING("Search", en.back().label.c_str());
        setLang(Lang::Fr);
        auto fr = keyboardLayout(Rect{100, 200, 1160, 500});
        TEST_ASSERT_EQUAL_STRING("a", fr[10].text.c_str());
        TEST_ASSERT_EQUAL_STRING("Chercher", fr.back().label.c_str());
        TEST_ASSERT_EQUAL((int)en.size(), (int)fr.size());
        TEST_ASSERT_EQUAL_STRING("Effacer", L("Clear", "Effacer"));
        setLang(Lang::En);
        TEST_ASSERT_EQUAL_STRING("Clear", L("Clear", "Effacer"));
        TEST_ASSERT_TRUE(langFromCode("fr") == Lang::Fr && langFromCode("de") == Lang::En && langFromCode("") == Lang::En);
        TEST_ASSERT_EQUAL_STRING("fr", langCode(Lang::Fr));
        // Lettres : chacune une seule fois dans les deux dispositions
        for (auto* k : {&en, &fr}) {
            std::string letters;
            for (auto& key : *k)
                if (key.kind == Key::Char && key.text.size() == 1 && key.text[0] >= 'a' && key.text[0] <= 'z') letters += key.text;
            std::sort(letters.begin(), letters.end());
            TEST_ASSERT_EQUAL_STRING("abcdefghijklmnopqrstuvwxyz", letters.c_str());
        }
    }
    for (Lang lg : {Lang::En, Lang::Fr})
    for (const Rect& area : {Rect{100, 200, 1160, 500}, Rect{20, 600, 680, 640}, Rect{80, 150, 860, 380}}) {
        setLang(lg);
        auto keys = keyboardLayout(area);
        TEST_ASSERT_EQUAL(10 + 10 + 10 + 9 + 3, (int)keys.size());
        int enter = 0, back = 0;
        for (size_t i = 0; i < keys.size(); ++i) {
            const Rect& r = keys[i].r;
            TEST_ASSERT_TRUE(r.x >= area.x && r.y >= area.y && r.right() <= area.right() && r.bottom() <= area.bottom());
            TEST_ASSERT_TRUE(r.w >= 44 && r.h >= 44);  // cible tactile
            for (size_t j = i + 1; j < keys.size(); ++j) TEST_ASSERT_FALSE(r.intersects(keys[j].r));
            enter += keys[i].kind == Key::Enter;
            back += keys[i].kind == Key::Back;
        }
        TEST_ASSERT_EQUAL(1, enter);
        TEST_ASSERT_EQUAL(1, back);
    }
    setLang(Lang::En);
    TEST_ASSERT_EQUAL_STRING("caf", utf8PopBack("caf\xC3\xA9").c_str());  // « é » : deux octets retirés
    TEST_ASSERT_EQUAL_STRING("ab", utf8PopBack("abc").c_str());
    TEST_ASSERT_EQUAL_STRING("", utf8PopBack("").c_str());
}

static void test_shares_word() {
    TEST_ASSERT_TRUE(sharesWord("aretha frank", {"Respect", "Aretha Franklin", "Lady Soul"}));
    TEST_ASSERT_TRUE(sharesWord("Beyonc\xC3\xA9", {"BEYONCE"}));  // accents et casse ignorés
    TEST_ASSERT_FALSE(sharesWord("justin hurwitz", {"Respect", "Aretha Franklin"}));
    TEST_ASSERT_FALSE(sharesWord("a b", {"a b c"}));  // mots trop courts : pas de preuve
}

static void test_deezer() {
    const std::string json =
        "{\"data\":[{\"id\":3135556,\"readable\":true,\"title\":\"Harder, Better, Faster, Stronger\","
        "\"duration\":224,\"artist\":{\"id\":27,\"name\":\"Daft Punk\",\"type\":\"artist\"},"
        "\"album\":{\"id\":302127,\"title\":\"Discovery\",\"cover_medium\":\"https:\\/\\/e-cdns.dzcdn.net\\/a.jpg\","
        "\"type\":\"album\"},\"type\":\"track\"},{\"id\":0,\"title\":\"\"}],\"total\":2,\"next\":\"x\"}";
    auto t = parseDeezerTracks(json);
    TEST_ASSERT_EQUAL(1, (int)t.size());  // l'objet sans id est ignoré
    TEST_ASSERT_EQUAL(3135556, (int)t[0].id);
    TEST_ASSERT_EQUAL(302127, (int)t[0].albumId);
    TEST_ASSERT_EQUAL_STRING("Harder, Better, Faster, Stronger", t[0].title.c_str());
    TEST_ASSERT_EQUAL_STRING("Daft Punk", t[0].artist.c_str());
    TEST_ASSERT_EQUAL_STRING("Discovery", t[0].album.c_str());
    TEST_ASSERT_TRUE(parseDeezerTracks("{\"error\":{}}").empty());
    TEST_ASSERT_EQUAL_STRING("https://123.airable.io", urlOrigin("https://123.airable.io/deezer/album/1137050721").c_str());
    TEST_ASSERT_EQUAL_STRING("http://h:49152", urlOrigin("http://h:49152?x").c_str());
    TEST_ASSERT_EQUAL_STRING("", urlOrigin("ftp://h/x").c_str());
}

static void test_qobuz_json() {
    // Clés imbriquées : le « title » et le « name » du bon niveau
    const std::string tracks =
        "{\"query\":\"daft\",\"tracks\":{\"limit\":2,\"items\":[{\"maximum_bit_depth\":16,"
        "\"composer\":{\"name\":\"Thomas Bangalter\",\"id\":1},\"album\":{\"image\":{\"small\":\"https:\\/\\/static.qobuz.com\\/a_230.jpg\","
        "\"large\":\"x\"},\"artist\":{\"name\":\"Daft Punk\"},\"title\":\"Discovery\",\"id\":\"0724384960650\","
        "\"label\":{\"name\":\"Parlophone\"}},\"performer\":{\"name\":\"Daft Punk\",\"id\":36819},"
        "\"title\":\"One More Time\",\"version\":null,\"id\":3135556},"
        "{\"title\":\"Digital Love\",\"version\":\"Remastered\",\"id\":3135557,\"album\":{\"id\":\"0724384960650\","
        "\"title\":\"Discovery\",\"artist\":{\"name\":\"Daft Punk\"}}}]}}";
    auto t = parseQobuzTracks(tracks);
    TEST_ASSERT_EQUAL(2, (int)t.size());
    TEST_ASSERT_EQUAL_STRING("One More Time", t[0].title.c_str());
    TEST_ASSERT_EQUAL_STRING("3135556", t[0].id.c_str());
    TEST_ASSERT_EQUAL_STRING("Daft Punk", t[0].artist.c_str());
    TEST_ASSERT_EQUAL_STRING("0724384960650", t[0].albumId.c_str());
    TEST_ASSERT_EQUAL_STRING("Discovery", t[0].album.c_str());
    TEST_ASSERT_EQUAL_STRING("https://static.qobuz.com/a_230.jpg", t[0].image.c_str());
    TEST_ASSERT_EQUAL_STRING("Digital Love (Remastered)", t[1].title.c_str());
    TEST_ASSERT_EQUAL_STRING("Daft Punk", t[1].artist.c_str());  // pas de performer : artiste de l'album

    const std::string albums =
        "{\"albums\":{\"total\":1,\"items\":[{\"image\":{\"small\":\"https:\\/\\/s\\/b.jpg\"},\"artist\":{\"name\":"
        "\"Justin Hurwitz\",\"id\":5},\"genre\":{\"name\":\"Bandes originales\"},\"title\":\"Babylon (Music from the "
        "Motion Picture)\",\"tracks_count\":48,\"id\":\"mlw3bkxusda\"}]}}";
    auto a = parseQobuzAlbums(albums);
    TEST_ASSERT_EQUAL(1, (int)a.size());
    TEST_ASSERT_EQUAL_STRING("mlw3bkxusda", a[0].id.c_str());
    TEST_ASSERT_EQUAL_STRING("Babylon (Music from the Motion Picture)", a[0].title.c_str());
    TEST_ASSERT_EQUAL_STRING("Justin Hurwitz", a[0].artist.c_str());
    TEST_ASSERT_EQUAL_STRING("https://s/b.jpg", a[0].image.c_str());
    TEST_ASSERT_EQUAL(48, a[0].tracks);
    TEST_ASSERT_TRUE(parseQobuzAlbums("{\"status\":\"error\"}").empty());
    TEST_ASSERT_EQUAL_STRING("\xC3\xA9t\xC3\xA9", jsonText("\"\\u00e9t\\u00e9\"").c_str());

    auto dz = parseDeezerAlbums(
        "{\"data\":[{\"id\":302127,\"title\":\"Discovery\",\"cover_medium\":\"https:\\/\\/e-cdns\\/c.jpg\","
        "\"nb_tracks\":14,\"artist\":{\"id\":27,\"name\":\"Daft Punk\"},\"type\":\"album\"}],\"total\":1}");
    TEST_ASSERT_EQUAL(1, (int)dz.size());
    TEST_ASSERT_EQUAL_STRING("302127", dz[0].id.c_str());
    TEST_ASSERT_EQUAL_STRING("Daft Punk", dz[0].artist.c_str());
    TEST_ASSERT_EQUAL_STRING("https://e-cdns/c.jpg", dz[0].image.c_str());
    TEST_ASSERT_EQUAL(14, dz[0].tracks);
    auto names = jsonValuesOf("[{\"source\":\"Qobuz\",\"showName\":\"Qobuz\"},{\"showName\":\"Deezer\"}]", "showName");
    TEST_ASSERT_EQUAL(2, (int)names.size());
    TEST_ASSERT_EQUAL_STRING("Deezer", names[1].c_str());
    TEST_ASSERT_EQUAL_STRING("https://api.deezer.com/user/me/albums?access_token=***&limit=50",
                             maskSecrets("https://api.deezer.com/user/me/albums?access_token=SECRET&limit=50").c_str());

    QobuzAccess acc;
    TEST_ASSERT_TRUE(parseQobuzAccess("{\"userName\":\"x\",\"clientId\":\"123456789\",\"accessToken\":\"abc-DEF_9\"}", acc));
    TEST_ASSERT_EQUAL_STRING("123456789", acc.appId.c_str());
    TEST_ASSERT_EQUAL_STRING("x", acc.user.c_str());
    // Modèle de file Qobuz construit depuis le nom du compte : utilisable par queueFromTemplate
    const std::string model = qobuzQueueModel("a&b@example.org");
    TEST_ASSERT_TRUE(model.find("<Login_username>a&amp;b@example.org</Login_username>") != std::string::npos);
    const std::string q = queueFromTemplate(model, "wiimscreen-1", "https://www.qobuz.com/api.json/0.2/album/get?album_id=7");
    const QueueInfo qi = parseQueueContext(q);
    TEST_ASSERT_EQUAL_STRING("Qobuz", qi.sourceName.c_str());
    TEST_ASSERT_EQUAL_STRING("wiimscreen-1", qi.listName.c_str());
    TEST_ASSERT_EQUAL_STRING("https://www.qobuz.com/api.json/0.2/album/get?album_id=7", qi.searchUrl.c_str());
    TEST_ASSERT_TRUE(qobuzQueueModel("").empty());
    // Pochettes Deezer : variante JPEG de base (le décodeur ne lit pas les JPEG progressifs)
    TEST_ASSERT_EQUAL_STRING("https://cdn-images.dzcdn.net/images/cover/93f1af/250x250-000000-80-0-1.jpg",
                             baselineCoverUrl("https://cdn-images.dzcdn.net/images/cover/93f1af/250x250-000000-80-0-0.jpg").c_str());
    TEST_ASSERT_EQUAL_STRING("https://e-cdns-images.dzcdn.net/images/artist/ab/56x56-000000-80-0-1.jpg",
                             baselineCoverUrl("https://e-cdns-images.dzcdn.net/images/artist/ab/56x56-000000-80-0-0.jpg").c_str());
    const char* same[] = {"https://static.qobuz.com/images/covers/x_230.jpg",
                          "https://cdn-images.dzcdn.net/images/cover/93f1af/250x250.jpg",
                          "https://cdn-images.dzcdn.net/images/cover/93f1af/250x250-000000-80-0-0.png",
                          "https://cdn-images.dzcdn.net/images/cover/93f1af/250x250-000000-80-0-0.jpg?x=1",
                          "https://evil.example/cdn-images.dzcdn.net/250x250-000000-80-0-0.jpg"};
    for (const char* u : same) TEST_ASSERT_EQUAL_STRING(u, baselineCoverUrl(u).c_str());
    // Album Qobuz : adresse de la file et clé de favori (sans offset/limit, comme une file copiée du WiiM)
    TEST_ASSERT_EQUAL_STRING("https://www.qobuz.com/api.json/0.2/album/get?album_id=0886443927087&offset=0&limit=50",
                             qobuzAlbumUrl("0886443927087").c_str());
    QueueInfo copied;
    copied.searchUrl = "https://www.qobuz.com/api.json/0.2/album/get?album_id=0886443927087&offset=0&limit=100";
    TEST_ASSERT_EQUAL_STRING(queueKey(copied).c_str(), qobuzAlbumKey("0886443927087").c_str());
    TEST_ASSERT_TRUE(qobuzAlbumKey("a") != qobuzAlbumKey("b"));
    TEST_ASSERT_TRUE(qobuzAlbumUrl("a&b").find("album_id=a%26b&") != std::string::npos);
    TEST_ASSERT_FALSE(parseQobuzAccess("{\"clientId\":\"1\",\"accessToken\":\"bad\\r\\ntoken\"}", acc));  // en-tête HTTP
    TEST_ASSERT_FALSE(parseQobuzAccess("{\"clientId\":\"1\"}", acc));
}

static void test_fields() {
    std::vector<std::pair<std::string, std::string>> f = {
        {"ssid", "Livebox-A3F2"}, {"pass", "p\\a\tss\nword"}, {"presets", "1=FIP\n2=Nova"}, {"empty", ""}};
    auto back = decodeFields(encodeFields(f) + "broken line\n");
    TEST_ASSERT_EQUAL(4, (int)back.size());
    TEST_ASSERT_EQUAL_STRING("p\\a\tss\nword", back[1].second.c_str());  // échappements gardés
    TEST_ASSERT_EQUAL_STRING("1=FIP\n2=Nova", back[2].second.c_str());
    TEST_ASSERT_EQUAL_STRING("empty", back[3].first.c_str());
    TEST_ASSERT_EQUAL_STRING("", back[3].second.c_str());
    TEST_ASSERT_TRUE(decodeFields("").empty());
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
    RUN_TEST(test_upnp_xml);
    RUN_TEST(test_upnp_description);
    RUN_TEST(test_upnp_queue);
    RUN_TEST(test_favorites);
    RUN_TEST(test_queue_template);
    RUN_TEST(test_keyboard);
    RUN_TEST(test_shares_word);
    RUN_TEST(test_deezer);
    RUN_TEST(test_qobuz_json);
    RUN_TEST(test_fields);
    RUN_TEST(test_discovery);
    RUN_TEST(test_wiim_doc_examples);
    RUN_TEST(test_urls_and_images);
    RUN_TEST(test_json_escapes);
    RUN_TEST(test_http_response);
    RUN_TEST(test_decisions);
    RUN_TEST(test_json_unicode);
    RUN_TEST(test_fingerprint);
    RUN_TEST(test_hosts);
    RUN_TEST(test_html_escape);
    RUN_TEST(test_wrap);
    RUN_TEST(test_layout);
    RUN_TEST(test_accent);
    RUN_TEST(test_spectrum);
    RUN_TEST(test_motion);
    RUN_TEST(test_voice_text);
    RUN_TEST(test_voice_audio);
    RUN_TEST(test_deezer_user);
    RUN_TEST(test_firmware_stream);
    return UNITY_END();
}
