// Tests: Every parser on truncated or absurd input (pio test -e native)
#include <unity.h>

#include <string>
#include <vector>
#include <algorithm>
#include <chrono>

#include "core.h"
#include "upnp.h"
#include "qobuz.h"
#include "voice.h"
#include "decisions.h"
#include "test_helpers.h"

using namespace wr;

// Toutes les fonctions d'analyse sur des entrées tronquées ou absurdes : elles doivent seulement rendre la main
// (avec ASan/UBSan dans [env:native], cela garde contre les plantages et les boucles sans fin)
static size_t g_sink = 0;

// Réglages décrits par un tableau (lib/core/src/fields.h) : copie de secours relue depuis n'importe quel texte
struct RobustSettings {
    std::string text;
    bool flag = true;
    int number = 0;
};
using RobustField = SettingField<RobustSettings, std::string>;
static constexpr RobustField kRobust[] = {
    RobustField::text("t", &RobustSettings::text, true),
    RobustField::flag("f", &RobustSettings::flag),
    RobustField::number("n", &RobustSettings::number, -9, 9),
};

static void feedAll(const std::string& s) {
    std::vector<Preset> presets;
    std::string err, v, ip;
    HttpResponse r;
    QobuzAccess acc;
    long n = 0;
    uint8_t fp[32];
    uint16_t port = 0;
    g_sink += parsePresets(s, presets, err);
    g_sink += decodeHexText(s).size();
    g_sink += parsePlayerStatus(s).title.size();
    g_sink += parseMetaInfo(s).artUrl.size();
    g_sink += parsePresetInfo(s, presets);
    g_sink += parseStatusEx(s).name.size();
    g_sink += parseHttpResponse(s, r) + r.body.size();
    const HttpFrame f = httpFrame(s);
    g_sink += f.complete + f.reusable;
    g_sink += jsonString(s, "a", v) + jsonInt(s, "a", n);
    g_sink += ssdpHost(s, ip) + isHttpUrl(s) + isValidHost(s) + parseFingerprint(s, fp);
    std::string name;
    splitHostPort(s, name, port, 1);
    g_sink += baselineCoverUrl(s).size() + htmlEscape(s).size();
    g_sink += wrapText(s, 50, 2, fakeMeasure).size();
    g_sink += tileStatsFromText(s).uses.size() + shortTitle(s).size();
    g_sink += decodeFields(s).size() + utf8PopBack(s).size();
    const QueueInfo q = parseQueueContext(s, kQueueTitlesMax);
    g_sink += q.items.size() + queueKey(q).size() + isLiveQueue(q) + queueIndexOf(q, "a", true);
    g_sink += xmlElement(s, "a", v) + xmlElements(s, "Track1").size() + xmlUnescape(s).size();
    g_sink += parseUpnpServices(s).size() + parseScpdActions(s).size() + parseScpdArguments(s, "a").size();
    g_sink += queueFromTemplate(s, "n", "u").size() + maskSecrets(s).size() + favoritesFromText(s).size();
    g_sink += urlOrigin(s).size() + urlQueryEncode(s).size() + maskAccount(s).size();
    g_sink += parseQobuzTracks(s).size() + parseQobuzAlbums(s).size() + parseQobuzAccess(s, acc);
    g_sink += parseDeezerTracks(s).size() + parseDeezerAlbums(s).size() + jsonValuesOf(s, "a").size();
    g_sink += jsonTop(s, "a").size() + jsonText(s).size() + jsonArray(s).size();
    g_sink += normalizeText(s).size() + sharesWord(s, {s, "abc"}) + deezerUserId(s).size();
    g_sink += cleanTranscript(s).size() + typeText(s, s).size();
    SearchUndo undo;
    g_sink += parseSearchUndo(s, undo) + searchUndoText(undo).size();
    RobustSettings rs;
    decodeSettings(s, rs, RobustSettings(), kRobust);
    g_sink += rs.text.size() + rs.flag + rs.number + parseFlag(s.c_str(), false) + parseNumber(s.c_str(), -5, 5);
}

static void test_parsers_robust() {
    const auto t0 = std::chrono::steady_clock::now();
    const std::string samples[] = {
        "{\"status\":\"play\",\"Title\":\"4142\",\"vol\":\"42\",\"curpos\":\"1\",\"totlen\":\"9\"}",
        "{\"metaData\":{\"title\":\"So \\u00e9 \\ud83d\\ude00\",\"albumArtURI \":\"https://a/b\",\"sampleRate\":\"44100\"}}",
        "{\"preset_list\":[{\"number\":1,\"name\":\"FIP\",\"picurl\":\"http://p\"},{\"number\":2}]}",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: keep-alive\r\n\r\n4;e\r\nWiki\r\n0\r\n\r\n",
        "HTTP/1.0 200 OK\r\nContent-Length: 2\r\n\r\nOK",
        "<?xml version=\"1.0\"?><PlayList><ListName>L &amp; M</ListName><ListInfo><SourceName>Qobuz</SourceName>"
        "<SearchUrl>https://q/a?x=1&amp;user_auth_token=S</SearchUrl><TrackNumber>2</TrackNumber></ListInfo><Tracks>"
        "<Track1><Metadata>&lt;DIDL-Lite&gt;&lt;dc:title&gt;T &amp;amp; U&lt;/dc:title&gt;&lt;/DIDL-Lite&gt;</Metadata>"
        "</Track1><Track2/></Tracks></PlayList>",
        "<root><service><serviceType>urn:x</serviceType><controlURL>/c</controlURL></service></root>",
        "{\"tracks\":{\"items\":[{\"id\":1,\"title\":\"t\",\"album\":{\"id\":\"a\",\"image\":{\"small\":\"s\"}},"
        "\"performer\":{\"name\":\"p\"}}]},\"albums\":{\"items\":[{\"id\":\"x\",\"title\":\"y\"}]}}",
        "{\"data\":[{\"id\":3135556,\"title\":\"H\",\"artist\":{\"name\":\"D\"},\"album\":{\"id\":3,\"title\":\"x\"}}]}",
        "{\"clientId\":\"1\",\"accessToken\":\"t\",\"userName\":\"u\"}",
        "f1\tk\tTitle\tArtist\tQobuz\thttps://a\nssid\tLive\\tbox\n1=FIP\n# c\n",
        "https://www.deezer.com/fr/profile/123456 x?token=a&sig=b \xC3\xA9\xE2\x80\xA6\xF0\x9F\x8E\xB5",
    };
    for (const std::string& s : samples)
        for (size_t k = 0; k <= s.size(); ++k) feedAll(s.substr(0, k));
    for (char c : std::string("<&{[}]\"\\:;%\r\n\t\x80\xC3")) {
        feedAll(std::string(1, c));
        feedAll(std::string(3000, c));
    }
    std::string mix;
    for (int i = 0; i < 500; ++i) mix += "<{[\"\\&#x;}]>%3D=\r\n0\r\n";
    feedAll(mix);
    feedAll("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n" + std::string(3000, 'f'));
    feedAll("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n" + std::string(3000, '0'));
    TEST_ASSERT_TRUE(g_sink > 0);
    TEST_ASSERT_TRUE(secondsSince(t0) < 60.0);  // marge pour une machine chargée (ASan)
}

void run_robust_tests() {
    RUN_TEST(test_parsers_robust);
}
