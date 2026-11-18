// Tests of lib/core/src/keys.*: key events from raw key-down/key-up (press, long press, repeat), key names.
#include <unity.h>

#include <vector>

#include "keys.h"
#include "test_helpers.h"

namespace {
using namespace wr;

std::vector<KeyEvent> drain(KeyTracker& t) {
    std::vector<KeyEvent> v;
    KeyEvent e;
    while (t.take(e)) v.push_back(e);
    return v;
}

void expectOne(KeyTracker& t, NavKey key, KeyEvent::Kind kind) {
    auto v = drain(t);
    TEST_ASSERT_EQUAL(1, (int)v.size());
    TEST_ASSERT_TRUE(v[0].key == key);
    TEST_ASSERT_EQUAL(kind, v[0].kind);
}

void expectNone(KeyTracker& t) { TEST_ASSERT_EQUAL(0, (int)drain(t).size()); }

// Ticks every 10 ms from `from` to `to` (inclusive); returns the events in order
std::vector<KeyEvent> run(KeyTracker& t, uint32_t from, uint32_t to) {
    for (uint32_t now = from; static_cast<int32_t>(to - now) >= 0; now += 10) t.tick(now);
    return drain(t);
}

void test_arrow_press_then_repeat() {
    KeyTracker t;
    const uint32_t t0 = 1000;
    t.down(NavKey::Up, t0);
    expectOne(t, NavKey::Up, KeyEvent::Press);
    // Nothing before the initial delay
    TEST_ASSERT_EQUAL(0, (int)run(t, t0, t0 + KeyTiming::kRepeatDelayMs - 10).size());
    t.tick(t0 + KeyTiming::kRepeatDelayMs);
    expectOne(t, NavKey::Up, KeyEvent::Repeat);
    // Then 8 per second: 1 s more gives 8 repeats
    auto v = run(t, t0 + KeyTiming::kRepeatDelayMs + 10, t0 + KeyTiming::kRepeatDelayMs + 1000);
    TEST_ASSERT_EQUAL(1000 / KeyTiming::kRepeatIntervalMs, (int)v.size());
    for (auto& e : v) TEST_ASSERT_EQUAL(KeyEvent::Repeat, e.kind);
    t.up(NavKey::Up, t0 + 1500);
    expectNone(t);
    TEST_ASSERT_EQUAL(0, (int)run(t, t0 + 1500, t0 + 3000).size());
}

void test_backspace_repeats_quick_tap_does_not() {
    KeyTracker t;
    t.down(NavKey::Backspace, 0);
    t.tick(50);
    t.up(NavKey::Backspace, 60);
    expectOne(t, NavKey::Backspace, KeyEvent::Press);
    TEST_ASSERT_EQUAL(0, (int)run(t, 60, 2000).size());
    t.down(NavKey::Backspace, 3000);
    auto v = run(t, 3000, 3000 + KeyTiming::kRepeatDelayMs);
    TEST_ASSERT_EQUAL(2, (int)v.size());
    TEST_ASSERT_EQUAL(KeyEvent::Repeat, v[1].kind);
}

void test_late_tick_gives_one_repeat_not_a_burst() {
    KeyTracker t;
    t.down(NavKey::Down, 0);
    drain(t);
    t.tick(3000);  // the app was busy for 3 s
    expectOne(t, NavKey::Down, KeyEvent::Repeat);
    t.tick(3000 + KeyTiming::kRepeatIntervalMs - 1);
    expectNone(t);
    t.tick(3000 + KeyTiming::kRepeatIntervalMs);
    expectOne(t, NavKey::Down, KeyEvent::Repeat);
}

void test_ok_short_press_on_release() {
    KeyTracker t;
    t.down(NavKey::Ok, 100);
    expectNone(t);  // nothing on down: it may become a long press
    TEST_ASSERT_EQUAL(0, (int)run(t, 100, 100 + KeyTiming::kLongPressMs - 10).size());
    t.up(NavKey::Ok, 100 + KeyTiming::kLongPressMs - 1);
    expectOne(t, NavKey::Ok, KeyEvent::Press);
}

void test_ok_long_press_once_then_no_press() {
    KeyTracker t;
    t.down(NavKey::Ok, 0);
    t.tick(KeyTiming::kLongPressMs - 1);
    expectNone(t);
    t.tick(KeyTiming::kLongPressMs);
    expectOne(t, NavKey::Ok, KeyEvent::LongPress);
    TEST_ASSERT_EQUAL(0, (int)run(t, KeyTiming::kLongPressMs, 5000).size());  // once, and Ok never repeats
    t.up(NavKey::Ok, 5000);
    expectNone(t);
}

void test_ok_released_late_without_tick_is_long() {
    KeyTracker t;
    t.down(NavKey::Ok, 0);
    t.up(NavKey::Ok, KeyTiming::kLongPressMs);
    expectOne(t, NavKey::Ok, KeyEvent::LongPress);
}

void test_other_keys_press_on_down_os_repeat_ignored() {
    KeyTracker t;
    t.down(NavKey::PlayPause, 0);
    t.down(NavKey::PlayPause, 30);  // OS auto-repeat: another down while held
    t.down(NavKey::PlayPause, 60);
    TEST_ASSERT_EQUAL(1, (int)run(t, 60, 3000).size());  // only the first Press
    t.up(NavKey::PlayPause, 3000);
    expectNone(t);
    // Same for an arrow: a second down while held is not a second Press
    t.down(NavKey::Left, 4000);
    t.down(NavKey::Left, 4030);
    expectOne(t, NavKey::Left, KeyEvent::Press);
    // Released: a new down is a new Press
    t.up(NavKey::Left, 4040);
    t.down(NavKey::Left, 4050);
    expectOne(t, NavKey::Left, KeyEvent::Press);
}

void test_clock_wraps() {
    KeyTracker t;
    const uint32_t t0 = 0xFFFFFFFFu - 100;  // millis() wraps after 49 days
    t.down(NavKey::Right, t0);
    expectOne(t, NavKey::Right, KeyEvent::Press);
    t.tick(t0 + 50);  // still before the wrap
    t.tick(t0 + 200);  // wrapped: 200 ms held
    expectNone(t);
    t.tick(t0 + KeyTiming::kRepeatDelayMs);
    expectOne(t, NavKey::Right, KeyEvent::Repeat);
    t.up(NavKey::Right, t0 + 500);
    // Ok held over the wrap: long press at 700 ms, not at once and not never
    t.down(NavKey::Ok, t0);
    t.tick(t0 + 300);
    expectNone(t);
    t.tick(t0 + KeyTiming::kLongPressMs);
    expectOne(t, NavKey::Ok, KeyEvent::LongPress);
    // A tick with a time a little before the key-down (clock read earlier) is not a long hold
    KeyTracker u;
    u.down(NavKey::Ok, 5000);
    u.tick(4990);
    u.up(NavKey::Ok, 4995);
    expectOne(u, NavKey::Ok, KeyEvent::Press);
}

void test_two_keys_held() {
    KeyTracker t;
    t.down(NavKey::Up, 0);
    t.down(NavKey::Ok, 100);
    auto v = run(t, 100, 100 + KeyTiming::kLongPressMs);
    // Up: Press, then repeats from 400 ms; Ok: one long press at 800 ms
    int up = 0, ok = 0, upRepeat = 0;
    t.up(NavKey::Up, 900);
    for (auto& e : v) {
        if (e.key == NavKey::Up) ++up, upRepeat += e.kind == KeyEvent::Repeat;
        if (e.key == NavKey::Ok) ++ok, TEST_ASSERT_EQUAL(KeyEvent::LongPress, e.kind);
    }
    TEST_ASSERT_EQUAL(1, ok);
    TEST_ASSERT_EQUAL(up - 1, upRepeat);
    TEST_ASSERT_EQUAL(1 + 1 + (800 - 400) / (int)KeyTiming::kRepeatIntervalMs, up);
    TEST_ASSERT_EQUAL(KeyEvent::Press, v[0].kind);  // the Up press comes first
    // Up released: Ok still held, its long press was sent once
    TEST_ASSERT_TRUE(t.held(NavKey::Ok));
    TEST_ASSERT_FALSE(t.held(NavKey::Up));
    TEST_ASSERT_EQUAL(0, (int)run(t, 900, 2000).size());
    t.up(NavKey::Ok, 2000);
    expectNone(t);
}

void test_up_without_down_and_none() {
    KeyTracker t;
    t.up(NavKey::Ok, 10);
    t.up(NavKey::Left, 10);
    t.down(NavKey::None, 10);
    t.down(NavKey::Text, 10);
    t.post(NavKey::None);
    t.text("");
    t.tick(5000);
    expectNone(t);
}

void test_post_text_cancel_and_bound() {
    KeyTracker t;
    t.post(NavKey::Ok);  // a platform without key-up
    t.post(NavKey::Back, KeyEvent::LongPress);
    t.text("\xC3\xA9t\xC3\xA9");
    auto v = drain(t);
    TEST_ASSERT_EQUAL(3, (int)v.size());
    TEST_ASSERT_TRUE(v[0].key == NavKey::Ok && v[0].kind == KeyEvent::Press);
    TEST_ASSERT_TRUE(v[1].key == NavKey::Back && v[1].kind == KeyEvent::LongPress);
    TEST_ASSERT_TRUE(v[2].key == NavKey::Text);
    TEST_ASSERT_EQUAL_STRING("\xC3\xA9t\xC3\xA9", v[2].text.c_str());
    // Focus lost while a key is held: no endless repeat, no Press on a later up
    t.down(NavKey::Up, 0);
    t.down(NavKey::Ok, 0);
    drain(t);
    t.cancelAll();
    t.tick(5000);
    t.up(NavKey::Ok, 5000);
    expectNone(t);
    // Nobody reads: the queue keeps the newest kMaxQueued events
    for (int i = 0; i < 100; ++i) t.post(i < 99 ? NavKey::Next : NavKey::Prev);
    v = drain(t);
    TEST_ASSERT_EQUAL((int)KeyTiming::kMaxQueued, (int)v.size());
    TEST_ASSERT_TRUE(v.back().key == NavKey::Prev);
}

void test_names() {
    const char* names[] = {"up",   "down",   "left",  "right", "ok",     "back",    "menu",
                           "playpause", "play", "pause", "next",  "prev",   "ffwd",    "rewind",
                           "volup", "voldown", "mute", "search", "backspace", "enter"};
    for (const char* n : names) {
        const NavKey k = navKeyFromName(n);
        TEST_ASSERT_TRUE(k != NavKey::None);
        TEST_ASSERT_EQUAL_STRING(n, navKeyName(k));
    }
    // Every key but None and Text has a name
    for (int k = (int)NavKey::Up; k < (int)NavKey::Text; ++k)
        TEST_ASSERT_TRUE(navKeyFromName(navKeyName((NavKey)k)) == (NavKey)k);
    TEST_ASSERT_TRUE(navKeyFromName("") == NavKey::None);
    TEST_ASSERT_TRUE(navKeyFromName("Up") == NavKey::None);  // lower case only
    TEST_ASSERT_TRUE(navKeyFromName("text") == NavKey::None);
    TEST_ASSERT_TRUE(navKeyFromName("f12") == NavKey::None);
    TEST_ASSERT_EQUAL_STRING("text", navKeyName(NavKey::Text));
    TEST_ASSERT_EQUAL_STRING("none", navKeyName(NavKey::None));
    TEST_ASSERT_EQUAL_STRING("press", keyKindName(KeyEvent::Press));
    TEST_ASSERT_EQUAL_STRING("long", keyKindName(KeyEvent::LongPress));
    TEST_ASSERT_EQUAL_STRING("repeat", keyKindName(KeyEvent::Repeat));
}

}  // namespace

void run_keys_tests() {
    RUN_TEST(test_arrow_press_then_repeat);
    RUN_TEST(test_backspace_repeats_quick_tap_does_not);
    RUN_TEST(test_late_tick_gives_one_repeat_not_a_burst);
    RUN_TEST(test_ok_short_press_on_release);
    RUN_TEST(test_ok_long_press_once_then_no_press);
    RUN_TEST(test_ok_released_late_without_tick_is_long);
    RUN_TEST(test_other_keys_press_on_down_os_repeat_ignored);
    RUN_TEST(test_clock_wraps);
    RUN_TEST(test_two_keys_held);
    RUN_TEST(test_up_without_down_and_none);
    RUN_TEST(test_post_text_cancel_and_bound);
    RUN_TEST(test_names);
}
