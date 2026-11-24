#include "compositor/ClipClock.h"

#include <algorithm>
#include <cmath>

namespace vj::compositor {

double ClipClock::random() {
  rng_ = rng_ * 1664525u + 1013904223u;
  return (rng_ >> 8) / 16777216.0;
}

void ClipClock::restart(const ClockConfig& c) {
  const double a = c.length * std::min(c.in, c.out), b = c.length * std::max(c.in, c.out);
  pos_ = c.direction == Direction::Backward ? b : a;
  bounce_ = 1;
  done_ = false;
  sinceJump_ = 0;
}

ClockStep ClipClock::advance(double dt, const ClockConfig& c) {
  ClockStep step;
  const double a = c.length * std::min(c.in, c.out), b = c.length * std::max(c.in, c.out);
  const double span = b - a;
  if (c.direction == Direction::Pause || span <= 0 || done_) return step;

  if (c.mode == PlayMode::Random) {
    sinceJump_ += dt * (c.rate > 0 ? std::max(c.rate, 1e-6) : 0);
    while (sinceJump_ >= c.randomInterval) {
      sinceJump_ -= c.randomInterval;
      const double reach = span * c.randomDistance;
      // Uniforme dentro del alcance: recortar el salto dejaría el clip pegado a los bordes.
      const double lo = std::max(a, pos_ - reach), hi = std::min(b, pos_ + reach);
      pos_ = lo + random() * (hi - lo);
    }
    return step;
  }

  const double dir = c.direction == Direction::Forward ? 1 : -1;
  pos_ += dt * c.rate * dir * bounce_;

  switch (c.mode) {
    case PlayMode::Loop:
      while (pos_ >= b) { pos_ -= span; step.looped = true; }
      while (pos_ < a) { pos_ += span; step.looped = true; }
      break;
    case PlayMode::PingPong:
      for (int i = 0; i < 64 && (pos_ > b || pos_ < a); ++i) {
        const bool high = pos_ > b;
        pos_ = high ? 2 * b - pos_ : 2 * a - pos_;
        bounce_ = -bounce_;
        // Una vuelta termina al regresar al punto de salida.
        if (high == (dir < 0)) step.looped = true;
      }
      break;
    case PlayMode::OnceEject:
    case PlayMode::OnceHold:
      if (dir > 0 ? pos_ >= b : pos_ <= a) {
        pos_ = dir > 0 ? b : a;
        done_ = true;
        step.finished = true;
        step.looped = true;
      }
      break;
    case PlayMode::Random:
      break;
  }
  return step;
}

}  // namespace vj::compositor
