#include "decisions.h"

#include <cstring>

#include "voice.h"

namespace wr {

bool pendingConfirmed(Awaited what, const PlayerStatus& now, const std::string& titleBefore, uint32_t posBeforeMs,
                      bool wantPlaying) {
    const bool newTitle = !now.title.empty() && now.title != titleBefore;
    switch (what) {
        case Awaited::Track: return newTitle || (posBeforeMs > 5000 && now.positionMs < 3000);
        case Awaited::Playing: return now.playing == wantPlaying;
        case Awaited::Music: return now.playing && !now.loading && newTitle;
        case Awaited::None: break;
    }
    return false;
}

RestoreAction restoreAction(const PlaybackBefore& before, const PlayerStatus& now) {
    // Same title but back to the start (the search queue held the track that played): changed too
    const bool restarted = before.posMs > 8000 && now.positionMs + 4000 < before.posMs;
    const bool changed = now.playing && (!before.playing || now.title != before.title || restarted);
    if (!changed) {
        // Music stopped by the search (DeleteQueue stops playback; a failed search leaves an empty queue): put it
        // back when its queue is known. Outside a queue (AirPlay, Spotify…) it cannot be restarted from here.
        if (before.playing && !now.playing) return before.inQueue ? RestoreAction::PutBack : RestoreAction::Nothing;
        return !before.playing && !now.playing && !now.title.empty() && now.title != before.title
                   ? RestoreAction::StopSearchQueue
                   : RestoreAction::Nothing;
    }
    if (before.playing && before.inQueue) return RestoreAction::PutBack;
    return before.playing ? RestoreAction::Pause : RestoreAction::Stop;
}

bool QueueStability::feed(int tracks, uint32_t elapsedMs) {
    if (tracks > 0 && tracks == last_) {
        if (++same_ >= 4 && elapsedMs >= 3000) return true;
    } else {
        same_ = 0;
    }
    last_ = tracks;
    return false;
}

int queueIndexOf(const QueueInfo& q, const std::string& title, bool loose) {
    if (title.empty()) return 0;
    const std::string want = loose ? normalizeText(title) : title;
    for (size_t k = 0; k < q.items.size(); ++k)
        if ((loose ? normalizeText(q.items[k].title) : q.items[k].title) == want) return (int)k + 1;
    return 0;
}

std::string cleanTranscript(const std::string& text, size_t maxLen) {
    size_t a = 0, b = text.size();
    while (b > a && std::strchr(" .,;:!?\"'\n", text[b - 1])) --b;
    while (a < b && std::strchr(" \"'\n", text[a])) ++a;
    std::string out = text.substr(a, b - a);
    if (out.size() > maxLen) {
        size_t cut = maxLen;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;  // not inside a character
        out.resize(cut);
    }
    return out;
}

}  // namespace wr
