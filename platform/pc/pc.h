// Computer layer (platform/pc): internals shared by the simulator (tools/sim) and the desktop build (not used by src/).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pc {
const std::string& stateDir();  // settings.txt (the simulated NVS)
const std::string& outDir();    // screenshots
bool quiet();
const std::string& relay();    // relay address host:port ("real WiiM" build), default 127.0.0.1:8787
[[noreturn]] void restart();    // like ESP.restart(): the program starts again
void defaultSettings();         // first start: settings of a configured Tab5
std::string programDir();       // folder of the program file, with a final separator ("" if unknown)

// Root certificates shipped with the program, for builds where OpenSSL has no system store (PC_BUNDLED_CA:
// Windows; an Android port gives the same PEM file from its assets): the PEM text, and in `where` where it was
// read (for the log). Default: cacert.pem beside the program (pc_platform.cpp).
std::string bundledRootCerts(std::string& where);

// PNG picture of an album cover, drawn from its URL (sim://cover/<hue>/<pattern>)
std::vector<uint8_t> coverPng(const std::string& url, int size);
}  // namespace pc
