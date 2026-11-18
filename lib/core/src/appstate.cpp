#include "appstate.h"

#include <algorithm>

namespace wr {

bool expired(uint32_t now, uint32_t since, uint32_t limitMs) {
    const int32_t d = static_cast<int32_t>(now - since);
    return d > 0 && static_cast<uint32_t>(d) > limitMs;
}

IdleAction idleAction(const IdleInput& in, const IdleTimes& t) {
    const bool playing = in.online && in.playing;
    const uint32_t quiet = playing ? 0 : std::min(in.touchIdleMs, in.notPlayingMs);
    if (in.sleepMin > 0 && !playing && !in.modal && quiet > static_cast<uint32_t>(in.sleepMin) * 60000UL)
        return IdleAction::Sleep;
    if (!playing && quiet > t.offMs) return (in.dark && !in.ambient) ? IdleAction::None : IdleAction::Off;
    if (!in.ambient && !in.modal && in.fullBright && playing && in.touchIdleMs > t.ambientMs) return IdleAction::Ambient;
    if (in.fullBright && in.touchIdleMs > t.dimMs) return IdleAction::Dim;
    return IdleAction::None;
}

bool musicSettled(const PlayerStatus& now, bool sawLoading, uint32_t sinceReadyMs, uint32_t readyMs) {
    return now.playing && !now.loading && (sawLoading || sinceReadyMs >= readyMs);
}

}  // namespace wr
