// Helpers shared by the test files of test/test_core (all built together by `pio test -e native`)
#pragma once

#include <unity.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#include "core.h"

namespace wr_test {
using namespace wr;

// Mesure factice : 10 px par caractère affiché (UTF-8 compté en points de code)
inline int fakeMeasure(const std::string& s) {
    int n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++n;
    return n * 10;
}

// Mise en page : blocs dans l'écran, sans chevauchement, cibles tactiles assez grandes
inline void checkLayout(int W, int H, int n) {
    Layout L = computeLayout(W, H, n);
    auto inside = [&](const Rect& r) { return r.x >= 0 && r.y >= 0 && r.right() <= W && r.bottom() <= H; };
    // Blocs principaux : dans l'écran, sans chevauchement
    std::vector<Rect> blocks = {L.art, L.info, L.progress, L.transport[kPrev], L.transport[kPlayPause],
                                L.transport[kNext], L.fav, L.volume, L.navHome, L.navSettings, L.navSearch};
    blocks.push_back(L.strip);  // toujours là, même sans preset
    for (size_t i = 0; i < blocks.size(); ++i) {
        TEST_ASSERT_TRUE(inside(blocks[i]));
        TEST_ASSERT_TRUE(blocks[i].w > 0 && blocks[i].h > 0);
        for (size_t j = i + 1; j < blocks.size(); ++j) TEST_ASSERT_FALSE(blocks[i].intersects(blocks[j]));
    }
    TEST_ASSERT_EQUAL(L.art.w, L.art.h);
    // Barre de navigation : boutons dedans, rien d'autre ne la touche
    for (const Rect& r : {L.navHome, L.navSettings, L.navSearch}) {
        TEST_ASSERT_TRUE(r.x >= L.rail.x && r.right() <= L.rail.right() && r.y >= L.rail.y && r.bottom() <= L.rail.bottom());
        TEST_ASSERT_TRUE(r.h >= 44 * std::min(W, H) / 540);
    }
    for (const Rect& r : {L.art, L.info, L.progress, L.volume, L.strip, L.transport[kPrev]})
        TEST_ASSERT_FALSE(r.intersects(L.rail));
    // Éléments de la pilule de volume : dedans, dans l'ordre, sans chevauchement
    for (const Rect& r : {L.transport[kVolDown], L.mute, L.volBar, L.transport[kVolUp]}) {
        TEST_ASSERT_TRUE(r.x >= L.volume.x && r.right() <= L.volume.right());
        TEST_ASSERT_TRUE(r.y >= L.volume.y && r.bottom() <= L.volume.bottom());
    }
    TEST_ASSERT_TRUE(L.transport[kVolDown].right() <= L.mute.x);
    TEST_ASSERT_TRUE(L.mute.right() <= L.volBar.x);
    TEST_ASSERT_TRUE(L.volBar.right() <= L.transport[kVolUp].x);
    TEST_ASSERT_TRUE(L.volBar.w > W / 6);
    // Cibles tactiles : au moins 44 px à l'échelle 540 (≈ 7 mm sur le Tab5)
    const int minTouch = 44 * std::min(W, H) / 540;
    for (int t = 0; t < kTransportCount; ++t) TEST_ASSERT_TRUE(L.transport[t].h >= minTouch * 9 / 10);
    TEST_ASSERT_TRUE(L.transport[kPlayPause].w >= L.transport[kPrev].w);  // lecture mise en avant
    // Presets : une page de tuiles dans la bande, sans chevauchement
    TEST_ASSERT_EQUAL(std::min(n, L.perPage), (int)L.presets.size());
    for (size_t i = 0; i < L.presets.size(); ++i) {
        const Rect& r = L.presets[i];
        TEST_ASSERT_TRUE(r.x >= L.strip.x && r.right() <= L.strip.right() && r.y >= L.strip.y &&
                         r.bottom() <= L.strip.bottom());
        TEST_ASSERT_TRUE(r.h >= minTouch * 2);
        for (size_t j = i + 1; j < L.presets.size(); ++j) TEST_ASSERT_FALSE(r.intersects(L.presets[j]));
    }
    // Textes : assez de place pour puces + titre + artiste
    TEST_ASSERT_TRUE(L.info.h >= 140 * std::min(W, H) / 540);
}

inline double secondsSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

}  // namespace wr_test

using wr_test::checkLayout;
using wr_test::fakeMeasure;
using wr_test::secondsSince;

// One function per test file, called by test_main.cpp
void run_fields_tests();
void run_wiim_api_tests();
void run_http_tests();
void run_json_tests();
void run_text_tests();
void run_layout_tests();
void run_upnp_tests();
void run_qobuz_deezer_tests();
void run_decisions_tests();
void run_appstate_tests();
void run_keys_tests();
void run_audio_tests();
void run_firmware_tests();
void run_robust_tests();
