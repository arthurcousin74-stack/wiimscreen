#pragma once

// Timing decisions of the main loop (screen, sleep, waiting rings), without hardware: unit-tested in test/test_core.

#include <cstdint>

#include "core.h"

namespace wr {

// True when more than limitMs have passed since `since`. Signed difference: `since` may be a little AFTER `now`
// (millis() read later than the loop's `now`) and the counter may wrap after 49 days; neither counts as expired.
bool expired(uint32_t now, uint32_t since, uint32_t limitMs);

// What the idle timers ask for, one action per loop
enum class IdleAction { None, Ambient, Dim, Off, Sleep };

struct IdleInput {
    uint32_t touchIdleMs = 0;   // since the last touch (or motion wake)
    uint32_t notPlayingMs = 0;  // since playback was last seen (0 while it plays)
    bool online = false;        // the WiiM answers; offline, its last "playing" state is stale and does not count
    bool playing = false;
    bool modal = false;         // menu, search, Journal, touch test, voice
    bool ambient = false;       // ambient view shown
    bool fullBright = false;    // backlight at the normal level
    bool dark = false;          // backlight off
    int sleepMin = 0;           // setting: power off after N minutes without touch AND without playback (0 = never)
};

struct IdleTimes {
    uint32_t ambientMs = 60UL * 1000;     // ambient view after this long without touch, while music plays
    uint32_t dimMs = 10UL * 60 * 1000;    // dimmed after this long without touch
    uint32_t offMs = 20UL * 60 * 1000;    // off after this long without touch and without playback
};

// Sleep and Off count from the later of the last touch and the last playback seen: 45 min of music untouched does
// not power off the moment the album ends.
IdleAction idleAction(const IdleInput& in, const IdleTimes& t = IdleTimes());

// Awaited::Music (preset, favorite, search result) is confirmed by pendingConfirmed only with a new title. The tile of
// what already plays, or a radio without title, never gives one: it also counts as confirmed when the WiiM plays (not
// loading) after it was seen loading, or readyMs after the command reached the WiiM.
bool musicSettled(const PlayerStatus& now, bool sawLoading, uint32_t sinceReadyMs, uint32_t readyMs = 4000);

}  // namespace wr
