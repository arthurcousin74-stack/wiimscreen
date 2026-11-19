// Tests: tappable elements of a screen (TargetList) (pio test -e native)
#include <unity.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "focus.h"
#include "i18n.h"
#include "layout.h"
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

// ------------------------------------------------------------------ D-pad moves (nextTarget, defaultTarget)

namespace {

int scOf(int W, int H, int v) { return v * std::min(W, H) / 540; }

Rect navHitOf(const Rect& rail, const Rect& b, int pad) {
    if (rail.h > rail.w) return Rect{rail.x, b.y - pad, rail.w, b.h + 2 * pad};
    return Rect{b.x - pad, rail.y, b.w + 2 * pad, rail.h};
}

void addRail(TargetList& L, const Layout& lay, int W, int H) {
    const int p = scOf(W, H, 16);
    L.add(Act::Home, 0, lay.navHome, navHitOf(lay.rail, lay.navHome, p));
    L.add(Act::Search, 0, lay.navSearch, navHitOf(lay.rail, lay.navSearch, p));
    L.add(Act::Settings, 0, lay.navSettings, navHitOf(lay.rail, lay.navSettings, p));
}

// Controls view, in the order of Ui::render (navRail) then Ui::controlsTargets, from computeLayout. The progress
// touch area is approximated by layout.progress (Ui computes it while drawing; it is NoFocus, so moves ignore it).
TargetList controlsList(int W, int H, int presets) {
    const Layout lay = computeLayout(W, H, presets);
    TargetList L;
    addRail(L, lay, W, H);
    const int pad = scOf(W, H, 10);
    L.add(Act::Volume, 0, lay.volBar, kTargetNoFocus);
    L.add(Act::Mute, 0, lay.mute);
    L.add(Act::Progress, 0, lay.progress, kTargetNoFocus);
    L.add(Act::Header, 0, lay.art, kTargetNoFocus);
    L.add(Act::Header, 1, lay.info, kTargetNoFocus);
    const int pages = lay.perPage > 0 ? (presets + lay.perPage - 1) / lay.perPage : 0;
    if (pages >= 2) {
        const Rect& d = lay.dots;
        const int y = d.y - scOf(W, H, 6), h = d.h + scOf(W, H, 12), half = d.w / 2;
        L.add(Act::Dots, -1, Rect{d.x, y, half, h}, kTargetNoFocus);
        L.add(Act::Dots, 1, Rect{d.x + half, y, d.w - half, h}, kTargetNoFocus);
    }
    for (size_t slot = 0; slot < lay.presets.size(); ++slot) L.add(Act::Preset, (int)slot, lay.presets[slot]);
    if (presets > 0) L.add(Act::Strip, 0, lay.strip, kTargetNoFocus);
    const Rect& f = lay.fav;
    L.add(Act::Fav, 0, f, Rect{f.x - pad, f.y - pad, f.w + 2 * pad, f.h + 2 * pad});
    for (int i = 0; i < kTransportCount; ++i) {
        const Rect& r = lay.transport[i];
        const bool vol = i == kVolDown || i == kVolUp;
        L.add(Act::Transport, i, r, vol ? r : Rect{r.x - pad, r.y - pad, r.w + 2 * pad, r.h + 2 * pad},
              i == kPlayPause ? kTargetDefault : 0);
    }
    return L;
}

// Search keyboard, in the order of Ui::renderSearch: rail, mic, source chips (Qobuz | Deezer), keys from
// keyboardLayout. The field, mic and chips follow Ui::searchField's formulas; the status line height (a font
// height in Ui) is approximated by sc(26).
TargetList searchList(int W, int H, std::vector<Key>* keysOut = nullptr) {
    const Layout lay = computeLayout(W, H, 0);
    auto sc = [&](int v) { return scOf(W, H, v); };
    TargetList L;
    addRail(L, lay, W, H);
    const Rect& A = lay.content;
    const int m = sc(18), X = A.x + m, Wd = A.w - 2 * m, fh = sc(64), fy = A.y + m;
    const int d = fh - sc(10);
    L.add(Act::Mic, 0, Rect{X + Wd - sc(5) - d, fy + sc(5), d, d});
    const int n = 2, gap = sc(8), ch = sc(40), cy = fy + fh + sc(6);
    const int cw = std::min(sc(124), (Wd * 60 / 100 - (n - 1) * gap) / n);
    for (int k = 0; k < n; ++k) L.add(Act::Source, k, Rect{X + Wd - (n - k) * cw - (n - 1 - k) * gap, cy, cw, ch});
    const int areaBottom = fy + fh + sc(10) + sc(26) + sc(14);
    const std::vector<Key> keys = keyboardLayout(Rect{A.x + m, areaBottom, A.w - 2 * m, A.bottom() - m - areaBottom});
    const int pad = sc(4);
    for (size_t i = 0; i < keys.size(); ++i) {
        const Rect& r = keys[i].r;
        L.add(Act::Key, (int)i, r, Rect{r.x - pad, r.y - pad, r.w + 2 * pad, r.h + 2 * pad}, i == 10 ? kTargetDefault : 0);
    }
    if (keysOut) *keysOut = keys;
    return L;
}

bool isRail(const Target* t) { return t && (t->act == Act::Home || t->act == Act::Search || t->act == Act::Settings); }
int cx(const Target* t) { return t->r.x + t->r.w / 2; }
int cy(const Target* t) { return t->r.y + t->r.h / 2; }
const Dir kDirs[] = {Dir::Up, Dir::Down, Dir::Left, Dir::Right};
Dir opposite(Dir d) {
    switch (d) {
        case Dir::Up: return Dir::Down;
        case Dir::Down: return Dir::Up;
        case Dir::Left: return Dir::Right;
        case Dir::Right: return Dir::Left;
    }
    return d;
}

struct NavCounts {
    int lists = 0, moves = 0, directionChecked = 0, reached = 0, returns = 0, chains = 0;
};

// The properties that hold for every list: each move goes in its direction, never to a NoFocus target, nor to
// itself; a chain of presses in one direction ends at an edge within size() steps; every focusable target is
// reachable from the default one; within one row (same centre and size across the move) the opposite key returns.
using MoveFn = NavResult (*)(const TargetList&, const Target*, Dir);

void checkList(const TargetList& L, const char* what, NavCounts& c, MoveFn move = nextTarget) {
    char msg[160];
    ++c.lists;
    const Target* def = defaultTarget(L);
    TEST_ASSERT_NOT_NULL_MESSAGE(def, what);
    TEST_ASSERT_TRUE_MESSAGE(def->focusable(), what);
    for (const Target& t : L) {
        if (!t.focusable()) continue;
        for (Dir d : kDirs) {
            const NavResult r = move(L, &t, d);
            ++c.moves;
            snprintf(msg, sizeof msg, "%s: act %d arg %d dir %d", what, (int)t.act, t.arg, (int)d);
            TEST_ASSERT_EQUAL_MESSAGE(r.target == nullptr, r.edge, msg);
            if (!r.target) continue;
            TEST_ASSERT_TRUE_MESSAGE(r.target->focusable(), msg);
            TEST_ASSERT_TRUE_MESSAGE(r.target != &t, msg);
            const bool ok = d == Dir::Left    ? cx(r.target) < cx(&t)
                            : d == Dir::Right ? cx(r.target) > cx(&t)
                            : d == Dir::Up    ? cy(r.target) < cy(&t)
                                              : cy(r.target) > cy(&t);
            TEST_ASSERT_TRUE_MESSAGE(ok, msg);
            ++c.directionChecked;
            // Same row (Left/Right) or column (Up/Down) of equal-size elements: the opposite key comes back
            const Rect &a = t.r, &b = r.target->r;
            const bool horizontal = d == Dir::Left || d == Dir::Right;
            const bool sameLine = horizontal ? (a.y == b.y && a.h == b.h && a.w == b.w) : (a.x == b.x && a.w == b.w && a.h == b.h);
            if (sameLine) {
                TEST_ASSERT_TRUE_MESSAGE(move(L, r.target, opposite(d)).target == &t, msg);
                ++c.returns;
            }
        }
        for (Dir d : kDirs) {  // no endless chain
            const Target* cur = &t;
            size_t steps = 0;
            for (;;) {
                const NavResult r = move(L, cur, d);
                if (r.edge) break;
                cur = r.target;
                TEST_ASSERT_TRUE_MESSAGE(++steps <= L.size(), what);
            }
            ++c.chains;
        }
    }
    // Reachability from the default target (breadth-first over the four keys)
    std::vector<const Target*> seen{def}, todo{def};
    while (!todo.empty()) {
        const Target* t = todo.back();
        todo.pop_back();
        for (Dir d : kDirs) {
            const Target* n = move(L, t, d).target;
            if (n && std::find(seen.begin(), seen.end(), n) == seen.end()) {
                seen.push_back(n);
                todo.push_back(n);
            }
        }
    }
    for (const Target& t : L) {
        if (!t.focusable()) continue;
        snprintf(msg, sizeof msg, "%s: act %d arg %d not reachable", what, (int)t.act, t.arg);
        TEST_ASSERT_TRUE_MESSAGE(std::find(seen.begin(), seen.end(), &t) != seen.end(), msg);
        ++c.reached;
    }
}

// The first focusable non-rail target that has none beyond it in direction d (the edge of the content)
bool contentEdge(const TargetList& L, const Target& t, Dir d) {
    for (const Target& o : L) {
        if (&o == &t || !o.focusable() || isRail(&o)) continue;
        const bool beyond = d == Dir::Left ? o.r.x < t.r.x && o.r.right() < t.r.right()
                                           : o.r.y < t.r.y && o.r.bottom() < t.r.bottom();
        if (beyond) return false;
    }
    return true;
}

// Explicit rule: from the content's left edge, Left reaches the rail button nearest across (landscape: rail on the
// left); in portrait (rail on top) Up from the content's top edge reaches a rail button.
void checkRailRule(const TargetList& L, bool landscape, const char* what, int& count) {
    for (const Target& t : L) {
        if (!t.focusable() || isRail(&t)) continue;
        const Dir d = landscape ? Dir::Left : Dir::Up;
        if (!contentEdge(L, t, d)) continue;
        const Target* r = nextTarget(L, &t, d).target;
        TEST_ASSERT_TRUE_MESSAGE(isRail(r), what);
        if (landscape) {  // the nearest rail button across the move
            int best = 1 << 30;
            for (const Target& o : L)
                if (isRail(&o)) best = std::min(best, std::abs(cy(&o) - cy(&t)));
            TEST_ASSERT_EQUAL_MESSAGE(best, std::abs(cy(r) - cy(&t)), what);
        }
        ++count;
    }
}

}  // namespace

static void test_nav_empty_single() {
    TargetList L;
    TEST_ASSERT_NULL(defaultTarget(L));
    NavResult r = nextTarget(L, nullptr, Dir::Right);
    TEST_ASSERT_NULL(r.target);
    TEST_ASSERT_TRUE(r.edge);
    // Only NoFocus targets: nothing to focus
    L.add(Act::Strip, 0, Rect{0, 0, 100, 100}, kTargetNoFocus);
    TEST_ASSERT_NULL(defaultTarget(L));
    TEST_ASSERT_TRUE(nextTarget(L, nullptr, Dir::Down).edge);
    // One focusable target: it is the default; every move from it is an edge
    L.add(Act::Home, 0, Rect{10, 10, 50, 50});
    TEST_ASSERT_TRUE(defaultTarget(L) == &L[1]);
    r = nextTarget(L, nullptr, Dir::Up);
    TEST_ASSERT_TRUE(r.target == &L[1] && !r.edge);
    for (Dir d : kDirs) {
        r = nextTarget(L, &L[1], d);
        TEST_ASSERT_NULL(r.target);
        TEST_ASSERT_TRUE(r.edge);
    }
    // The Default flag wins over the first focusable target
    L.add(Act::Transport, kPlayPause, Rect{200, 10, 50, 50}, kTargetDefault);
    TEST_ASSERT_TRUE(defaultTarget(L) == &L[2]);
    TEST_ASSERT_TRUE(nextTarget(L, &L[1], Dir::Right).target == &L[2]);
    TEST_ASSERT_TRUE(nextTarget(L, &L[2], Dir::Left).target == &L[1]);
}

static void test_nav_rules() {
    // In the beam wins over a nearer target that is not in it
    TargetList L;
    L.add(Act::Key, 0, Rect{0, 100, 40, 40});
    L.add(Act::Key, 1, Rect{60, 150, 40, 40});   // nearer, below the beam
    L.add(Act::Key, 2, Rect{300, 110, 40, 40});  // far, in the beam
    TEST_ASSERT_EQUAL(2, nextTarget(L, &L[0], Dir::Right).target->arg);
    // Up / Down too, even when the other one lies entirely nearer (rail column: Search -> Home, not the content)
    TargetList V;
    V.add(Act::Search, 0, Rect{0, 300, 60, 60});
    V.add(Act::Home, 0, Rect{0, 20, 60, 60});      // above, in the beam, far
    V.add(Act::Mute, 0, Rect{400, 230, 40, 40});   // above, not in the beam, entirely nearer
    TEST_ASSERT_TRUE(nextTarget(V, &V[0], Dir::Up).target->act == Act::Home);
    // Out of the beam: 13 x gap^2 along the move + offset^2 across it
    TargetList S;
    S.add(Act::Key, 0, Rect{0, 0, 40, 40});
    S.add(Act::Key, 1, Rect{100, 60, 40, 40});   // gap 60, offset 60: 50400
    S.add(Act::Key, 2, Rect{200, -45, 40, 40});  // gap 160, offset 45: 334825
    TEST_ASSERT_EQUAL(1, nextTarget(S, &S[0], Dir::Right).target->arg);
    // Both in the beam: the nearer row wins over a better-centred one further away (keyboard: from "l", Down is
    // the Backspace key below it, not the Search key two rows down)
    TargetList B;
    B.add(Act::Key, 0, Rect{1037, 386, 105, 97});
    B.add(Act::Key, 1, Rect{1037, 491, 218, 97});
    B.add(Act::Key, 2, Rect{923, 596, 333, 97});
    TEST_ASSERT_EQUAL(1, nextTarget(B, &B[0], Dir::Down).target->arg);
    // NoFocus targets are never returned, even in the beam
    TargetList N;
    N.add(Act::Home, 0, Rect{0, 0, 40, 40});
    N.add(Act::Progress, 0, Rect{50, 0, 400, 40}, kTargetNoFocus);
    TEST_ASSERT_TRUE(nextTarget(N, &N[0], Dir::Right).edge);
    // A target overlapping the current one is a candidate only if it reaches further in the direction
    TargetList O;
    O.add(Act::Key, 0, Rect{0, 0, 100, 40});
    O.add(Act::Key, 1, Rect{50, 0, 100, 40});
    TEST_ASSERT_EQUAL(1, nextTarget(O, &O[0], Dir::Right).target->arg);
    TEST_ASSERT_EQUAL(0, nextTarget(O, &O[1], Dir::Left).target->arg);
    TEST_ASSERT_TRUE(nextTarget(O, &O[0], Dir::Left).edge);
}

static void test_nav_layouts() {
    NavCounts c;
    int railChecks = 0;
    const int sizes[][2] = {{1280, 720}, {720, 1280}, {960, 540}, {800, 480}};
    for (auto& s : sizes) {
        const int W = s[0], H = s[1];
        const bool landscape = W > H;
        char what[64];
        for (int presets : {0, 3, 6, 12}) {
            snprintf(what, sizeof what, "controls %dx%d %d presets", W, H, presets);
            const TargetList L = controlsList(W, H, presets);
            checkList(L, what, c);
            checkRailRule(L, landscape, what, railChecks);
            TEST_ASSERT_TRUE(defaultTarget(L)->act == Act::Transport && defaultTarget(L)->arg == kPlayPause);
        }
        for (Lang lg : {Lang::En, Lang::Fr}) {
            setLang(lg);
            snprintf(what, sizeof what, "keyboard %dx%d %s", W, H, langCode(lg));
            std::vector<Key> keys;
            const TargetList L = searchList(W, H, &keys);
            checkList(L, what, c);
            checkRailRule(L, landscape, what, railChecks);
            // Up / Down on the keyboard: the adjacent row
            std::vector<int> rowY;
            for (const Key& k : keys)
                if (std::find(rowY.begin(), rowY.end(), k.r.y) == rowY.end()) rowY.push_back(k.r.y);
            TEST_ASSERT_EQUAL(5, (int)rowY.size());
            for (const Target& t : L) {
                if (t.act != Act::Key) continue;
                const int row = (int)(std::find(rowY.begin(), rowY.end(), t.r.y) - rowY.begin());
                if (row < 4) {
                    const Target* d = nextTarget(L, &t, Dir::Down).target;
                    TEST_ASSERT_TRUE_MESSAGE(d && d->act == Act::Key && d->r.y == rowY[row + 1], what);
                }
                if (row > 0) {
                    const Target* u = nextTarget(L, &t, Dir::Up).target;
                    TEST_ASSERT_TRUE_MESSAGE(u && u->act == Act::Key && u->r.y == rowY[row - 1], what);
                }
            }
            // Left / Right along a row of letters: the neighbour, and back
            const Target* q = L.find(Act::Key, 10);
            TEST_ASSERT_EQUAL(11, nextTarget(L, q, Dir::Right).target->arg);
            TEST_ASSERT_EQUAL(10, nextTarget(L, L.find(Act::Key, 11), Dir::Left).target->arg);
        }
        setLang(Lang::En);
    }
    // Every list and every move was checked (the counts guard against a loop that checks nothing)
    TEST_ASSERT_EQUAL(4 * 4 + 4 * 2, c.lists);
    TEST_ASSERT_TRUE(c.returns > 100);
    TEST_ASSERT_TRUE(railChecks >= 24);
    printf("nav: %d lists, %d moves (%d in their direction), %d returns, %d chains, %d reached, %d rail rules\n",
           c.lists, c.moves, c.directionChecked, c.returns, c.chains, c.reached, railChecks);
}

static void test_nav_transport_row() {
    // Controls (Tab5 landscape): ♥, previous, play, next in one row; volume row below
    const TargetList L = controlsList(1280, 720, 6);
    const Target* play = L.find(Act::Transport, kPlayPause);
    const Target* prev = nextTarget(L, play, Dir::Left).target;
    TEST_ASSERT_TRUE(prev->act == Act::Transport && prev->arg == kPrev);
    TEST_ASSERT_TRUE(nextTarget(L, prev, Dir::Right).target == play);
    const Target* fav = nextTarget(L, prev, Dir::Left).target;
    TEST_ASSERT_TRUE(fav->act == Act::Fav);
    TEST_ASSERT_TRUE(nextTarget(L, fav, Dir::Right).target == prev);
    const Target* next = nextTarget(L, play, Dir::Right).target;
    TEST_ASSERT_TRUE(next->act == Act::Transport && next->arg == kNext);
    TEST_ASSERT_TRUE(nextTarget(L, next, Dir::Left).target == play);
    // From the tiles, Up reaches the volume row, not the header (NoFocus)
    const Target* tile = L.find(Act::Preset, 3);
    const Target* up = nextTarget(L, tile, Dir::Up).target;
    TEST_ASSERT_TRUE(up->act == Act::Transport || up->act == Act::Mute);
    // Tile row: neighbours, and back
    const Target* t1 = L.find(Act::Preset, 1);
    TEST_ASSERT_EQUAL(2, nextTarget(L, t1, Dir::Right).target->arg);
    TEST_ASSERT_TRUE(nextTarget(L, L.find(Act::Preset, 2), Dir::Left).target == t1);
    // Right from the last tile of the page: edge (the app turns it into the next page)
    TEST_ASSERT_TRUE(nextTarget(L, L.find(Act::Preset, 5), Dir::Right).edge);
}

// Settings menu at 1280x720 (rail, six choices: five rows and Close), rectangles of Ui::renderMenu on the Tab5
static TargetList menuList() {
    const Layout lay = computeLayout(1280, 720, 6);
    TargetList L;
    addRail(L, lay, 1280, 720);
    for (int i = 0; i < 5; ++i) L.add(Act::MenuChoice, i, Rect{317, 106 + 99 * i, 746, 89}, i == 0 ? kTargetDefault : 0);
    L.add(Act::MenuChoice, 5, Rect{590, 609, 200, 58});
    return L;
}

static void test_rail_moves_controls() {
    // Controls on the Tab5 (1280x720, 6 presets): the moves the plain rule got wrong, and the path of the plan
    const TargetList L = controlsList(1280, 720, 6);
    auto at = [&](Act a, int arg) { return L.find(a, arg); };
    const Target* play = at(Act::Transport, kPlayPause);
    const Target* prev = at(Act::Transport, kPrev);
    const Target* next = at(Act::Transport, kNext);
    const Target* fav = at(Act::Fav, 0);
    const Target* mute = at(Act::Mute, 0);
    const Target* volDown = at(Act::Transport, kVolDown);
    const Target* volUp = at(Act::Transport, kVolUp);
    // Down from a tile: nothing (was: the rail's Settings button)
    for (int i = 0; i < 6; ++i) TEST_ASSERT_TRUE(railMove(L, at(Act::Preset, i), Dir::Down).edge);
    // Up from Previous / Next / play / heart: only the header (NoFocus) is above (was: Home)
    for (const Target* t : {prev, next, play, fav}) TEST_ASSERT_TRUE(railMove(L, t, Dir::Up).edge);
    // Left from vol-: the rail button nearest across, Search (was: a tile below)
    TEST_ASSERT_TRUE(railMove(L, volDown, Dir::Left).target == at(Act::Search, 0));
    // Path: rail -> heart -> previous -> play -> next; play -> volume row -> tiles
    TEST_ASSERT_TRUE(railMove(L, at(Act::Search, 0), Dir::Right).target == fav);
    TEST_ASSERT_TRUE(railMove(L, fav, Dir::Right).target == prev);
    TEST_ASSERT_TRUE(railMove(L, prev, Dir::Right).target == play);
    TEST_ASSERT_TRUE(railMove(L, play, Dir::Right).target == next);
    TEST_ASSERT_TRUE(railMove(L, next, Dir::Right).edge);
    TEST_ASSERT_TRUE(railMove(L, play, Dir::Down).target == mute);  // the volume row, not a tile under it
    TEST_ASSERT_TRUE(railMove(L, mute, Dir::Left).target == volDown);
    TEST_ASSERT_TRUE(railMove(L, mute, Dir::Right).target == volUp);  // over the volume bar (NoFocus)
    TEST_ASSERT_TRUE(railMove(L, volUp, Dir::Left).target == mute);
    TEST_ASSERT_TRUE(railMove(L, volUp, Dir::Right).edge);
    const Target* tile = railMove(L, mute, Dir::Down).target;
    TEST_ASSERT_TRUE(tile && tile->act == Act::Preset);
    const Target* back = railMove(L, tile, Dir::Up).target;
    TEST_ASSERT_TRUE(back == mute || back == volDown || back == volUp);  // the volume row again
    // Tiles: neighbours; the last one is an edge (the app turns it into the next page); the first one -> rail
    TEST_ASSERT_TRUE(railMove(L, at(Act::Preset, 1), Dir::Right).target == at(Act::Preset, 2));
    TEST_ASSERT_TRUE(railMove(L, at(Act::Preset, 5), Dir::Right).edge);
    TEST_ASSERT_TRUE(railMove(L, at(Act::Preset, 0), Dir::Left).target == at(Act::Settings, 0));
    // Heart: Left -> rail (Search, in its row)
    TEST_ASSERT_TRUE(railMove(L, fav, Dir::Left).target == at(Act::Search, 0));
    // On the rail: along it, the rail only; Left: nothing; Right from Settings: the first tile (same row)
    TEST_ASSERT_TRUE(railMove(L, at(Act::Search, 0), Dir::Up).target == at(Act::Home, 0));
    TEST_ASSERT_TRUE(railMove(L, at(Act::Search, 0), Dir::Down).target == at(Act::Settings, 0));
    TEST_ASSERT_TRUE(railMove(L, at(Act::Settings, 0), Dir::Down).edge);
    TEST_ASSERT_TRUE(railMove(L, at(Act::Home, 0), Dir::Up).edge);
    TEST_ASSERT_TRUE(railMove(L, at(Act::Home, 0), Dir::Left).edge);
    TEST_ASSERT_TRUE(railMove(L, at(Act::Settings, 0), Dir::Right).target == at(Act::Preset, 0));
    // No current target: the default one (play)
    TEST_ASSERT_TRUE(railMove(L, nullptr, Dir::Up).target == play);
}

static void test_rail_moves_menu_and_properties() {
    // Settings menu: rows up and down, Close last; Left -> rail; Down from Close: nothing (not the Settings button)
    const TargetList M = menuList();
    TEST_ASSERT_EQUAL(1, railMove(M, M.find(Act::MenuChoice, 0), Dir::Down).target->arg);
    TEST_ASSERT_EQUAL(5, railMove(M, M.find(Act::MenuChoice, 4), Dir::Down).target->arg);
    TEST_ASSERT_TRUE(railMove(M, M.find(Act::MenuChoice, 5), Dir::Down).edge);
    TEST_ASSERT_EQUAL(4, railMove(M, M.find(Act::MenuChoice, 5), Dir::Up).target->arg);
    TEST_ASSERT_TRUE(railMove(M, M.find(Act::MenuChoice, 0), Dir::Up).edge);
    TEST_ASSERT_TRUE(railMove(M, M.find(Act::MenuChoice, 0), Dir::Left).target->act == Act::Home);
    TEST_ASSERT_TRUE(railMove(M, M.find(Act::MenuChoice, 2), Dir::Right).edge);
    // The general properties (direction, no NoFocus, chains end, every target reachable, same-row returns) hold
    // for railMove on every controls layout, on the keyboard and on the menu
    NavCounts c;
    char what[64];
    const int sizes[][2] = {{1280, 720}, {720, 1280}, {960, 540}, {800, 480}};
    for (auto& s : sizes) {
        const bool landscape = s[0] > s[1];
        for (int presets : {0, 3, 6, 12}) {
            snprintf(what, sizeof what, "railMove controls %dx%d %d presets", s[0], s[1], presets);
            const TargetList L = controlsList(s[0], s[1], presets);
            checkList(L, what, c, railMove);
            // Never from the content into the rail, except toward it
            for (const Target& t : L) {
                if (!t.focusable() || isRail(&t)) continue;
                for (Dir d : kDirs) {
                    const Target* n = railMove(L, &t, d).target;
                    if (isRail(n)) TEST_ASSERT_TRUE_MESSAGE(d == (landscape ? Dir::Left : Dir::Up), what);
                }
            }
        }
        snprintf(what, sizeof what, "railMove keyboard %dx%d", s[0], s[1]);
        checkList(searchList(s[0], s[1]), what, c, railMove);
    }
    checkList(M, "railMove menu", c, railMove);
    TEST_ASSERT_EQUAL(4 * 4 + 4 + 1, c.lists);
}

static void test_focused_target() {
    const TargetList L = controlsList(1280, 720, 6);
    FocusSpot s;  // nothing remembered: the default (play)
    TEST_ASSERT_TRUE(focusedTarget(L, s) == L.find(Act::Transport, kPlayPause));
    s.act = Act::Preset, s.arg = 4;
    TEST_ASSERT_TRUE(focusedTarget(L, s) == L.find(Act::Preset, 4));
    s.arg = 9;  // a tile of another page: gone after the render, back to the default
    TEST_ASSERT_TRUE(focusedTarget(L, s) == L.find(Act::Transport, kPlayPause));
    s.act = Act::Header, s.arg = 0;  // NoFocus: never focused
    TEST_ASSERT_TRUE(focusedTarget(L, s) == L.find(Act::Transport, kPlayPause));
    TargetList E;
    TEST_ASSERT_NULL(focusedTarget(E, s));
    // Names for logs (tools/check.sh greps them)
    TEST_ASSERT_EQUAL_STRING("transport", actName(Act::Transport));
    TEST_ASSERT_EQUAL_STRING("menuchoice", actName(Act::MenuChoice));
    TEST_ASSERT_EQUAL_STRING("updatecancel", actName(Act::UpdateCancel));
    TEST_ASSERT_EQUAL_STRING("none", actName(Act::None));
    TEST_ASSERT_EQUAL_STRING("?", actName(static_cast<Act>(200)));
}

void run_focus_tests() {
    RUN_TEST(test_targets_priority);
    RUN_TEST(test_targets_padding);
    RUN_TEST(test_targets_find_clear_flags);
    RUN_TEST(test_nav_empty_single);
    RUN_TEST(test_nav_rules);
    RUN_TEST(test_nav_layouts);
    RUN_TEST(test_nav_transport_row);
    RUN_TEST(test_rail_moves_controls);
    RUN_TEST(test_rail_moves_menu_and_properties);
    RUN_TEST(test_focused_target);
}
