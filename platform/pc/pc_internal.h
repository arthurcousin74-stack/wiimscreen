// Computer layer (platform/pc): what the pc_platform*.cpp files share with each other (not used elsewhere).
// One concern per file:
//   pc_platform.cpp          command-line options, state/output folders, main()
//   pc_platform_rtos.cpp     clock, random numbers, FreeRTOS on std::thread
//   pc_platform_prefs.cpp    Preferences (settings.txt in the state folder), settings of a first start
//   pc_platform_devices.cpp  M5, Serial, ESP and WiFi objects, Serial output, microphone, battery
//   pc_platform_screen.cpp   SDL window, display canvas shown in it, mouse/touch, screenshots, screen size
//   pc_platform_script.cpp   --script runner (wait, tap, press, move, release, key, hold, text, shot, quit)
//   pc_platform_keys.cpp     key input: SDL keyboard/controllers and script keys -> wr::KeyTracker -> takeKey()
//   pc_platform_exit.cpp     restart (execv of the same command; Windows: CreateProcess) and power off (_exit)
//   pc_platform_windows.cpp  Windows only: settings folder (%APPDATA%\wiimscreen), console or last.log output
#pragma once

#include <SDL2/SDL.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "keys.h"

namespace pc_internal {
// pc_platform.cpp
extern std::vector<std::string> g_argv;  // the command line, for a restart

// pc_platform_prefs.cpp
std::map<std::string, std::string>& prefs();
void loadPrefs();
void savePrefs();

// pc_platform_screen.cpp
extern SDL_Window* g_win;
extern SDL_Renderer* g_ren;
extern float g_scale;
extern int g_long, g_short;  // --size
extern bool g_fullscreen;
extern bool g_mouseDown, g_pressQueued, g_releaseQueued;
extern int g_mouseX, g_mouseY;
void shot(const std::string& name);

// pc_platform_keys.cpp
extern bool g_logKeys;  // --log-keys: print each event the app takes
void initKeyInput();    // after SDL_Init (window mode only)
bool keyEvent(const SDL_Event& e);  // a key/text/controller event: true when it was one
void keyDown(wr::NavKey key);
void keyUp(wr::NavKey key);
void keyText(const std::string& utf8);
void keyTick();  // repeats and long presses, once per M5.update()

// pc_platform_script.cpp
extern uint32_t g_stepAt;
void runScript();
bool loadScript(const std::string& path);

#ifdef _WIN32
// pc_platform_windows.cpp
std::string windowsConfigDir();  // %APPDATA%\wiimscreen (UTF-8)
// Where the Journal and messages go: a console window (--console), the terminal or pipe that started the
// program, or else <state>/last.log (the previous run's log becomes previous.log)
void windowsStartOutput(const std::string& stateDir, bool console);
bool windowsLogsToFile();  // output goes to last.log (a restart lets the new process open it again)
#endif
}  // namespace pc_internal
