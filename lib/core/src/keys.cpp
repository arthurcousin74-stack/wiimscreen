#include "keys.h"

#include <algorithm>

#include "appstate.h"

namespace wr {

namespace {

bool repeats(NavKey k) {
    return k == NavKey::Up || k == NavKey::Down || k == NavKey::Left || k == NavKey::Right || k == NavKey::Backspace ||
           k == NavKey::VolUp || k == NavKey::VolDown;
}

// At least `ms` since `since` (expired() is "more than")
bool reached(uint32_t now, uint32_t since, uint32_t ms) { return ms == 0 || expired(now, since, ms - 1); }

struct Name {
    NavKey key;
    const char* name;
};
const Name kNames[] = {
    {NavKey::Up, "up"},           {NavKey::Down, "down"},       {NavKey::Left, "left"},
    {NavKey::Right, "right"},     {NavKey::Ok, "ok"},           {NavKey::Back, "back"},
    {NavKey::Menu, "menu"},       {NavKey::PlayPause, "playpause"}, {NavKey::Play, "play"},
    {NavKey::Pause, "pause"},     {NavKey::Next, "next"},       {NavKey::Prev, "prev"},
    {NavKey::FastFwd, "ffwd"},    {NavKey::Rewind, "rewind"},   {NavKey::VolUp, "volup"},
    {NavKey::VolDown, "voldown"}, {NavKey::Mute, "mute"},       {NavKey::Search, "search"},
    {NavKey::Backspace, "backspace"}, {NavKey::Enter, "enter"},
};

}  // namespace

void KeyTracker::push(KeyEvent ev) {
    if (out_.size() >= KeyTiming::kMaxQueued) out_.pop_front();
    out_.push_back(std::move(ev));
}

bool KeyTracker::held(NavKey key) const {
    return std::any_of(held_.begin(), held_.end(), [key](const Held& h) { return h.key == key; });
}

void KeyTracker::down(NavKey key, uint32_t nowMs) {
    if (key == NavKey::None || key == NavKey::Text || held(key)) return;
    held_.push_back(Held{key, nowMs, nowMs, false, false});
    if (key != NavKey::Ok) push(KeyEvent{key, KeyEvent::Press, {}});
}

void KeyTracker::up(NavKey key, uint32_t nowMs) {
    auto it = std::find_if(held_.begin(), held_.end(), [key](const Held& h) { return h.key == key; });
    if (it == held_.end()) return;
    if (key == NavKey::Ok && !it->longSent)
        push(KeyEvent{key, reached(nowMs, it->downAt, KeyTiming::kLongPressMs) ? KeyEvent::LongPress : KeyEvent::Press, {}});
    held_.erase(it);
}

void KeyTracker::tick(uint32_t nowMs) {
    for (Held& h : held_) {
        if (h.key == NavKey::Ok) {
            if (!h.longSent && reached(nowMs, h.downAt, KeyTiming::kLongPressMs)) {
                h.longSent = true;
                push(KeyEvent{h.key, KeyEvent::LongPress, {}});
            }
        } else if (repeats(h.key)) {
            // One Repeat per tick at most; a late tick (busy app) skips the missed ones instead of a burst
            const uint32_t wait = h.repeating ? KeyTiming::kRepeatIntervalMs : KeyTiming::kRepeatDelayMs;
            const uint32_t from = h.repeating ? h.lastRepeatAt : h.downAt;
            if (!reached(nowMs, from, wait)) continue;
            h.lastRepeatAt = from + wait;
            if (reached(nowMs, h.lastRepeatAt, KeyTiming::kRepeatIntervalMs)) h.lastRepeatAt = nowMs;
            h.repeating = true;
            push(KeyEvent{h.key, KeyEvent::Repeat, {}});
        }
    }
}

void KeyTracker::post(NavKey key, KeyEvent::Kind kind) {
    if (key == NavKey::None || key == NavKey::Text) return;
    push(KeyEvent{key, kind, {}});
}

void KeyTracker::text(const std::string& utf8) {
    if (!utf8.empty()) push(KeyEvent{NavKey::Text, KeyEvent::Press, utf8});
}

void KeyTracker::cancelAll() { held_.clear(); }

bool KeyTracker::take(KeyEvent& out) {
    if (out_.empty()) return false;
    out = std::move(out_.front());
    out_.pop_front();
    return true;
}

NavKey navKeyFromName(const std::string& name) {
    for (const Name& n : kNames)
        if (name == n.name) return n.key;
    return NavKey::None;
}

const char* navKeyName(NavKey key) {
    if (key == NavKey::Text) return "text";
    for (const Name& n : kNames)
        if (key == n.key) return n.name;
    return "none";
}

const char* keyKindName(KeyEvent::Kind kind) {
    switch (kind) {
        case KeyEvent::LongPress: return "long";
        case KeyEvent::Repeat: return "repeat";
        default: return "press";
    }
}

}  // namespace wr
