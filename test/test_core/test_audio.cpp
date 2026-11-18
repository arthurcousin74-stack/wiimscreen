// Tests: Spectrum, visualiser, motion detector, WAV header (pio test -e native)
#include <unity.h>

#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

#include "dsp.h"
#include "voice.h"
#include "test_helpers.h"

using namespace wr;

static void test_spectrum() {
    const int n = 512, sr = 16000, bands = 24;
    std::vector<int16_t> x(n);
    float db[bands];
    // Silence : tout très bas
    spectrumBands(x.data(), n, sr, bands, db);
    for (float d : db) TEST_ASSERT_TRUE(d < -90);
    // Sinusoïde 1 kHz à pleine échelle : une seule bande forte, proche de 0 dB
    for (int i = 0; i < n; ++i) x[i] = (int16_t)(32000 * std::sin(2 * 3.14159265 * 1000 * i / sr));
    spectrumBands(x.data(), n, sr, bands, db);
    int best = 0;
    for (int b = 1; b < bands; ++b)
        if (db[b] > db[best]) best = b;
    float lo = 60 * std::pow(8000.0f / 60, float(best) / bands), hi = 60 * std::pow(8000.0f / 60, float(best + 1) / bands);
    TEST_ASSERT_TRUE(lo <= 1060 && hi >= 940);
    TEST_ASSERT_TRUE(db[best] > -3 && db[best] < 1);
    TEST_ASSERT_TRUE(db[0] < db[best] - 30);  // basses loin derrière

    // Lissage : une bande forte monte, puis redescend doucement dans le silence
    Visualizer v(bands);
    for (int k = 0; k < 20; ++k) v.update(db);
    TEST_ASSERT_TRUE(v.levels()[best] > 0.8f);
    TEST_ASSERT_TRUE(v.levels()[0] < 0.2f);
    float quiet[bands];
    for (float& q : quiet) q = -100;
    v.update(quiet);
    TEST_ASSERT_TRUE(v.levels()[best] > 0.5f);  // pas de chute brutale
    TEST_ASSERT_TRUE(v.peaks()[best] > 0.8f);   // crête maintenue
    for (int k = 0; k < 200; ++k) v.update(quiet);
    TEST_ASSERT_TRUE(v.levels()[best] < 0.05f);
}

static void test_motion() {
    MotionDetector m;
    // Posé à plat : rien, même avec une petite vibration (0,03 g)
    for (int i = 0; i < 300; ++i) TEST_ASSERT_FALSE(m.feed(0.01f * (i % 3), 0.03f * ((i % 2) ? 1 : -1), 1.0f));
    // Soulevé : écart franc et soutenu, un seul déclenchement
    int triggers = 0;
    for (int i = 0; i < 10; ++i) triggers += m.feed(0.3f, 0.2f, 0.85f);
    TEST_ASSERT_EQUAL(1, triggers);
    // Choc isolé d'un échantillon (porte qui claque) : ignoré
    MotionDetector m2;
    for (int i = 0; i < 100; ++i) m2.feed(0, 0, 1);
    TEST_ASSERT_FALSE(m2.feed(0, 0, 1.6f));
    TEST_ASSERT_FALSE(m2.feed(0, 0, 1.0f));
    TEST_ASSERT_FALSE(m2.feed(0, 0, 1.0f));
}

static void test_voice_audio() {

    uint8_t h[44];
    wavHeader(h, 32000, 16000);
    TEST_ASSERT_EQUAL_MEMORY("RIFF", h, 4);
    TEST_ASSERT_EQUAL_MEMORY("WAVEfmt ", h + 8, 8);
    TEST_ASSERT_EQUAL(36 + 32000, h[4] | h[5] << 8 | h[6] << 16 | h[7] << 24);
    TEST_ASSERT_EQUAL(16000, h[24] | h[25] << 8 | h[26] << 16);
    TEST_ASSERT_EQUAL(32000, h[40] | h[41] << 8 | h[42] << 16);

}

void run_audio_tests() {
    RUN_TEST(test_spectrum);
    RUN_TEST(test_motion);
    RUN_TEST(test_voice_audio);
}
