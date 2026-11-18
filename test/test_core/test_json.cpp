// Tests: JSON helpers (flat and depth-aware) and escape decoding (pio test -e native)
#include <unity.h>

#include <string>
#include <vector>
#include <algorithm>

#include "core.h"
#include "qobuz.h"
#include "voice.h"
#include "upnp.h"
#include "test_helpers.h"

using namespace wr;

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

static void test_json_array_progress() {
    // '}' égaré au niveau du tableau : boucle sans fin avant
    TEST_ASSERT_TRUE(parseDeezerAlbums("{\"data\":[}").empty());
    TEST_ASSERT_TRUE(parseQobuzTracks("{\"tracks\":{\"items\":[}]}}").empty());
    auto a = jsonArray("[1,}2,:3]");
    TEST_ASSERT_EQUAL(3, (int)a.size());
    TEST_ASSERT_EQUAL_STRING("2", a[1].c_str());
    TEST_ASSERT_TRUE(parseDeezerTracks("{\"data\":[}]}").empty());
}

static void test_escapes_strict() {
    std::string v;
    TEST_ASSERT_TRUE(jsonString("{\"a\":\"l1\\r\\nl2\\b\\f\\/\"}", "a", v));
    TEST_ASSERT_EQUAL_STRING("l1\nl2/", v.c_str());  // avant : « l1r\nl2bf/ »
    TEST_ASSERT_EQUAL_STRING("l1\nl2", jsonText("\"l1\\r\\nl2\\b\\f\"").c_str());
    // Références numériques XML refusées : NUL, contrôle, moitié de paire, signes, espaces, hors Unicode
    for (const char* bad : {"&#0;", "&#1;", "&#x1F;", "&#xD800;", "&#-1;", "&#+65;", "&# 65;", "&#x110000;", "&#;", "&#x;"})
        TEST_ASSERT_EQUAL_STRING(bad, xmlUnescape(bad).c_str());
    TEST_ASSERT_EQUAL_STRING("\tA\n\xF0\x9F\x8E\xB5", xmlUnescape("&#9;&#65;&#x0A;&#x1F3B5;").c_str());
    TEST_ASSERT_EQUAL_STRING("a &b c;", xmlUnescape("a &b c;").c_str());
    TEST_ASSERT_EQUAL_STRING("&abcdefghijkl;", xmlUnescape("&abcdefghijkl;").c_str());  // trop long : brut
}

// jsonString and jsonText share one escape decoder but keep their own limits (they differed before it was shared)
static void test_json_entry_styles() {
    std::string v;
    TEST_ASSERT_TRUE(jsonString("{\"a\":\"x\"y\"}", "a", v));  // flat lookup: stops at the first quote
    TEST_ASSERT_EQUAL_STRING("x", v.c_str());
    TEST_ASSERT_FALSE(jsonString("{\"a\":\"x\\", "a", v));     // no closing quote
    TEST_ASSERT_EQUAL_STRING("x\"y", jsonText("\"x\"y\"").c_str());  // raw token: last character is the quote
    TEST_ASSERT_EQUAL_STRING("x\\", jsonText("\"x\\\"").c_str());    // backslash just before it: kept
    TEST_ASSERT_EQUAL_STRING("", jsonText("\"x").c_str());
    TEST_ASSERT_EQUAL_STRING("\xC3\xA9", jsonText("\"\\u00e9").c_str());  // \uXXXX may read the last character
    // Depth-aware helpers skip every std::isspace character, \v and \f included
    TEST_ASSERT_EQUAL_STRING("1", jsonTop("{\v\"a\"\f:\v1}", "a").c_str());
    long n = 0;
    TEST_ASSERT_TRUE(jsonInt("{\"a\"\f:\v\"12\"}", "a", n));
    TEST_ASSERT_EQUAL(12, (int)n);
}

void run_json_tests() {
    RUN_TEST(test_json_entry_styles);
    RUN_TEST(test_json_escapes);
    RUN_TEST(test_json_unicode);
    RUN_TEST(test_json_array_progress);
    RUN_TEST(test_escapes_strict);
}
