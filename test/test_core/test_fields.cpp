// Tests: Settings text: preset list and settings backup fields (pio test -e native)
#include <unity.h>

#include <string>
#include <vector>
#include <algorithm>

#include "core.h"
#include "test_helpers.h"

using namespace wr;

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

void run_fields_tests() {
    RUN_TEST(test_presets_ok);
    RUN_TEST(test_presets_errors);
    RUN_TEST(test_fields);
}
