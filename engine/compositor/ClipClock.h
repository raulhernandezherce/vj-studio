#pragma once
#include <cstdint>

namespace vj::compositor {

enum class PlayMode { Loop, PingPong, Random, OnceEject, OnceHold };
enum class Direction { Forward, Backward, Pause };

struct ClockConfig {
  double length = 0;          // segundos del archivo
  double in = 0, out = 1;     // fracciones de length
  double rate = 1;            // segundos de archivo por segundo real
  Direction direction = Direction::Forward;
  PlayMode mode = PlayMode::Loop;
  double randomInterval = 0.5;
  double randomDistance = 1;
};

struct ClockStep {
  bool looped = false;    // completó una vuelta
  bool finished = false;  // llegó al final en un modo de una pasada
};

// Reloj de un clip en reproducción. Puro: solo avanza con dt, sin vídeo ni GPU.
class ClipClock {
 public:
  ClipClock(uint32_t seed = 1) : rng_(seed) {}
  void restart(const ClockConfig& c);
  ClockStep advance(double dt, const ClockConfig& c);
  void seek(double seconds) { pos_ = seconds; }
  double position() const { return pos_; }
  // +1 o -1 según vaya el rebote: el audio de ping-pong lee en ese sentido.
  int bounce() const { return bounce_; }

 private:
  double random();
  double pos_ = 0;
  int bounce_ = 1;
  bool done_ = false;
  double sinceJump_ = 0;
  uint32_t rng_;
};

}  // namespace vj::compositor
