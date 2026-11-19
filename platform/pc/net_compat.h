// Computer layer: the few socket calls that differ between POSIX (Linux, macOS, Raspberry Pi) and Windows
// (Winsock 2). net_direct.cpp and pc_platform_devices.cpp use these names, so they keep one code path.
// A socket is held as intptr_t: on Windows a SOCKET is pointer-sized, and INVALID_SOCKET reads as -1.
#pragma once

#include <cstdint>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>

#include <mutex>

namespace netc {
using sock = intptr_t;
// Winsock must be started once per process before any socket call (getaddrinfo included)
inline void init() {
    static std::once_flag once;
    std::call_once(once, [] {
        WSADATA d;
        WSAStartup(MAKEWORD(2, 2), &d);
    });
}
inline sock open(int family, int type, int proto) {
    init();
    const SOCKET s = ::socket(family, type, proto);
    return s == INVALID_SOCKET ? -1 : (sock)s;
}
inline void close(sock s) { ::closesocket((SOCKET)s); }
inline int lastError() { return WSAGetLastError(); }
inline bool connectPending() { return WSAGetLastError() == WSAEWOULDBLOCK; }
inline void setBlocking(sock s, bool on) {
    u_long nb = on ? 0 : 1;
    ioctlsocket((SOCKET)s, FIONBIO, &nb);
}
// Receive and send time limits: a DWORD in milliseconds on Windows
inline void setTimeouts(sock s, uint32_t ms) {
    const DWORD v = ms;
    setsockopt((SOCKET)s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&v), sizeof v);
    setsockopt((SOCKET)s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&v), sizeof v);
}
inline int pending(sock s) {
    u_long n = 0;
    return ioctlsocket((SOCKET)s, FIONREAD, &n) == 0 ? (int)n : 0;
}
inline int sendBytes(sock s, const void* b, size_t n) { return ::send((SOCKET)s, (const char*)b, (int)n, 0); }
inline int recvBytes(sock s, void* b, size_t n, int flags = 0) { return ::recv((SOCKET)s, (char*)b, (int)n, flags); }
// Readable (or closed) within ms
inline bool readable(sock s, int ms) {
    WSAPOLLFD p{(SOCKET)s, POLLIN, 0};
    return WSAPoll(&p, 1, ms) > 0 && (p.revents & (POLLIN | POLLHUP | POLLERR));
}
// A non-blocking connect finished within ms. select(), not WSAPoll: WSAPoll does not report a refused connection
// on older Windows 10 builds (it waits until the time limit)
inline bool connected(sock s, uint32_t ms) {
    fd_set w, e;
    FD_ZERO(&w);
    FD_ZERO(&e);
    FD_SET((SOCKET)s, &w);
    FD_SET((SOCKET)s, &e);
    timeval tv{(long)(ms / 1000), (long)(ms % 1000) * 1000};
    return select(0, nullptr, &w, &e, &tv) == 1 && FD_ISSET((SOCKET)s, &w);
}
}  // namespace netc

#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL  // macOS: SIGPIPE is ignored for the whole program instead (pc_platform.cpp)
#define MSG_NOSIGNAL 0
#endif

namespace netc {
using sock = intptr_t;
inline void init() {}
inline sock open(int family, int type, int proto) { return ::socket(family, type, proto); }
inline void close(sock s) { ::close((int)s); }
inline int lastError() { return errno; }
inline bool connectPending() { return errno == EINPROGRESS; }
inline void setBlocking(sock s, bool on) {
    const int flags = fcntl((int)s, F_GETFL, 0);
    fcntl((int)s, F_SETFL, on ? flags & ~O_NONBLOCK : flags | O_NONBLOCK);
}
inline void setTimeouts(sock s, uint32_t ms) {
    timeval tv{(time_t)(ms / 1000), (suseconds_t)(ms % 1000) * 1000};
    setsockopt((int)s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt((int)s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
}
inline int pending(sock s) {
    int n = 0;
    return ioctl((int)s, FIONREAD, &n) == 0 ? n : 0;
}
inline int sendBytes(sock s, const void* b, size_t n) { return (int)::send((int)s, b, n, MSG_NOSIGNAL); }
inline int recvBytes(sock s, void* b, size_t n, int flags = 0) { return (int)::recv((int)s, b, n, flags); }
inline bool readable(sock s, int ms) {
    pollfd p{(int)s, POLLIN, 0};
    return poll(&p, 1, ms) > 0 && (p.revents & (POLLIN | POLLHUP));
}
inline bool connected(sock s, uint32_t ms) {
    pollfd p{(int)s, POLLOUT, 0};
    int err = 0;
    socklen_t len = sizeof err;
    return poll(&p, 1, (int)ms) == 1 && getsockopt((int)s, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && !err;
}
}  // namespace netc
#endif
