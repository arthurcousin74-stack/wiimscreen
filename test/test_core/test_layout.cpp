// Tests: Screen layout, keyboard layout, interface language, accent colour (pio test -e native)
#include <unity.h>

#include <string>
#include <vector>
#include <algorithm>

#include "core.h"
#include "i18n.h"
#include "test_helpers.h"

using namespace wr;

static void test_keyboard() {
    // Langue : QWERTY et textes anglais par défaut, AZERTY et textes français en français ; mêmes touches
    TEST_ASSERT_TRUE(lang() == Lang::En);
    {
        auto en = keyboardLayout(Rect{100, 200, 1160, 500});
        TEST_ASSERT_EQUAL_STRING("q", en[10].text.c_str());
        TEST_ASSERT_EQUAL_STRING("Search", en.back().label.c_str());
        setLang(Lang::Fr);
        auto fr = keyboardLayout(Rect{100, 200, 1160, 500});
        TEST_ASSERT_EQUAL_STRING("a", fr[10].text.c_str());
        TEST_ASSERT_EQUAL_STRING("Chercher", fr.back().label.c_str());
        TEST_ASSERT_EQUAL((int)en.size(), (int)fr.size());
        TEST_ASSERT_EQUAL_STRING("Effacer", L("Clear", "Effacer"));
        setLang(Lang::En);
        TEST_ASSERT_EQUAL_STRING("Clear", L("Clear", "Effacer"));
        TEST_ASSERT_TRUE(langFromCode("fr") == Lang::Fr && langFromCode("de") == Lang::En && langFromCode("") == Lang::En);
        TEST_ASSERT_EQUAL_STRING("fr", langCode(Lang::Fr));
        // Lettres : chacune une seule fois dans les deux dispositions
        for (auto* k : {&en, &fr}) {
            std::string letters;
            for (auto& key : *k)
                if (key.kind == Key::Char && key.text.size() == 1 && key.text[0] >= 'a' && key.text[0] <= 'z') letters += key.text;
            std::sort(letters.begin(), letters.end());
            TEST_ASSERT_EQUAL_STRING("abcdefghijklmnopqrstuvwxyz", letters.c_str());
        }
    }
    for (Lang lg : {Lang::En, Lang::Fr})
    for (const Rect& area : {Rect{100, 200, 1160, 500}, Rect{20, 600, 680, 640}, Rect{80, 150, 860, 380}}) {
        setLang(lg);
        auto keys = keyboardLayout(area);
        TEST_ASSERT_EQUAL(10 + 10 + 10 + 9 + 3, (int)keys.size());
        int enter = 0, back = 0;
        for (size_t i = 0; i < keys.size(); ++i) {
            const Rect& r = keys[i].r;
            TEST_ASSERT_TRUE(r.x >= area.x && r.y >= area.y && r.right() <= area.right() && r.bottom() <= area.bottom());
            TEST_ASSERT_TRUE(r.w >= 44 && r.h >= 44);  // cible tactile
            for (size_t j = i + 1; j < keys.size(); ++j) TEST_ASSERT_FALSE(r.intersects(keys[j].r));
            enter += keys[i].kind == Key::Enter;
            back += keys[i].kind == Key::Back;
        }
        TEST_ASSERT_EQUAL(1, enter);
        TEST_ASSERT_EQUAL(1, back);
    }
    setLang(Lang::En);
    TEST_ASSERT_EQUAL_STRING("caf", utf8PopBack("caf\xC3\xA9").c_str());  // « é » : deux octets retirés
    TEST_ASSERT_EQUAL_STRING("ab", utf8PopBack("abc").c_str());
    TEST_ASSERT_EQUAL_STRING("", utf8PopBack("").c_str());
}

static void test_layout() {
    for (int n : {0, 1, 6, 12}) {
        checkLayout(1280, 720, n);  // Tab5 paysage
        checkLayout(720, 1280, n);  // Tab5 portrait
        checkLayout(960, 540, n);
        checkLayout(800, 480, n);   // écran officiel 7" du Raspberry Pi (version ordinateur)
        checkLayout(480, 800, n);
        checkLayout(1920, 1080, n);  // écran d'ordinateur
    }
    Layout L = computeLayout(1280, 720, 12);
    TEST_ASSERT_EQUAL(6, L.perPage);  // 12 presets : 2 pages de 6
    TEST_ASSERT_TRUE(L.art.w >= 400);  // la pochette domine
    L = computeLayout(720, 1280, 12);
    TEST_ASSERT_EQUAL(6, L.perPage);  // portrait : 2 rangées de 3
    TEST_ASSERT_TRUE(nameColor("FIP") == nameColor("FIP"));
    TEST_ASSERT_TRUE(nameColor("FIP") != nameColor("Nova"));
    // Statut "loading" : en lecture et en chargement
    PlayerStatus s = parsePlayerStatus("{\"status\":\"loading\"}");
    TEST_ASSERT_TRUE(s.playing && s.loading);
    s = parsePlayerStatus("{\"status\":\"load\"}");  // WiiM, chargement d'une file
    TEST_ASSERT_TRUE(s.playing && s.loading);
}

static void test_accent() {
    const uint32_t fb = 0x123456;
    std::vector<uint8_t> px;
    auto fill = [&](int n, uint8_t r, uint8_t g, uint8_t b) {
        for (int i = 0; i < n; ++i) px.insert(px.end(), {r, g, b});
    };
    // Noir, blanc, gris : pas de teinte, couleur par défaut
    fill(100, 0, 0, 0);
    fill(100, 255, 255, 255);
    fill(100, 128, 128, 128);
    TEST_ASSERT_EQUAL_HEX32(fb, accentColor(px.data(), px.size() / 3, fb));
    TEST_ASSERT_EQUAL_HEX32(fb, accentColor(nullptr, 0, fb));
    // Pochette surtout grise avec une tache bleue : l'accent est bleu
    fill(60, 30, 60, 220);
    uint32_t c = accentColor(px.data(), px.size() / 3, fb);
    int r = c >> 16, g = (c >> 8) & 0xFF, b = c & 0xFF;
    TEST_ASSERT_TRUE(b > r && b > g);
    // Rouge sombre : éclairci pour rester lisible sur fond sombre
    px.clear();
    fill(50, 90, 10, 10);
    c = accentColor(px.data(), px.size() / 3, fb);
    r = c >> 16;
    g = (c >> 8) & 0xFF;
    TEST_ASSERT_TRUE(r > 180 && r > g + 60);
}

void run_layout_tests() {
    RUN_TEST(test_keyboard);
    RUN_TEST(test_layout);
    RUN_TEST(test_accent);
}
