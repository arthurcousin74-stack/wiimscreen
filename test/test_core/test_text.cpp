// Tests: Text helpers: wrapping, HTML escaping, normalisation, word matching (pio test -e native)
#include <unity.h>

#include <string>
#include <vector>
#include <algorithm>

#include "core.h"
#include "voice.h"
#include "test_helpers.h"

using namespace wr;

static void test_shares_word() {
    TEST_ASSERT_TRUE(sharesWord("aretha frank", {"Respect", "Aretha Franklin", "Lady Soul"}));
    TEST_ASSERT_TRUE(sharesWord("Beyonc\xC3\xA9", {"BEYONCE"}));  // accents et casse ignorés
    TEST_ASSERT_FALSE(sharesWord("justin hurwitz", {"Respect", "Aretha Franklin"}));
    // Mots courts seulement : requête entière comparée mot pour mot (avant : jamais en rapport, et chaque
    // écriture de l'adresse était essayée, ~10 s chacune)
    TEST_ASSERT_TRUE(sharesWord("a b", {"a b c"}));
    TEST_ASSERT_FALSE(sharesWord("a b", {"ab c"}));
    TEST_ASSERT_TRUE(sharesWord("U2", {"One", "U2", "Achtung Baby"}));
    TEST_ASSERT_FALSE(sharesWord("U2", {"U23 Live"}));
    TEST_ASSERT_TRUE(sharesWord("Yo Yo Ma", {"Bach: Cello Suites", "Yo-Yo Ma"}));
    TEST_ASSERT_TRUE(sharesWord("AC DC", {"Back in Black", "AC/DC"}));
    TEST_ASSERT_TRUE(sharesWord("!!!", {"x"}));  // rien à comparer : en rapport
    // Lettres non latines gardées (cyrillique en minuscules)
    TEST_ASSERT_TRUE(sharesWord("\xD0\x9A\xD0\xB8\xD0\xBD\xD0\xBE", {"\xD0\x93\xD1\x80\xD1\x83\xD0\xBF\xD0\xBF\xD0\xB0", "\xD0\x9A\xD0\x98\xD0\x9D\xD0\x9E"}));  // Кино / КИНО
    TEST_ASSERT_FALSE(sharesWord("\xD0\x9A\xD0\xB8\xD0\xBD\xD0\xBE", {"Daft Punk"}));
    TEST_ASSERT_TRUE(sharesWord("\xE5\x91\xA8\xE6\x9D\xB0", {"\xE5\x91\xA8\xE6\x9D\xB0\xE5\x80\xAB"}));  // 周杰 / 周杰倫
    TEST_ASSERT_EQUAL_STRING("\xD0\xBA\xD0\xB8\xD0\xBD\xD0\xBE", normalizeText("\xD0\x9A\xD0\x98\xD0\x9D\xD0\x9E!").c_str());
    TEST_ASSERT_EQUAL_STRING("a b", normalizeText("a\xE2\x80\x94" "b").c_str());  // tiret cadratin : séparateur
    TEST_ASSERT_EQUAL_STRING("strasse aeon", normalizeText("Stra\xC3\x9F" "e \xC3\x86on").c_str());
    TEST_ASSERT_EQUAL_STRING("x y", normalizeText("x\xFF\xC3y").c_str());  // octets invalides : séparateurs
}

static void test_html_escape() {
    TEST_ASSERT_EQUAL_STRING("&lt;script&gt;&amp;&quot;&#39;",
                             htmlEscape("<script>&\"'").c_str());
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

static void test_voice_text() {
    TEST_ASSERT_EQUAL_STRING("joue fip", normalizeText("Joue FIP !").c_str());
    TEST_ASSERT_EQUAL_STRING("mets france culture", normalizeText("  Mets \xC2\xAB France Culture \xC2\xBB.").c_str());
    TEST_ASSERT_EQUAL_STRING("ecoute l heure des reves", normalizeText("\xC3\x89""coute l'Heure des r\xC3\xAA""ves").c_str());
    TEST_ASSERT_EQUAL_STRING("coeur", normalizeText("C\xC5\x93ur").c_str());
}

static void test_wrap_empty() {
    // Texte vide ou fait d'espaces : une ligne (ui.cpp lit wrapText(...)[0])
    for (const char* s : {"", " ", "     "}) {
        auto l = wrapText(s, 100, 1, fakeMeasure);
        TEST_ASSERT_EQUAL(1, (int)l.size());
        TEST_ASSERT_EQUAL_STRING("", l[0].c_str());
        TEST_ASSERT_EQUAL(1, (int)wrapText(s, 100, 3, fakeMeasure).size());
    }
    TEST_ASSERT_TRUE(wrapText("abc", 100, 0, fakeMeasure).empty());
    // Titres faits d'espaces : vides
    PlayerStatus st = parsePlayerStatus("{\"status\":\"play\",\"Title\":\" \",\"Artist\":\"  A  \",\"Album\":\"\"}");
    TEST_ASSERT_EQUAL_STRING("", st.title.c_str());
    TEST_ASSERT_EQUAL_STRING("A", st.artist.c_str());
    TrackMeta m = parseMetaInfo("{\"metaData\":{\"title\":\" \",\"artist\":\" Miles \",\"album\":\" unknow \"}}");
    TEST_ASSERT_EQUAL_STRING("", m.title.c_str());
    TEST_ASSERT_EQUAL_STRING("Miles", m.artist.c_str());
    TEST_ASSERT_EQUAL_STRING("", m.album.c_str());
}

// utf8PopBack and wrapText share one implementation (they were two equivalent ones): stray continuation bytes
static void test_utf8_pop_back_edges() {
    TEST_ASSERT_EQUAL_STRING("", utf8PopBack("\x80\x80").c_str());
    TEST_ASSERT_EQUAL_STRING("a", utf8PopBack("ab\x80").c_str());
    TEST_ASSERT_EQUAL_STRING("", utf8PopBack("\xC3").c_str());
    TEST_ASSERT_EQUAL_STRING("\xC3\xA9", utf8PopBack("\xC3\xA9\xE2\x80\xA6").c_str());
}

// Search field typing: on-screen keys and a real keyboard (typed text) follow the same rules
static void test_type_text() {
    TEST_ASSERT_EQUAL_STRING("miles davis", typeText("", "miles davis").c_str());
    TEST_ASSERT_EQUAL_STRING("ab", typeText("a", "b").c_str());
    TEST_ASSERT_EQUAL_STRING("a b", typeText("a", "  b").c_str());   // no double space
    TEST_ASSERT_EQUAL_STRING("x", typeText("", " x").c_str());       // no leading space
    TEST_ASSERT_EQUAL_STRING("a ", typeText("a", " ").c_str());      // a trailing space stays (on-screen: same)
    TEST_ASSERT_EQUAL_STRING("ab", typeText("a", "\t\r\n\x7f\x1b" "b").c_str());  // control characters
    TEST_ASSERT_EQUAL_STRING("caf\xC3\xA9", typeText("caf", "\xC3\xA9").c_str());
    TEST_ASSERT_EQUAL_STRING("\xE2\x82\xAC\xF0\x9F\x8E\xB5", typeText("", "\xE2\x82\xAC\xF0\x9F\x8E\xB5").c_str());
    TEST_ASSERT_EQUAL_STRING("ab", typeText("a", "\x80\xC3" "b").c_str());  // stray continuation, truncated lead
    TEST_ASSERT_EQUAL_STRING("a", typeText("a", "\xE2\x82").c_str());       // truncated at the end
    // Length: like the on-screen keys (a character while shorter than the maximum)
    const std::string full(60, 'x');
    TEST_ASSERT_EQUAL_STRING(full.c_str(), typeText(full, "y").c_str());
    TEST_ASSERT_EQUAL(60u, typeText(std::string(58, 'x'), "abcd").size());
    TEST_ASSERT_EQUAL(61u, typeText(std::string(59, 'x'), "\xC3\xA9\xC3\xA9").size());  // never cuts a character
    TEST_ASSERT_EQUAL(5u, typeText("", "abcdefgh", 5).size());
}

void run_text_tests() {
    RUN_TEST(test_type_text);
    RUN_TEST(test_utf8_pop_back_edges);
    RUN_TEST(test_shares_word);
    RUN_TEST(test_html_escape);
    RUN_TEST(test_wrap);
    RUN_TEST(test_voice_text);
    RUN_TEST(test_wrap_empty);
}
