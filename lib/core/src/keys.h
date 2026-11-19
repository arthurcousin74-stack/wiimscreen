#pragma once

// Key input without hardware: remote/keyboard keys turned into app events (press, long press, repeat).
// A platform feeds raw key-down/key-up with its clock (KeyTracker::down/up/tick) and the app reads the events in
// order (KeyTracker::take). A platform that only knows "pressed" (no key-up) calls KeyTracker::post instead.
// Unit-tested in test/test_core/test_keys.cpp.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace wr {

enum class NavKey {
    None,
    Up,
    Down,
    Left,
    Right,
    Ok,
    Back,
    Menu,
    PlayPause,
    Play,
    Pause,
    Next,
    Prev,
    FastFwd,
    Rewind,
    VolUp,
    VolDown,
    Mute,
    Search,
    Backspace,
    Enter,
    Text,  // typed text: KeyEvent::text holds it (UTF-8)
};

struct KeyEvent {
    NavKey key = NavKey::None;
    enum Kind { Press, LongPress, Repeat } kind = Press;
    std::string text;  // UTF-8, for NavKey::Text only
};

// Timing rules, in one place
struct KeyTiming {
    static constexpr uint32_t kRepeatDelayMs = 400;     // arrows, Backspace, volume: first Repeat after this long held
    static constexpr uint32_t kRepeatIntervalMs = 125;  // then one Repeat every 125 ms (8/s)
    static constexpr uint32_t kLongPressMs = 700;       // Ok: LongPress once held this long (no Press on release)
    static constexpr size_t kMaxQueued = 32;            // unread events beyond this: the oldest are dropped
};

// Raw key-down/key-up → events.
//   Arrows, Backspace, VolUp/VolDown (held like a remote's volume key): Press on down, then Repeat after kRepeatDelayMs, every kRepeatIntervalMs while held.
//   Ok: Press on release if held less than kLongPressMs; LongPress once at kLongPressMs while held (or at the
//       release, when no tick() came in between), then no Press.
//   Other keys: Press on down.
// A down() for a key already held (OS auto-repeat) and an up() without down() are ignored. Times are millis()
// values: differences are wrap-safe (wr::expired).
class KeyTracker {
public:
    void down(NavKey key, uint32_t nowMs);
    void up(NavKey key, uint32_t nowMs);
    void tick(uint32_t nowMs);              // call often (every loop): repeats and long presses
    void post(NavKey key, KeyEvent::Kind kind = KeyEvent::Press);  // an event as is (platforms without key-up)
    void text(const std::string& utf8);     // typed text (NavKey::Text); empty text is ignored
    void cancelAll();                       // forget the held keys, no event (focus lost, device removed)
    bool take(KeyEvent& out);               // oldest event first; false when there is none
    bool held(NavKey key) const;

private:
    struct Held {
        NavKey key;
        uint32_t downAt;
        uint32_t lastRepeatAt;  // time the last Repeat was due (scheduled, not when tick() saw it)
        bool repeating;
        bool longSent;
    };
    void push(KeyEvent ev);
    std::vector<Held> held_;
    std::deque<KeyEvent> out_;
};

// Lower-case ASCII names for scripts and logs: up, down, left, right, ok, back, menu, playpause, play, pause, next,
// prev, ffwd, rewind, volup, voldown, mute, search, backspace, enter. NavKey::None for any other name (also "text").
NavKey navKeyFromName(const std::string& name);
const char* navKeyName(NavKey key);              // "none" and "text" for those two
const char* keyKindName(KeyEvent::Kind kind);    // press, long, repeat

}  // namespace wr
