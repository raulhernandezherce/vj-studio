#pragma once
// Sockets iguales en macOS y Windows. Fuera de aquí, el resto del motor no distingue plataforma.
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <basetsd.h>

using ssize_t = SSIZE_T;
inline int poll(pollfd* fds, size_t count, int timeout) { return WSAPoll(fds, static_cast<ULONG>(count), timeout); }

namespace vj::net {
inline const int kStarted = [] {
  WSADATA data;
  return WSAStartup(MAKEWORD(2, 2), &data);
}();
inline void closeSocket(int fd) { closesocket(static_cast<SOCKET>(fd)); }
inline void setNonBlocking(int fd) {
  u_long yes = 1;
  ioctlsocket(static_cast<SOCKET>(fd), FIONBIO, &yes);
}
// Windows no admite tuberías en poll: el aviso entre hilos va por UDP local.
inline bool wakePair(int fds[2]) {
  const SOCKET a = socket(AF_INET, SOCK_DGRAM, 0), b = socket(AF_INET, SOCK_DGRAM, 0);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  int len = sizeof addr;
  if (a == INVALID_SOCKET || b == INVALID_SOCKET || bind(a, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 ||
      getsockname(a, reinterpret_cast<sockaddr*>(&addr), &len) != 0 || connect(b, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0)
    return false;
  fds[0] = static_cast<int>(a);
  fds[1] = static_cast<int>(b);
  setNonBlocking(fds[0]);
  setNonBlocking(fds[1]);
  return true;
}
inline void wakeSignal(int fd) { (void)::send(static_cast<SOCKET>(fd), "x", 1, 0); }
inline void wakeDrain(int fd) {
  char buf[64];
  while (recv(static_cast<SOCKET>(fd), buf, sizeof buf, 0) > 0) {}
}
}  // namespace vj::net
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace vj::net {
inline void closeSocket(int fd) { ::close(fd); }
inline void setNonBlocking(int fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK); }
inline bool wakePair(int fds[2]) {
  if (pipe(fds) != 0) return false;
  setNonBlocking(fds[0]);
  setNonBlocking(fds[1]);
  return true;
}
inline void wakeSignal(int fd) {
  const char b = 1;
  (void)!write(fd, &b, 1);
}
inline void wakeDrain(int fd) {
  char buf[64];
  while (read(fd, buf, sizeof buf) > 0) {}
}
}  // namespace vj::net
#endif

namespace vj::net {
inline pollfd pollEntry(int fd, short events) {
  pollfd p{};
  p.fd = static_cast<decltype(p.fd)>(fd);
  p.events = events;
  return p;
}
inline void setOption(int fd, int level, int name, int value) {
  setsockopt(fd, level, name, reinterpret_cast<const char*>(&value), sizeof value);
}
}  // namespace vj::net
