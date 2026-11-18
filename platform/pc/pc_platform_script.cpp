// Computer layer: the --script runner. One step per line: wait <ms>, tap <x> <y>, press <x> <y>, move <x> <y>,
// release, key <name>, hold <name> <ms>, text <UTF-8 text>, shot <name>, quit. M5.update() runs it (one touch or
// key per frame); taps go through the same input state as the mouse (pc_platform_screen.cpp), keys through the
// same wr::KeyTracker as the keyboard (pc_platform_keys.cpp; names: wr::navKeyFromName). hold = key down, wait
// <ms> (real time, like wait), key up. See pc_internal.h for the other pc_platform*.cpp files.
#include <unistd.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "Arduino.h"
#include "pc_internal.h"

// ------------------------------------------------------------------ script

namespace pc_internal {

struct Step {
    enum Kind { Wait, Tap, Press, Move, Release, Key, KeyDown, KeyUp, Text, Shot, Quit } kind;
    int x = 0, y = 0, ms = 0;
    std::string name;  // shot: file name; text: the text
    wr::NavKey key = wr::NavKey::None;
};

std::vector<Step> g_script;
size_t g_step = 0;
uint32_t g_stepAt = 0;

void runScript() {
    while (g_step < g_script.size()) {
        const Step& s = g_script[g_step];
        if (s.kind == Step::Wait) {
            if (millis() - g_stepAt < (uint32_t)s.ms) return;
        } else if (s.kind == Step::Tap) {
            g_mouseX = s.x, g_mouseY = s.y;
            g_pressQueued = g_releaseQueued = true;
        } else if (s.kind == Step::Press) {
            g_mouseX = s.x, g_mouseY = s.y;
            g_mouseDown = g_pressQueued = true;
        } else if (s.kind == Step::Move) {
            g_mouseX = s.x, g_mouseY = s.y;
        } else if (s.kind == Step::Release) {
            g_mouseDown = false;
            g_releaseQueued = true;
        } else if (s.kind == Step::Key) {
            keyDown(s.key);
            keyUp(s.key);
        } else if (s.kind == Step::KeyDown) {
            keyDown(s.key);
        } else if (s.kind == Step::KeyUp) {
            keyUp(s.key);
        } else if (s.kind == Step::Text) {
            keyText(s.name);
        } else if (s.kind == Step::Shot) {
            shot(s.name);
        } else {
            fflush(stdout), _exit(0);
        }
        ++g_step;
        g_stepAt = millis();
        if (s.kind != Step::Wait && s.kind != Step::Shot) return;  // one touch or key per frame
    }
}

bool loadScript(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    int lineNo = 0;
    // Key name of a key/hold step; an unknown name stops the script like an unknown command
    auto keyOf = [&](std::istringstream& in, wr::NavKey& key) {
        std::string name;
        in >> name;
        key = wr::navKeyFromName(name);
        if (key == wr::NavKey::None)
            fprintf(stderr, "script ligne %d : touche inconnue \xC2\xAB %s \xC2\xBB (up, down, left, right, ok, back, menu, "
                            "playpause, play, pause, next, prev, ffwd, rewind, volup, voldown, mute, search, backspace, "
                            "enter)\n", lineNo, name.c_str());
        return key != wr::NavKey::None;
    };
    while (std::getline(f, line)) {
        ++lineNo;
        std::istringstream in(line);
        std::string cmd;
        in >> cmd;
        if (cmd.empty() || cmd[0] == '#') continue;
        Step s{Step::Wait};
        if (cmd == "wait") in >> s.ms;
        else if (cmd == "tap") s.kind = Step::Tap, in >> s.x >> s.y;
        else if (cmd == "press") s.kind = Step::Press, in >> s.x >> s.y;
        else if (cmd == "move") s.kind = Step::Move, in >> s.x >> s.y;
        else if (cmd == "release") s.kind = Step::Release;
        else if (cmd == "key") {
            s.kind = Step::Key;
            if (!keyOf(in, s.key)) return false;
        } else if (cmd == "hold") {
            // down, wait, up: three steps
            if (!keyOf(in, s.key)) return false;
            int ms = 0;
            if (!(in >> ms) || ms <= 0) {
                fprintf(stderr, "script ligne %d : hold <touche> <ms>\n", lineNo);
                return false;
            }
            s.kind = Step::KeyDown;
            g_script.push_back(s);
            Step w{Step::Wait};
            w.ms = ms;
            g_script.push_back(w);
            s.kind = Step::KeyUp;
        } else if (cmd == "text") {
            s.kind = Step::Text;
            std::getline(in >> std::ws, s.name);  // the rest of the line, spaces included
            if (!s.name.empty() && s.name.back() == '\r') s.name.pop_back();
            if (s.name.empty()) {
                fprintf(stderr, "script ligne %d : text <texte>\n", lineNo);
                return false;
            }
        } else if (cmd == "shot") s.kind = Step::Shot, in >> s.name;
        else if (cmd == "quit") s.kind = Step::Quit;
        else {
            fprintf(stderr, "script : commande inconnue « %s »\n", cmd.c_str());
            return false;
        }
        g_script.push_back(s);
    }
    return true;
}

}  // namespace pc_internal
