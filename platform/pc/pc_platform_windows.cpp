// Computer layer, Windows only (wiimscreen.exe, env windows): the settings folder and where the output goes.
// See pc_internal.h for the other pc_platform*.cpp files; the restart is in pc_platform_exit.cpp.
//
// wiimscreen.exe is a GUI program (no console window opens with it). Its Journal and messages go to:
//   - the terminal or pipe that started it, when there is one (cmd, PowerShell, WSL, the CI self-test);
//   - a new console window with --console;
//   - else <state>\last.log (the log of the run before becomes previous.log), so a user can send it.
// Text is UTF-8 everywhere: the program's manifest sets the UTF-8 code page (platform/windows/wiimscreen.manifest).
#ifdef _WIN32

#include <windows.h>

#include <cstdio>
#include <string>

#include "pc_internal.h"

namespace {
bool s_toFile = false;

std::string utf8(const wchar_t* w) {
    if (!w || !*w) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}

// A standard handle that the parent gave us (terminal, file or pipe)
bool usable(DWORD which) {
    const HANDLE h = GetStdHandle(which);
    return h && h != INVALID_HANDLE_VALUE && GetFileType(h) != FILE_TYPE_UNKNOWN;
}
}  // namespace

namespace pc_internal {

std::string windowsConfigDir() {
    const std::string appData = utf8(_wgetenv(L"APPDATA"));
    return (appData.empty() ? std::string(".") : appData) + "\\wiimscreen";
}

void windowsStartOutput(const std::string& stateDir, bool console) {
    if (console) {
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) AllocConsole();
        SetConsoleOutputCP(CP_UTF8);
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        return;
    }
    if (usable(STD_OUTPUT_HANDLE)) return;  // started from a terminal or with its output redirected
    const std::string log = stateDir + "\\last.log", previous = stateDir + "\\previous.log";
    // A restarted wiimscreen starts while the old process may still hold the file for a moment
    for (int i = 0; i < 20; ++i) {
        DeleteFileA(previous.c_str());
        if (MoveFileA(log.c_str(), previous.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND) break;
        Sleep(50);
    }
    FILE* f = fopen(log.c_str(), "w");  // empty file; then both streams append to it, in the order of the writes
    if (!f) return;
    fclose(f);
    if (!freopen(log.c_str(), "a", stdout) || !freopen(log.c_str(), "a", stderr)) return;
    setvbuf(stdout, nullptr, _IONBF, 0);  // each line is in the file at once (a crash keeps it)
    setvbuf(stderr, nullptr, _IONBF, 0);
    s_toFile = true;
}

bool windowsLogsToFile() { return s_toFile; }

}  // namespace pc_internal

#endif  // _WIN32
