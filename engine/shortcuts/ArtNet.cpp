#include "shortcuts/ArtNet.h"

#include "core/Net.h"

#include <algorithm>
#include <cstring>

namespace vj::shortcuts {

std::optional<ArtDmx> parseArtDmx(const uint8_t* d, size_t size) {
  if (size < 18 || std::memcmp(d, "Art-Net\0", 8) != 0) return std::nullopt;
  if (d[8] != 0x00 || d[9] != 0x50) return std::nullopt;  // OpDmx, en little endian
  const size_t length = (size_t{d[16]} << 8) | d[17];
  if (length == 0 || length > 512 || size < 18 + length) return std::nullopt;
  ArtDmx out;
  out.universe = d[14] | ((d[15] & 0x7F) << 8);
  out.channels.assign(d + 18, d + 18 + length);
  return out;
}

bool ArtNetReceiver::start(int port) {
  stop();
  const int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) return false;
  const int yes = 1;
  net::setOption(fd, SOL_SOCKET, SO_REUSEADDR, yes);
#ifdef SO_REUSEPORT
  net::setOption(fd, SOL_SOCKET, SO_REUSEPORT, yes);
#endif
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(static_cast<uint16_t>(port));
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
    net::closeSocket(fd);
    return false;
  }
  fd_ = fd;
  running_ = true;
  thread_ = std::thread([this, fd] { run(fd); });
  return true;
}

void ArtNetReceiver::stop() {
  if (!running_.exchange(false)) return;
  thread_.join();
  net::closeSocket(fd_);
  fd_ = -1;
}

void ArtNetReceiver::run(int fd) {
  uint8_t buf[1024];
  while (running_) {
    pollfd p = net::pollEntry(fd, POLLIN);
    if (poll(&p, 1, 200) <= 0) continue;
    const ssize_t n = recv(fd, reinterpret_cast<char*>(buf), sizeof buf, 0);
    if (n <= 0) continue;
    if (auto dmx = parseArtDmx(buf, static_cast<size_t>(n))) {
      std::lock_guard lock(mutex_);
      Universe& u = universes_[dmx->universe];
      std::copy(dmx->channels.begin(), dmx->channels.end(), u.begin());
    }
  }
}

std::map<int, Universe> ArtNetReceiver::snapshot() {
  std::lock_guard lock(mutex_);
  return universes_;
}

}  // namespace vj::shortcuts
