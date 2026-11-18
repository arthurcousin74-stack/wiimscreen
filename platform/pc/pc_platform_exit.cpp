// Computer layer: how the program restarts (ESP.restart: execv of the same command) and powers off (_exit).
// Kept apart so that another host (an app on a phone, for example) can replace them. The script's « quit » step
// and the window's close button also end the program with _exit (pc_platform_script.cpp, pc_platform_screen.cpp).
// See pc_internal.h for the other pc_platform*.cpp files.
#include <unistd.h>

#include <vector>

#include "M5Unified.h"
#include "pc.h"
#include "pc_internal.h"

using namespace pc_internal;

void EspSim::restart() { pc::restart(); }

void SimPower::powerOff() {
    printf("Mise en veille : fin du programme\n");
    fflush(stdout), _exit(0);
}

void pc::restart() {
    printf("Red\xC3\xA9marrage\n");
    fflush(stdout);
    std::vector<char*> args;
    for (auto& a : g_argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);
    execv("/proc/self/exe", args.data());  // Linux
    execvp(args[0], args.data());           // macOS and others: the same command again
    std::exit(1);
}
