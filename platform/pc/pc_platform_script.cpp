// Computer layer: the --script runner. One step per line: wait <ms>, tap <x> <y>, press <x> <y>, move <x> <y>,
// release, shot <name>, quit. M5.update() runs it (one touch per frame); taps go through the same input state as
// the mouse (pc_platform_screen.cpp). See pc_internal.h for the other pc_platform*.cpp files.
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
    enum Kind { Wait, Tap, Press, Move, Release, Shot, Quit } kind;
    int x = 0, y = 0, ms = 0;
    std::string name;
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
        } else if (s.kind == Step::Shot) {
            shot(s.name);
        } else {
            fflush(stdout), _exit(0);
        }
        ++g_step;
        g_stepAt = millis();
        if (s.kind != Step::Wait && s.kind != Step::Shot) return;  // one touch per frame
    }
}

bool loadScript(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
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
        else if (cmd == "shot") s.kind = Step::Shot, in >> s.name;
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
