// Tests: Qobuz and Deezer JSON parsing, Deezer profile number, cover URLs (pio test -e native)
#include <unity.h>

#include <string>
#include <vector>
#include <algorithm>

#include "qobuz.h"
#include "voice.h"
#include "upnp.h"
#include "test_helpers.h"

using namespace wr;

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
    TEST_ASSERT_EQUAL_STRING("908622995", deezerLovedPlaylist(
        "{\"data\":[{\"id\":11,\"title\":\"Road\",\"is_loved_track\":false},"
        "{\"id\":908622995,\"title\":\"Loved tracks\",\"is_loved_track\":true,\"nb_tracks\":42}],\"total\":2}").c_str());
    TEST_ASSERT_EQUAL_STRING("", deezerLovedPlaylist("{\"data\":[{\"id\":11,\"is_loved_track\":false}]}").c_str());
    TEST_ASSERT_EQUAL_STRING("", deezerLovedPlaylist("{\"data\":[{\"id\":\"1/2\",\"is_loved_track\":true}]}").c_str());
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

static void test_deezer_ids() {
    // Numéros au-delà de 2^31 (32 bits sur l'ESP32-P4) ; « album » et « artist » comme titres
    auto t = parseDeezerTracks(
        "{\"data\":[{\"id\":3000000001,\"title\":\"album\",\"artist\":{\"id\":5,\"name\":\"A\"},"
        "\"album\":{\"id\":\"4294967297\",\"title\":\"B\",\"cover_medium\":\"https:\\/\\/e\\/c.jpg\"}},"
        "{\"id\":2,\"title\":\"artist\",\"album\":{\"id\":7,\"title\":\"C\"},\"artist\":{\"name\":\"D\"}}]}");
    TEST_ASSERT_EQUAL(2, (int)t.size());
    TEST_ASSERT_TRUE(sizeof(t[0].id) == 8 && sizeof(t[0].albumId) == 8);
    TEST_ASSERT_TRUE(t[0].id == 3000000001LL);
    TEST_ASSERT_TRUE(t[0].albumId == 4294967297LL);
    TEST_ASSERT_EQUAL_STRING("album", t[0].title.c_str());
    TEST_ASSERT_EQUAL_STRING("A", t[0].artist.c_str());
    TEST_ASSERT_EQUAL_STRING("B", t[0].album.c_str());
    TEST_ASSERT_EQUAL_STRING("https://e/c.jpg", t[0].cover.c_str());
    TEST_ASSERT_TRUE(t[1].albumId == 7);
    TEST_ASSERT_EQUAL_STRING("D", t[1].artist.c_str());
    TEST_ASSERT_TRUE(parseDeezerTracks("{\"data\":[{\"id\":-3,\"title\":\"x\"},{\"id\":\"12a\",\"title\":\"y\"}]}").empty());
}

void run_qobuz_deezer_tests() {
    RUN_TEST(test_deezer);
    RUN_TEST(test_qobuz_json);
    RUN_TEST(test_deezer_user);
    RUN_TEST(test_deezer_ids);
}
