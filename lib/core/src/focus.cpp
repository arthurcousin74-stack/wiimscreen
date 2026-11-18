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

}  // namespace wr
