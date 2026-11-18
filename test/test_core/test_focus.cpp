// Tests: tappable elements of a screen (TargetList) (pio test -e native)
#include <unity.h>

#include "focus.h"
#include "test_helpers.h"

using namespace wr;

static Rect pad(const Rect& r, int p) { return Rect{r.x - p, r.y - p, r.w + 2 * p, r.h + 2 * p}; }

static void test_targets_priority() {
    // Insertion order is the priority: a bar added before a padded button wins where they overlap
    TargetList L;
    const Rect bar{100, 100, 200, 20}, play{180, 130, 40, 40};
    L.add(Act::Progress, 0, bar, pad(bar, 10), kTargetNoFocus);
    L.add(Act::Transport, kPlayPause, play, pad(play, 10));
    const Target* t = L.hit(200, 125);  // in both touch areas
    TEST_ASSERT_NOT_NULL(t);
    TEST_ASSERT_TRUE(t->act == Act::Progress);
    // The same point, asked for one kind: the button (what Ui::hitTransport-style lookups do)
    t = L.hit(Act::Transport, 200, 125);
    TEST_ASSERT_NOT_NULL(t);
    TEST_ASSERT_EQUAL(kPlayPause, t->arg);
    // Reverse order: the button wins
    TargetList R;
    R.add(Act::Transport, kPlayPause, play, pad(play, 10));
    R.add(Act::Progress, 0, bar, pad(bar, 10), kTargetNoFocus);
    TEST_ASSERT_TRUE(R.hit(200, 125)->act == Act::Transport);
    // Two padded neighbours that overlap: the first one added wins (as Ui::hitKey did in index order)
    TargetList K;
    K.add(Act::Key, 0, Rect{0, 0, 50, 50}, pad(Rect{0, 0, 50, 50}, 5));
    K.add(Act::Key, 1, Rect{58, 0, 50, 50}, pad(Rect{58, 0, 50, 50}, 5));
    TEST_ASSERT_EQUAL(0, K.hit(54, 10)->arg);
    TEST_ASSERT_EQUAL(1, K.hit(55, 10)->arg);  // only in key 1's area
    TEST_ASSERT_EQUAL(0, K.hit(53, 10)->arg);  // only in key 0's area
}

static void test_targets_padding() {
    // The touch area, not the drawn rectangle, decides; the drawn rectangle is kept for the focus ring
    TargetList L;
    const Rect heart{300, 300, 40, 40};
    L.add(Act::ResultHeart, 7, heart, pad(heart, 6));
    TEST_ASSERT_NOT_NULL(L.hit(295, 300));   // in the padding
    TEST_ASSERT_NOT_NULL(L.hit(345, 345));   // last pixel of the padding (right/bottom edges excluded)
    TEST_ASSERT_NULL(L.hit(346, 300));
    TEST_ASSERT_NULL(L.hit(293, 300));
    const Target* t = L.hit(320, 320);
    TEST_ASSERT_EQUAL(300, t->r.x);
    TEST_ASSERT_EQUAL(40, t->r.w);
    TEST_ASSERT_EQUAL(294, t->hit.x);
    TEST_ASSERT_EQUAL(52, t->hit.w);
    // add() without a touch area: the drawn rectangle is the touch area
    L.add(Act::ResultRow, 7, Rect{0, 0, 10, 10});
    TEST_ASSERT_EQUAL(10, L.find(Act::ResultRow, 7)->hit.w);
    // An empty rectangle is never hit
    L.add(Act::Mic, 0, Rect{});
    TEST_ASSERT_NULL(L.hit(Act::Mic, 0, 0));
}

static void test_targets_find_clear_flags() {
    TargetList L;
    L.reserve(8);
    TEST_ASSERT_TRUE(L.empty());
    TEST_ASSERT_NULL(L.hit(0, 0));
    TEST_ASSERT_NULL(L.find(Act::Home, 0));
    L.add(Act::Home, 0, Rect{0, 0, 60, 60});
    L.add(Act::Dots, -1, Rect{100, 500, 50, 20}, kTargetNoFocus);
    L.add(Act::Dots, 1, Rect{150, 500, 50, 20}, kTargetNoFocus);
    L.add(Act::Transport, kPlayPause, Rect{300, 300, 76, 76}, kTargetDefault);
    TEST_ASSERT_EQUAL(4, (int)L.size());
    // find: kind and arg
    TEST_ASSERT_EQUAL(150, L.find(Act::Dots, 1)->r.x);
    TEST_ASSERT_EQUAL(100, L.find(Act::Dots, -1)->r.x);
    TEST_ASSERT_NULL(L.find(Act::Dots, 0));
    TEST_ASSERT_NULL(L.find(Act::Search, 0));
    // flags
    TEST_ASSERT_TRUE(L.find(Act::Home, 0)->focusable());
    TEST_ASSERT_FALSE(L.find(Act::Dots, 1)->focusable());
    TEST_ASSERT_TRUE(L.find(Act::Transport, kPlayPause)->flags & kTargetDefault);
    TEST_ASSERT_TRUE(L.find(Act::Transport, kPlayPause)->focusable());
    // iteration in insertion order
    int n = 0;
    for (const Target& t : L) {
        if (n == 0) TEST_ASSERT_TRUE(t.act == Act::Home);
        ++n;
    }
    TEST_ASSERT_EQUAL(4, n);
    TEST_ASSERT_TRUE(L[3].act == Act::Transport);
    // clear: nothing left, the capacity stays (no allocation on the next screen)
    L.clear();
    TEST_ASSERT_TRUE(L.empty());
    TEST_ASSERT_NULL(L.hit(10, 10));
    TEST_ASSERT_NULL(L.find(Act::Home, 0));
}

void run_focus_tests() {
    RUN_TEST(test_targets_priority);
    RUN_TEST(test_targets_padding);
    RUN_TEST(test_targets_find_clear_flags);
}
