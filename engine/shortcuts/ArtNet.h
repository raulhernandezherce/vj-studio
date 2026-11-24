#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace vj::shortcuts {

struct ArtDmx {
  int universe = 0;  // Subnet × 16 + Universe, con la red en los bits altos
  std::vector<uint8_t> channels;
};

// Nulo si el paquete no es ArtDmx válido.
std::optional<ArtDmx> parseArtDmx(const uint8_t* data, size_t size);

using Universe = std::array<uint8_t, 512>;

// Receptor UDP en su propio hilo. Guarda el último estado de cada universo.
class ArtNetReceiver {
 public:
  ~ArtNetReceiver() { stop(); }
  bool start(int port);
  void stop();
  std::map<int, Universe> snapshot();
  bool active() const { return running_; }

 private:
  void run(int fd);
  int fd_ = -1;
  std::atomic<bool> running_{false};
  std::thread thread_;
  std::mutex mutex_;
  std::map<int, Universe> universes_;
};

}  // namespace vj::shortcuts
