// Computer layer: the M5, Serial, ESP and WiFi objects of the Arduino/M5Unified stubs, the computer's network
// address, Serial output (--quiet), microphone and battery. See pc_internal.h for the other pc_platform*.cpp files.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "M5Unified.h"
#include "WiFi.h"
#include "pc.h"

SimM5 M5;

#ifdef PC_DESKTOP
// Address of the computer on the network: the interface that would be used to reach the Internet (no packet sent)
IPAddress WiFiSim::localIP() const {
    IPAddress ip;
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(53);
    inet_pton(AF_INET, "8.8.8.8", &to.sin_addr);
    sockaddr_in me{};
    socklen_t n = sizeof me;
    if (fd >= 0 && connect(fd, (sockaddr*)&to, sizeof to) == 0 && getsockname(fd, (sockaddr*)&me, &n) == 0) {
        char s[INET_ADDRSTRLEN];
        ip.text = inet_ntop(AF_INET, &me.sin_addr, s, sizeof s);
    }
    if (fd >= 0) close(fd);
    return ip;
}
#else
IPAddress WiFiSim::localIP() const { return IPAddress(); }
#endif
SerialSim Serial;
EspSim ESP;
WiFiSim WiFi;

// ------------------------------------------------------------------ Serial

bool quietSerial() { return pc::quiet(); }

void SerialSim::println(const char* s) {
    if (!pc::quiet()) printf("%s\n", s);
}
void SerialSim::print(const char* s) {
    if (!pc::quiet()) printf("%s", s);
}
int SerialSim::printf(const char* fmt, ...) {
    if (pc::quiet()) return 0;
    va_list ap;
    va_start(ap, fmt);
    int n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}

// ------------------------------------------------------------------ microphone, power

#ifdef PC_DESKTOP
bool SimMic::begin() { return false; }  // no microphone support yet on a PC: « Micro indisponible »
int SimPower::getBatteryLevel() const { return -1; }
#else
bool SimMic::begin() { return on = true; }
int SimPower::getBatteryLevel() const { return 85; }
#endif
bool SimMic::record(int16_t* buf, size_t len, uint32_t) {
    std::memset(buf, 0, len * 2);  // silence: the dictation text comes from transcribe() (fake)
    return on;
}
