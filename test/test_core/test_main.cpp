// Tests de la logique pure : pio test -e native
#include <unity.h>

#include <string>

#include "core.h"

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

static void test_layout() {
    Layout L = computeLayout(960, 540, 3, 6);
    TEST_ASSERT_EQUAL(6, (int)L.presets.size());
    // Pas de chevauchement entre la grille, l'en-tête et la barre de transport
    TEST_ASSERT_TRUE(L.presets[0].y >= L.header.y + L.header.h);
    TEST_ASSERT_TRUE(L.presets[5].y + L.presets[5].h <= L.transport[0].y);
    TEST_ASSERT_TRUE(L.transport[kVolUp].x + L.transport[kVolUp].w <= 960);
    TEST_ASSERT_TRUE(L.presets[2].x + L.presets[2].w <= 960);
    TEST_ASSERT_TRUE(L.presets[4].contains(L.presets[4].x + 1, L.presets[4].y + 1));
    TEST_ASSERT_FALSE(L.presets[4].contains(L.presets[5].x + 1, L.presets[5].y + 1));
    // Portrait, 2 colonnes, 5 presets
    L = computeLayout(540, 960, 2, 5);
    TEST_ASSERT_EQUAL(5, (int)L.presets.size());
    TEST_ASSERT_TRUE(L.presets[4].y + L.presets[4].h <= L.transport[0].y);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_presets_ok);
    RUN_TEST(test_presets_errors);
    RUN_TEST(test_hex_text);
    RUN_TEST(test_player_status);
    RUN_TEST(test_json_escapes);
    RUN_TEST(test_http_response);
    RUN_TEST(test_fingerprint);
    RUN_TEST(test_hosts);
    RUN_TEST(test_html_escape);
    RUN_TEST(test_wrap);
    RUN_TEST(test_layout);
    return UNITY_END();
}
