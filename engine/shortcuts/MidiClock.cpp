#include "shortcuts/MidiClock.h"

#include <algorithm>

namespace vj::shortcuts {

void MidiClock::start(double) {
  ticks_ = 0;
  times_.clear();
  running_ = true;
}

void MidiClock::resume() { running_ = true; }

void MidiClock::stop() { running_ = false; }

void MidiClock::tick(double now) {
  if (!running_) running_ = true;  // hay relojes que nunca envían Start
  ++ticks_;
  times_.push_back(now);
  if (times_.size() > kWindow) times_.pop_front();
}

double MidiClock::bpm() const {
  if (times_.size() < 2) return 0;
  const double span = times_.back() - times_.front();
  return span > 0 ? 60.0 * static_cast<double>(times_.size() - 1) / 24.0 / span : 0;
}

double MidiClock::beat(double now) const {
  // Los pulsos cuentan el tiempo real: la deriva no se acumula. Entre pulsos se interpola.
  const double base = static_cast<double>(ticks_) / 24.0;
  if (times_.empty() || !running_) return base;
  const double perSecond = bpm() / 60.0;
  return base + std::clamp(now - times_.back(), 0.0, 1.0 / 12) * perSecond;
}

}  // namespace vj::shortcuts
