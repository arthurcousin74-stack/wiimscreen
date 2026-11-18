// Tappable elements of the screen shown, in one list: what a tap hits, and (later) what a D-pad focus moves over.
// Ui fills the list while it draws a full screen (Ui::targets()); partial redraws never change it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "layout.h"

namespace wr {

// Kind of element. `Target::arg` tells which one when there are several.
enum class Act : uint8_t {
    None = 0,
    // Navigation rail (also the Home button of the ambient view)
    Home,
    Search,
    Settings,
    // Controls view
    Header,     // cover art (arg 0) or texts (arg 1): long press opens Settings
    Progress,   // progress bar (the tap position is the seek target)
    Volume,     // volume slider (the tap position is the level)
    Mute,       // speaker icon
    Dots,       // page dots: arg -1 left half (previous page), +1 right half (next page)
    Preset,     // preset tile: arg = global preset index
    Strip,      // preset strip around the tiles (drag to change page)
    Fav,        // heart button (add / remove the current album)
    Transport,  // arg = wr::Transport (vol-, prev, play/pause, next, vol+)
    // Ambient view
    AmbientExit,  // anywhere: back to the controls view
    // Settings menu
    MenuChoice,  // arg = Ui::MenuChoice
    // WiiM picker
    PickDevice,  // arg = index in the list of players
    PickRetry,
    PickSetup,
    // Journal
    LogClose,
    LogQueue,
    // Touch test
    TouchClose,
    // Search keyboard
    Source,  // source chip: arg 0 Qobuz, 1 Deezer, 2 Albums
    Mic,     // dictation button in the search field
    Key,     // arg = index in Ui::keys()
    // Results list
    ResultRow,    // arg = global index of the result
    ResultHeart,  // arg = global index of the result whose heart it is
    ResultsPage,  // arg -1 previous page, +1 next page
    ResultsEdit,  // "Edit" (back to the keyboard)
    // Update screen
    UpdateCancel,
};

// Flags of a target
enum : uint8_t {
    kTargetNoFocus = 1,  // tappable, never focused by the D-pad (bars, header, strip, dots, whole-screen areas)
    kTargetDefault = 2,  // initial focus of the screen
};

struct Target {
    Rect r;        // drawn rectangle (focus ring, geometry of the D-pad moves)
    Rect hit;      // touch area: r, or r with padding (fingers often land next to small buttons)
    int arg = 0;   // which element (see Act)
    Act act = Act::None;
    uint8_t flags = 0;
    bool focusable() const { return !(flags & kTargetNoFocus); }
};

class TargetList {
public:
    void reserve(size_t n) { items_.reserve(n); }
    void clear() { items_.clear(); }  // keeps the capacity: no heap churn per frame
    void add(Act act, int arg, const Rect& r, const Rect& hit, uint8_t flags = 0);
    void add(Act act, int arg, const Rect& r, uint8_t flags = 0) { add(act, arg, r, r, flags); }

    // First target whose touch area contains the point: the insertion order is the priority
    const Target* hit(int x, int y) const;
    // First target of this kind whose touch area contains the point (Ui::hitXxx lookups)
    const Target* hit(Act act, int x, int y) const;
    // First target of this kind with this arg, or nullptr
    const Target* find(Act act, int arg) const;

    size_t size() const { return items_.size(); }
    bool empty() const { return items_.empty(); }
    const Target& operator[](size_t i) const { return items_[i]; }
    std::vector<Target>::const_iterator begin() const { return items_.begin(); }
    std::vector<Target>::const_iterator end() const { return items_.end(); }

private:
    std::vector<Target> items_;
};

}  // namespace wr
