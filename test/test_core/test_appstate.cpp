// Tests: Loop timing decisions (appstate.h): waiting rings, sleep, screen (pio test -e native)
#include <unity.h>

#include <string>
#include <vector>
#include <algorithm>

#include "appstate.h"
#include "test_helpers.h"

using namespace wr;

static void test_app_expired() {
    TEST_ASSERT_FALSE(expired(1000, 1005, 10000));  // instant pris après `now` (markPending) : pas expiré
    TEST_ASSERT_FALSE(expired(1000, 1000, 0));
    TEST_ASSERT_FALSE(expired(11000, 1000, 10000));
    TEST_ASSERT_TRUE(expired(11001, 1000, 10000));
    TEST_ASSERT_TRUE(expired(5000, 0xFFFFF000u, 1000));  // compteur repassé par zéro (49 jours)
    TEST_ASSERT_FALSE(expired(100, 0xFFFFFF00u, 1000));
}

static void test_app_idle() {
    const uint32_t min = 60000;
    IdleInput in;
    in.online = true;
    in.fullBright = true;
    in.sleepMin = 30;
    // 45 min de musique sans appui, l'album vient de finir : ni veille ni écran éteint tout de suite
    in.touchIdleMs = 45 * min;
    in.notPlayingMs = 2000;
    TEST_ASSERT_TRUE(idleAction(in) == IdleAction::Dim);
    in.fullBright = false;
    TEST_ASSERT_TRUE(idleAction(in) == IdleAction::None);
    in.notPlayingMs = 21 * min;
    TEST_ASSERT_TRUE(idleAction(in) == IdleAction::Off);
    in.dark = true;
    TEST_ASSERT_TRUE(idleAction(in) == IdleAction::None);  // déjà éteint
    in.notPlayingMs = 31 * min;
    TEST_ASSERT_TRUE(idleAction(in) == IdleAction::Sleep);
    in.modal = true;
    TEST_ASSERT_TRUE(idleAction(in) == IdleAction::None);  // pas de veille sur un écran modal (déjà éteint)
    in.modal = false;
    in.sleepMin = 0;
    TEST_ASSERT_TRUE(idleAction(in) == IdleAction::None);
    // Appui récent : rien, même longtemps après la fin de la musique
    in = IdleInput();
    in.online = true;
    in.fullBright = true;
    in.sleepMin = 30;
    in.touchIdleMs = 5000;
    in.notPlayingMs = 60 * min;
    TEST_ASSERT_TRUE(idleAction(in) == IdleAction::None);
    // Lecture en cours : vue ambiance après 60 s, jamais éteint ni en veille
    in.playing = true;
    in.notPlayingMs = 0;
    in.touchIdleMs = 61000;
    TEST_ASSERT_TRUE(idleAction(in) == IdleAction::Ambient);
    in.ambient = true;
    TEST_ASSERT_TRUE(idleAction(in) == IdleAction::None);
    in.touchIdleMs = 90 * min;
    TEST_ASSERT_TRUE(idleAction(in) == IdleAction::Dim);
    in.fullBright = false;
    TEST_ASSERT_TRUE(idleAction(in) == IdleAction::None);
    // WiiM injoignable : son dernier « lecture » ne compte plus (écran éteint, veille, micro coupé)
    in.online = false;
    in.notPlayingMs = 25 * min;
    TEST_ASSERT_TRUE(idleAction(in) == IdleAction::Off);  // en vue ambiance : sortie, micro coupé
    in.notPlayingMs = 31 * min;
    TEST_ASSERT_TRUE(idleAction(in) == IdleAction::Sleep);
    in = IdleInput();
    in.fullBright = true;
    in.playing = true;  // hors ligne
    in.touchIdleMs = 61000;
    TEST_ASSERT_TRUE(idleAction(in) == IdleAction::None);  // pas de vue ambiance hors ligne
}

static void test_app_music_settled() {
    PlayerStatus st;
    st.playing = true;
    TEST_ASSERT_FALSE(musicSettled(st, false, 1000));
    TEST_ASSERT_TRUE(musicSettled(st, true, 1000));   // vu en chargement, puis joue
    TEST_ASSERT_TRUE(musicSettled(st, false, 4000));  // tuile de ce qui joue déjà, radio sans titre
    st.loading = true;
    TEST_ASSERT_FALSE(musicSettled(st, true, 9000));
    st.loading = false;
    st.playing = false;
    TEST_ASSERT_FALSE(musicSettled(st, true, 9000));
}

void run_appstate_tests() {
    RUN_TEST(test_app_expired);
    RUN_TEST(test_app_idle);
    RUN_TEST(test_app_music_settled);
}
