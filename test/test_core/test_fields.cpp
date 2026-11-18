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

// Réglages décrits par un tableau (même usage que src/storage/settings.cpp, avec std::string au lieu de String)
struct FakeSettings {
    std::string name;
    std::string model = "modele-par-defaut";
    bool on = true;      // défaut vrai : tout sauf "0"
    bool extra = false;  // défaut faux : "1" seulement
    int level = 5;
    int minutes = 0;
    std::vector<int> derived;  // déduit, hors du tableau
};
using FakeField = SettingField<FakeSettings, std::string>;
static constexpr FakeField kFake[] = {
    FakeField::text("name", &FakeSettings::name),
    FakeField::text("model", &FakeSettings::model, true),
    FakeField::flag("on", &FakeSettings::on),
    FakeField::flag("extra", &FakeSettings::extra),
    FakeField::number("level", &FakeSettings::level, 1, 20),
    FakeField::number("minutes", &FakeSettings::minutes, 0, 240, false),
};
// Le contrôle de src/storage/settings.cpp : un membre ajouté sans sa ligne change le compte
static_assert(aggregateFieldCount<FakeSettings>() == sizeof(kFake) / sizeof(kFake[0]) + 1, "FakeSettings");
struct FakeSettingsGrown {  // FakeSettings avec un réglage de plus, oublié dans le tableau
    std::string name;
    std::string model = "modele-par-defaut";
    bool on = true;
    bool extra = false;
    int level = 5;
    int minutes = 0;
    std::vector<int> derived;
    std::string forgotten;
};
static_assert(aggregateFieldCount<FakeSettingsGrown>() != sizeof(kFake) / sizeof(kFake[0]) + 1, "Grown");
struct OneMore {
    std::string a;
    bool b = false;
    int c = 0;
    std::vector<Preset> d;
    std::string e;
};
static_assert(aggregateFieldCount<OneMore>() == 5, "OneMore");

static void test_setting_values() {
    TEST_ASSERT_EQUAL_STRING("1", flagText(true).c_str());
    TEST_ASSERT_EQUAL_STRING("0", flagText(false).c_str());
    TEST_ASSERT_TRUE(parseFlag("", true));
    TEST_ASSERT_TRUE(parseFlag("yes", true));
    TEST_ASSERT_TRUE(parseFlag("00", true));
    TEST_ASSERT_FALSE(parseFlag("0", true));
    TEST_ASSERT_TRUE(parseFlag("1", false));
    TEST_ASSERT_FALSE(parseFlag("yes", false));
    TEST_ASSERT_FALSE(parseFlag("1 ", false));
    TEST_ASSERT_EQUAL(7, parseNumber(" 7", 1, 20));
    TEST_ASSERT_EQUAL(2, parseNumber("2x", 0, 3));
    TEST_ASSERT_EQUAL(1, parseNumber("abc", 1, 20));  // illisible : 0, puis la borne basse
    TEST_ASSERT_EQUAL(240, parseNumber("99999999999999999999", 0, 240));
    TEST_ASSERT_EQUAL(0, parseNumber("-5", 0, 240));
}

static void test_settings_table() {
    FakeSettings s;
    s.name = "Salon\tWi-Fi";
    s.on = false;
    s.extra = true;
    s.level = 12;
    s.minutes = 30;
    const std::string text = encodeSettings(s, kFake);
    // Ordre du tableau, nombres en décimal, cases en 1/0 (format des copies de secours déjà écrites)
    TEST_ASSERT_EQUAL_STRING("name\tSalon\\tWi-Fi\nmodel\tmodele-par-defaut\non\t0\nextra\t1\nlevel\t12\nminutes\t30\n",
                             text.c_str());
    FakeSettings back;
    back.derived = {1};
    decodeSettings(text, back, FakeSettings(), kFake);
    TEST_ASSERT_EQUAL_STRING("Salon\tWi-Fi", back.name.c_str());
    TEST_ASSERT_FALSE(back.on);
    TEST_ASSERT_TRUE(back.extra);
    TEST_ASSERT_EQUAL(12, back.level);
    TEST_ASSERT_EQUAL(30, back.minutes);
    TEST_ASSERT_EQUAL(1, (int)back.derived.size());  // hors du tableau : pas touché

    // Clés inconnues ignorées, dernière valeur gardée, bornes, texte vide → défaut (emptyIsDefault)
    FakeSettings odd;
    odd.model = "autre";
    decodeSettings("name\tA\nname\tB\nmodel\t\nlevel\t99\nminutes\t-1\nextra\tyes\nautre\tx\n", odd, FakeSettings(),
                   kFake);
    TEST_ASSERT_EQUAL_STRING("B", odd.name.c_str());
    TEST_ASSERT_EQUAL_STRING("modele-par-defaut", odd.model.c_str());
    TEST_ASSERT_EQUAL(20, odd.level);
    TEST_ASSERT_EQUAL(0, odd.minutes);
    TEST_ASSERT_FALSE(odd.extra);
    TEST_ASSERT_TRUE(odd.on);  // absent : valeur d'avant
}

void run_fields_tests() {
    RUN_TEST(test_presets_ok);
    RUN_TEST(test_presets_errors);
    RUN_TEST(test_fields);
    RUN_TEST(test_setting_values);
    RUN_TEST(test_settings_table);
}
