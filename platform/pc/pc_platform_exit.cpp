// Computer layer: how the program restarts (ESP.restart: the same command again) and powers off (_exit).
// Kept apart so that another host (an app on a phone, for example) can replace them: pc::restart() is the only
// place that starts the program again. The script's « quit » step and the window's close button also end the
// program with _exit (pc_platform_script.cpp, pc_platform_screen.cpp).
// See pc_internal.h for the other pc_platform*.cpp files.
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <cstdlib>
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

#ifdef _WIN32
// Windows: no exec(). Start the same program with the same command line (GetCommandLineW keeps the quoting),
// in the same folder, then end this process. Output: the new process finds the same terminal or pipe (handles
// inherited), or opens last.log itself (pc_platform_windows.cpp).
void pc::restart() {
    printf("Red\xC3\xA9marrage\n");
    fflush(stdout), fflush(stderr);
    wchar_t exe[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(nullptr, exe, sizeof exe / sizeof exe[0]);
    std::wstring cmd = GetCommandLineW();  // CreateProcessW may write into it: a copy
    STARTUPINFOW si{};
    si.cb = sizeof si;
    const bool toFile = windowsLogsToFile();
    if (!toFile) {
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    }
    PROCESS_INFORMATION pi{};
    if (n > 0 && n < sizeof exe / sizeof exe[0] &&
        CreateProcessW(exe, &cmd[0], nullptr, nullptr, toFile ? FALSE : TRUE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        _exit(0);
    }
    fprintf(stderr, "Red\xC3\xA9marrage impossible (erreur %lu)\n", (unsigned long)GetLastError());
    std::exit(1);
}
#else
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
#endif
