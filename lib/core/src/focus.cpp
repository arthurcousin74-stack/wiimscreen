#include "focus.h"

namespace wr {

void TargetList::add(Act act, int arg, const Rect& r, const Rect& hit, uint8_t flags) {
    Target t;
    t.r = r;
    t.hit = hit;
    t.arg = arg;
    t.act = act;
    t.flags = flags;
    items_.push_back(t);
}

const Target* TargetList::hit(int x, int y) const {
    for (const Target& t : items_)
        if (t.hit.contains(x, y)) return &t;
    return nullptr;
}

const Target* TargetList::hit(Act act, int x, int y) const {
    for (const Target& t : items_)
        if (t.act == act && t.hit.contains(x, y)) return &t;
    return nullptr;
}

const Target* TargetList::find(Act act, int arg) const {
    for (const Target& t : items_)
        if (t.act == act && t.arg == arg) return &t;
    return nullptr;
}

// ------------------------------------------------------------------ D-pad moves

namespace {

struct Axis {
    int nearGap;  // from the current rectangle's edge to the candidate's near edge, along the move (may be < 0)
    int offset;   // distance between the centres, across the move
    bool beam;    // the candidate overlaps the current rectangle across the move
};

int centreX(const Rect& r) { return r.x + r.w / 2; }
int centreY(const Rect& r) { return r.y + r.h / 2; }

// Is c beyond `from` in direction d (Android FocusFinder.isCandidate)? Its centre is then on the pressed side.
bool beyond(const Rect& from, const Rect& c, Dir d) {
    switch (d) {
        case Dir::Left: return (from.right() > c.right() || from.x >= c.right()) && from.x > c.x;
        case Dir::Right: return (from.x < c.x || from.right() <= c.x) && from.right() < c.right();
        case Dir::Up: return (from.bottom() > c.bottom() || from.y >= c.bottom()) && from.y > c.y;
        case Dir::Down: return (from.y < c.y || from.bottom() <= c.y) && from.bottom() < c.bottom();
    }
    return false;
}

Axis measure(const Rect& from, const Rect& c, Dir d) {
    Axis a{};
    const bool horizontal = d == Dir::Left || d == Dir::Right;
    switch (d) {
        case Dir::Left: a.nearGap = from.x - c.right(); break;
        case Dir::Right: a.nearGap = c.x - from.right(); break;
        case Dir::Up: a.nearGap = from.y - c.bottom(); break;
        case Dir::Down: a.nearGap = c.y - from.bottom(); break;
    }
    const int dc = horizontal ? centreY(c) - centreY(from) : centreX(c) - centreX(from);
    a.offset = dc < 0 ? -dc : dc;
    a.beam = horizontal ? (c.y < from.bottom() && from.y < c.bottom()) : (c.x < from.right() && from.x < c.right());
    return a;
}

// Android's weighted distance: the gap along the move counts much more than the offset across it
long long score(const Axis& a) {
    const long long g = a.nearGap > 0 ? a.nearGap : 0, o = a.offset;
    return 13 * g * g + o * o;
}

// In the beam beats out of the beam, in all four directions. (Android lets an out-of-beam candidate win Up/Down
// when it lies entirely nearer; here that sent Up from the rail's Search button into the content, far across,
// instead of to Home just above it.)
bool beamBeats(const Axis& a, const Axis& b) { return a.beam && !b.beam; }

bool better(const Axis& a, const Axis& b) {
    if (beamBeats(a, b)) return true;
    if (beamBeats(b, a)) return false;
    return score(a) < score(b);
}

}  // namespace

const Target* defaultTarget(const TargetList& list) {
    const Target* first = nullptr;
    for (const Target& t : list) {
        if (!t.focusable()) continue;
        if (t.flags & kTargetDefault) return &t;
        if (!first) first = &t;
    }
    return first;
}

NavResult nextTarget(const TargetList& list, const Target* current, Dir d) {
    NavResult res;
    if (!current) {
        res.target = defaultTarget(list);
        res.edge = res.target == nullptr;
        return res;
    }
    const Rect& from = current->r;
    const Target* best = nullptr;
    Axis bestAxis{};
    for (const Target& t : list) {
        if (&t == current || !t.focusable() || t.r.w <= 0 || t.r.h <= 0) continue;
        if (!beyond(from, t.r, d)) continue;
        const Axis a = measure(from, t.r, d);
        if (!best || better(a, bestAxis)) {
            best = &t;
            bestAxis = a;
        }
    }
    res.target = best;
    res.edge = best == nullptr;
    return res;
}

}  // namespace wr
