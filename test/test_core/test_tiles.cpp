// Historique et ordre des tuiles (lib/core/src/tiles.*)
#include <unity.h>

#include "test_helpers.h"
#include "tiles.h"

using namespace wr;

static std::vector<size_t> order(const std::vector<std::string>& keys, const TileStats& s, bool recent = true) {
    return tileOrder(keys, s, recent);
}

static void test_tile_key() {
    Preset p;
    p.slot = 3;
    TEST_ASSERT_EQUAL_STRING("preset:3", tileKey(p).c_str());
    p.favId = "qobuz-album:42";
    TEST_ASSERT_EQUAL_STRING("qobuz-album:42", tileKey(p).c_str());
    p.favId = "a\tb";
    TEST_ASSERT_EQUAL_STRING("", tileKey(p).c_str());
}

static void test_tile_played_and_order() {
    const std::vector<std::string> keys = {"preset:1", "preset:2", "a", "b", "c"};
    TileStats s;
    // Rien de joué : ordre donné
    std::vector<size_t> o = order(keys, s);
    TEST_ASSERT_EQUAL(5, (int)o.size());
    for (size_t i = 0; i < o.size(); ++i) TEST_ASSERT_EQUAL((int)i, (int)o[i]);
    s.played("b");
    s.played("preset:2");
    s.played("b");  // b : la plus récente
    TEST_ASSERT_EQUAL(3u, s.seq);
    TEST_ASSERT_EQUAL(2u, s.find("b")->count);
    o = order(keys, s);
    const size_t want[] = {3, 1, 0, 2, 4};
    for (size_t i = 0; i < 5; ++i) TEST_ASSERT_EQUAL((int)want[i], (int)o[i]);
    // Ordre fixe : l'historique ne change rien
    o = order(keys, s, false);
    for (size_t i = 0; i < o.size(); ++i) TEST_ASSERT_EQUAL((int)i, (int)o[i]);
    // Épinglées d'abord, dans l'ordre où on les a épinglées, même jamais jouées et en ordre fixe
    s.setPin("c", true);
    s.setPin("preset:1", true);
    s.setPin("c", true);  // déjà épinglée : place gardée
    o = order(keys, s, false);
    const size_t fixed[] = {4, 0, 1, 2, 3};
    for (size_t i = 0; i < 5; ++i) TEST_ASSERT_EQUAL((int)fixed[i], (int)o[i]);
    o = order(keys, s);
    const size_t recent[] = {4, 0, 3, 1, 2};
    for (size_t i = 0; i < 5; ++i) TEST_ASSERT_EQUAL((int)recent[i], (int)o[i]);
    s.setPin("c", false);
    TEST_ASSERT_FALSE(s.pinned("c"));
    TEST_ASSERT_TRUE(s.pinned("preset:1"));
    s.setPin("zz", false);  // inconnue : rien
    TEST_ASSERT_NULL(s.find("zz"));
    // Clé illégale : ignorée
    s.played("x\ny");
    TEST_ASSERT_NULL(s.find("x\ny"));
    TEST_ASSERT_EQUAL(3u, s.seq);
}

static void test_tile_stats_text() {
    TileStats s;
    s.played("preset:1");
    s.played("qobuz-album:9");
    s.setPin("fav-2", true);
    const std::string t = tileStatsToText(s);
    TEST_ASSERT_EQUAL_STRING("seq\t2\npreset:1\t1\t1\t0\nqobuz-album:9\t2\t1\t0\nfav-2\t0\t0\t1\n", t.c_str());
    const TileStats r = tileStatsFromText(t);
    TEST_ASSERT_EQUAL(2u, r.seq);
    TEST_ASSERT_EQUAL(3, (int)r.uses.size());
    TEST_ASSERT_EQUAL(1u, r.find("fav-2")->pin);
    TEST_ASSERT_EQUAL(2u, r.find("qobuz-album:9")->last);
    // Lignes abîmées ignorées, doublon ignoré, compteur jamais derrière une entrée, CRLF accepté
    const TileStats b = tileStatsFromText("seq\t1\r\na\t5\t2\t0\r\nb\t-1\t0\t0\nc\t1\t1\nd\t99999999999\t1\t0\na\t7\t7\t7\n\n");
    TEST_ASSERT_EQUAL(5u, b.seq);
    TEST_ASSERT_EQUAL(1, (int)b.uses.size());
    TEST_ASSERT_EQUAL(2u, b.find("a")->count);
    TEST_ASSERT_EQUAL(0, (int)tileStatsFromText("").uses.size());
}

static void test_tile_stats_full() {
    TileStats s;
    s.setPin("pinned", true);
    for (size_t i = 0; i < TileStats::kMax + 10; ++i) s.played("k" + std::to_string(i));
    TEST_ASSERT_EQUAL((int)TileStats::kMax, (int)s.uses.size());
    TEST_ASSERT_TRUE(s.pinned("pinned"));  // épinglée : jamais retirée
    TEST_ASSERT_NULL(s.find("k0"));        // les plus anciennes sont parties
    TEST_ASSERT_NOT_NULL(s.find("k209"));
}

static void test_tile_kind() {
    Preset p;
    TEST_ASSERT_TRUE(tileKind(p) == TileKind::Preset);
    p.favId = "0a1b2c3d";  // favori enregistré (♥, Flow)
    TEST_ASSERT_TRUE(tileKind(p) == TileKind::Favorite);
    p.favId = "qobuz-album:42";
    TEST_ASSERT_TRUE(tileKind(p) == TileKind::Qobuz);
    p.favId = "qobuz-favoris";
    TEST_ASSERT_TRUE(tileKind(p) == TileKind::Qobuz);
    p.favId = "deezer-album:7";
    TEST_ASSERT_TRUE(tileKind(p) == TileKind::Deezer);
    p.favId = "deezer-favoris";
    TEST_ASSERT_TRUE(tileKind(p) == TileKind::Deezer);
}

void run_tiles_tests() {
    RUN_TEST(test_tile_kind);
    RUN_TEST(test_tile_key);
    RUN_TEST(test_tile_played_and_order);
    RUN_TEST(test_tile_stats_text);
    RUN_TEST(test_tile_stats_full);
}
