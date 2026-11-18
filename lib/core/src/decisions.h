#pragma once

// Decisions of the app that need no hardware or network, taken out of src/ so they are unit-tested
// (test/test_core). Each one used to be written inline in main.cpp or wiim_worker.cpp.

#include <cstddef>
#include <cstdint>
#include <string>

#include "core.h"
#include "upnp.h"

namespace wr {

// What a command waits for before its "waiting" ring stops (main loop)
enum class Awaited { None, Track, Playing, Music };
// True if this WiiM report confirms the pending command. An empty title (getMetaInfo missed this time) never counts
// as a new track.
//   Track: another title, or the same one back near the start (it was far in: previous = restart)
//   Playing: the play/pause state wanted
//   Music: something new plays (preset, favorite, result), loading finished
bool pendingConfirmed(Awaited what, const PlayerStatus& now, const std::string& titleBefore, uint32_t posBeforeMs,
                      bool wantPlaying);

// After a search done through a WiiM queue (the WiiM starts every queue it creates): what to do with the music.
enum class RestoreAction {
    Nothing,          // nothing changed
    StopSearchQueue,  // nothing played before, the WiiM stays paused on the search queue: stop it
    PutBack,          // put the previous queue back, same track and position (also when the search stopped it)
    PutBackPaused,    // a track was paused in a known queue: put it back (same track, position), paused
    Pause,            // something played outside a queue (AirPlay, Spotify…): pause what the search started
    Stop,             // nothing played before: stop what the search started
};
struct PlaybackBefore {
    bool playing = false;
    std::string title;
    uint32_t posMs = 0;
    bool inQueue = false;  // its queue and its place in it are known (can be put back)
};
RestoreAction restoreAction(const PlaybackBefore& before, const PlayerStatus& now);

// Loop mode ("loop" of getPlayerStatus, "setPlayerCmd:loopmode:n") with the same repeat but no shuffle, or -1 if
// shuffle is already off. 0 repeat all, 1 repeat one, 2 shuffle + repeat all, 3 shuffle, 4 no repeat, 5 shuffle +
// repeat one. A search through a WiiM queue turns shuffle off for its duration: else the WiiM shuffles the results.
int loopWithoutShuffle(int loop);

// Search through a WiiM queue: try the next way to write the query? `found`: the queue got tracks; `relevant`: they
// share a word with the query; `empties`: queues that stayed empty so far (this one included). An empty queue means no
// match or a WiiM that loads nothing: only a query of several words gets one more try (other spelling of the spaces).
bool searchTryNext(bool found, bool relevant, int empties, bool severalWords);

// What a search through a WiiM queue changed on the WiiM (sound muted, shuffle turned off), kept in storage while it
// runs: if the remote restarts in the middle, the next start puts it back. `loop` is the mode to set again, -1: none.
struct SearchUndo {
    bool unmute = false;
    int loop = -1;
};
std::string searchUndoText(const SearchUndo& u);
bool parseSearchUndo(const std::string& text, SearchUndo& out);  // false: nothing to undo (empty, unreadable)

// A queue filled by the WiiM (search, favorites, album) grows step by step (1-2 tracks after 1 s, 7 after 6 s):
// it is complete when the same non-zero count comes back 4 more times in a row and at least 3 s have passed.
class QueueStability {
public:
    bool feed(int tracks, uint32_t elapsedMs);  // true when stable

private:
    int last_ = -1, same_ = 0;
};

// 1-based place of `title` in the queue (as PlayQueueWithIndex counts), 0 if absent. loose: compare with
// normalizeText (case, accents, punctuation).
int queueIndexOf(const QueueInfo& q, const std::string& title, bool loose);

// Text heard by the speech-to-text service, ready for the search field: quotes and final punctuation removed,
// at most maxLen bytes (cut on a UTF-8 character boundary).
std::string cleanTranscript(const std::string& text, size_t maxLen = 60);

}  // namespace wr
