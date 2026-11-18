// Tests: WiiM HTTP API responses: status, metadata, presets, discovery, formatting (pio test -e native)
#include <unity.h>

#include <string>
#include <vector>
#include <algorithm>

#include "core.h"
#include "test_helpers.h"

using namespace wr;

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

static void test_quality() {
    TEST_ASSERT_EQUAL_STRING("16 bits \xC2\xB7 44,1 kHz", formatQuality(44100, 16).c_str());
    TEST_ASSERT_EQUAL_STRING("24 bits \xC2\xB7 96 kHz", formatQuality(96000, 24).c_str());
    TEST_ASSERT_EQUAL_STRING("48 kHz", formatQuality(48000, 0).c_str());
    TEST_ASSERT_EQUAL_STRING("24 bits", formatQuality(0, 24).c_str());
    TEST_ASSERT_EQUAL_STRING("", formatQuality(0, 0).c_str());
    TEST_ASSERT_EQUAL_STRING("88,2 kHz", formatQuality(88200, 0).c_str());
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

static void test_hex_only_for_status() {
    // Noms en texte ordinaire : « 2021 » n'est pas de l'hexadécimal (devenait « ! »)
    WiimIdentity w = parseStatusEx("{\"uuid\":\"u\",\"DeviceName\":\"2021\"}");
    TEST_ASSERT_EQUAL_STRING("2021", w.name.c_str());
    w = parseStatusEx("{\"uuid\":\"u\",\"DeviceName\":\" \",\"ssid\":\"4142\"}");
    TEST_ASSERT_EQUAL_STRING("4142", w.name.c_str());
    std::vector<Preset> p;
    TEST_ASSERT_TRUE(parsePresetInfo("{\"preset_list\":[{\"number\":1,\"name\":\"2021\"}]}", p));
    TEST_ASSERT_EQUAL_STRING("2021", p[0].label.c_str());
    // getPlayerStatus : toujours décodé
    TEST_ASSERT_EQUAL_STRING("AB", parsePlayerStatus("{\"status\":\"play\",\"Title\":\"4142\"}").title.c_str());
}

void run_wiim_api_tests() {
    RUN_TEST(test_hex_text);
    RUN_TEST(test_player_status);
    RUN_TEST(test_meta_info);
    RUN_TEST(test_quality);
    RUN_TEST(test_discovery);
    RUN_TEST(test_wiim_doc_examples);
    RUN_TEST(test_hex_only_for_status);
}
