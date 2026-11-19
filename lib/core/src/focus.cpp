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

namespace {

bool candidate(const Target& t, const Target* current, Dir d) {
    return &t != current && t.focusable() && t.r.w > 0 && t.r.h > 0 && beyond(current->r, t.r, d);
}

// nextTarget() over the targets that `accept` keeps (current != nullptr)
template <class Accept>
NavResult pick(const TargetList& list, const Target* current, Dir d, Accept accept) {
    NavResult res;
    const Rect& from = current->r;
    const Target* best = nullptr;
    Axis bestAxis{};
    for (const Target& t : list) {
        if (!candidate(t, current, d) || !accept(t)) continue;
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

bool onRail(const Target& t) { return t.act == Act::Home || t.act == Act::Search || t.act == Act::Settings; }

}  // namespace

NavResult nextTarget(const TargetList& list, const Target* current, Dir d) {
    if (!current) {
        NavResult res;
        res.target = defaultTarget(list);
        res.edge = res.target == nullptr;
        return res;
    }
    return pick(list, current, d, [](const Target&) { return true; });
}

NavResult railMove(const TargetList& list, const Target* current, Dir d) {
    if (!current) return nextTarget(list, nullptr, d);
    // Rail: a column (same centre x) on the left, else a row on top
    const Target* firstRail = nullptr;
    bool column = true;
    for (const Target& t : list) {
        if (!onRail(t)) continue;
        if (!firstRail) firstRail = &t;
        else if (centreX(t.r) != centreX(firstRail->r)) column = false;
    }
    const Dir toRail = column ? Dir::Left : Dir::Up, awayFromRail = column ? Dir::Right : Dir::Down;
    auto railOnly = [](const Target& t) { return onRail(t); };
    if (onRail(*current)) {
        if (d == toRail) return NavResult{nullptr, true};
        if (d == awayFromRail) return pick(list, current, d, [](const Target& t) { return !onRail(t); });
        return pick(list, current, d, railOnly);
    }
    const Rect& from = current->r;
    NavResult res;
    if (d == Dir::Left || d == Dir::Right) {
        res = pick(list, current, d, [&](const Target& t) { return !onRail(t) && measure(from, t.r, d).beam; });
    } else {
        // Nearest row beyond: the candidate whose near edge is nearest, and those that start before its far edge
        const Target* nearest = nullptr;
        int nearestGap = 0;
        for (const Target& t : list) {
            if (!candidate(t, current, d) || onRail(t)) continue;
            const int g = measure(from, t.r, d).nearGap;
            if (!nearest || g < nearestGap) nearest = &t, nearestGap = g;
        }
        if (nearest) {
            const Rect& n = nearest->r;
            res = pick(list, current, d, [&](const Target& t) {
                return !onRail(t) && (d == Dir::Down ? t.r.y < n.bottom() : t.r.bottom() > n.y);
            });
        } else {
            res = NavResult{nullptr, true};
        }
    }
    if (res.target || d != toRail || !firstRail) return res;
    return pick(list, current, d, railOnly);
}

int pageEdgeMove(int index, int perPage, int count, Dir d) {
    if (perPage < 1 || index < 0 || index >= count) return -1;
    const int first = index / perPage * perPage;
    if (d == Dir::Down) {
        const int next = first + perPage;
        const bool last = index == next - 1 || index == count - 1;
        return last && next < count ? next : -1;
    }
    if (d == Dir::Up) return index == first && first > 0 ? first - 1 : -1;
    return -1;
}

const char* actName(Act act) {
    static const char* const kNames[] = {"none", "home", "search", "settings", "header", "progress", "volume", "mute",
                                         "dots", "preset", "strip", "fav", "transport", "ambientexit", "menuchoice",
                                         "pickdevice", "pickretry", "picksetup", "logclose", "logqueue", "touchclose",
                                         "source", "mic", "key", "resultrow", "resultheart", "resultspage",
                                         "resultsedit", "updatecancel"};
    static_assert(sizeof kNames / sizeof kNames[0] == (size_t)Act::UpdateCancel + 1, "one name per Act");
    const size_t i = (size_t)act;
    return i < sizeof kNames / sizeof kNames[0] ? kNames[i] : "?";
}

const Target* focusedTarget(const TargetList& list, const FocusSpot& spot) {
    const Target* t = spot.act == Act::None ? nullptr : list.find(spot.act, spot.arg);
    return t && t->focusable() ? t : defaultTarget(list);
}

}  // namespace wr
