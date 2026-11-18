// Tests: UPnP / XML / SOAP, PlayQueue parsing, favourites text, secret masking (pio test -e native)
#include <unity.h>

#include <string>
#include <vector>
#include <algorithm>
#include <chrono>

#include "upnp.h"
#include "test_helpers.h"

using namespace wr;

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
    // Valeur numérique : masquée aussi désormais (avant : inchangée)
    TEST_ASSERT_EQUAL_STRING("{\"token\":***}", maskSecrets("{\"token\":12}").c_str());
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

static void test_xml_hostile_speed() {
    // 300 Ko de '<' ou de '&' : temps linéaire (200 Ko de '<' prenaient 52 s)
    const std::string lt(300000, '<'), amp(300000, '&');
    std::string mixed;
    for (int i = 0; i < 30000; ++i) mixed += "<a&b;<";
    const auto t0 = std::chrono::steady_clock::now();
    std::string v;
    for (const std::string* s : std::initializer_list<const std::string*>{&lt, &amp, &mixed}) {
        xmlElement(*s, "Track1", v);
        xmlElements(*s, "a");
        parseQueueContext(*s);
        TEST_ASSERT_TRUE(xmlUnescape(*s).size() <= s->size());
    }
    TEST_ASSERT_TRUE(secondsSince(t0) < 3.0);
    // Noms longs : toujours trouvés jusqu'à 63 caractères, ignorés au-delà
    const std::string name(60, 'n');
    TEST_ASSERT_TRUE(xmlElement("<" + name + ">x</" + name + ">", name, v));
    TEST_ASSERT_EQUAL_STRING("x", v.c_str());
}

static void test_mask_more() {
    TEST_ASSERT_EQUAL_STRING("{\"app_id\": ***}", maskSecrets("{\"app_id\": 123456789}").c_str());
    TEST_ASSERT_EQUAL_STRING("<accessToken>***</accessToken>", maskSecrets("<accessToken>SECRET</accessToken>").c_str());
    TEST_ASSERT_EQUAL_STRING("<u:accessToken>***</u:accessToken>", maskSecrets("<u:accessToken>SECRET</u:accessToken>").c_str());
    TEST_ASSERT_EQUAL_STRING("&lt;token&gt;***&lt;/token&gt;", maskSecrets("&lt;token&gt;SECRET&lt;/token&gt;").c_str());
    TEST_ASSERT_TRUE(maskSecrets("f(token=SECRET)").find("SECRET") == std::string::npos);
    TEST_ASSERT_TRUE(maskSecrets("'token=SECRET'").find("SECRET") == std::string::npos);
    TEST_ASSERT_TRUE(maskSecrets("\"token=SECRET\"").find("SECRET") == std::string::npos);
    TEST_ASSERT_EQUAL_STRING("a\ttoken=***\tb", maskSecrets("a\ttoken=SECRET\tb").c_str());
    TEST_ASSERT_EQUAL_STRING("u=x%3Fuser_auth_token%3D***%26b%3D1",
                             maskSecrets("u=x%3Fuser_auth_token%3DSECRET%26b%3D1").c_str());
    TEST_ASSERT_EQUAL_STRING("q%26token%3d***", maskSecrets("q%26token%3dSECRET").c_str());
    TEST_ASSERT_EQUAL_STRING("{\"accessToken\":\n  \"***\"}", maskSecrets("{\"accessToken\":\n  \"SECRET\"}").c_str());
    TEST_ASSERT_EQUAL_STRING("{\\\"accessToken\\\":\\\"***\\\"}", maskSecrets("{\\\"accessToken\\\":\\\"SECRET\\\"}").c_str());
    TEST_ASSERT_EQUAL_STRING("{&quot;clientId&quot;:&quot;***&quot;}", maskSecrets("{&quot;clientId&quot;:&quot;123&quot;}").c_str());
    // Inchangés : autre nom, valeur qui est un nom de clé, objet
    TEST_ASSERT_EQUAL_STRING("mytoken=x&my_sig=y", maskSecrets("mytoken=x&my_sig=y").c_str());
    TEST_ASSERT_EQUAL_STRING("{\"name\":\"token\",\"n\":1}", maskSecrets("{\"name\":\"token\",\"n\":1}").c_str());
    TEST_ASSERT_EQUAL_STRING("{\"token\":{\"a\":1}}", maskSecrets("{\"token\":{\"a\":1}}").c_str());
    TEST_ASSERT_EQUAL_STRING("token", maskSecrets("token").c_str());
}

static void test_template_empty_element() {
    // <SearchUrl/> : remplacé (avant : gardé devant le nouveau et lu en premier, adresse vide)
    const std::string t = "<PlayList><ListName/><ListInfo><SourceName>Qobuz</SourceName><SearchUrl/></ListInfo>"
                          "<Tracks/></PlayList>";
    QueueInfo q = parseQueueContext(queueFromTemplate(t, "n", "https://x/y?a=1&b=2"));
    TEST_ASSERT_EQUAL_STRING("n", q.listName.c_str());
    TEST_ASSERT_EQUAL_STRING("https://x/y?a=1&b=2", q.searchUrl.c_str());
    q = parseQueueContext(queueFromTemplate("<PlayList><ListInfo/></PlayList>", "n", "https://x/z"));
    TEST_ASSERT_EQUAL_STRING("https://x/z", q.searchUrl.c_str());
}

// maskSecrets skips space, tab, CR and LF between a JSON key and its value (not \v or \f, unlike the JSON helpers)
static void test_mask_blanks() {
    TEST_ASSERT_EQUAL_STRING("{\"token\" \t:\r\n \"***\"}", maskSecrets("{\"token\" \t:\r\n \"S\"}").c_str());
}

void run_upnp_tests() {
    RUN_TEST(test_mask_blanks);
    RUN_TEST(test_upnp_xml);
    RUN_TEST(test_upnp_description);
    RUN_TEST(test_upnp_queue);
    RUN_TEST(test_favorites);
    RUN_TEST(test_queue_template);
    RUN_TEST(test_xml_hostile_speed);
    RUN_TEST(test_mask_more);
    RUN_TEST(test_template_empty_element);
}
