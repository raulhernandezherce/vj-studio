#pragma once
#include <deque>

namespace vj::shortcuts {

// Sigue un reloj MIDI (24 pulsos por negra). Puro: recibe el instante de cada pulso.
class MidiClock {
 public:
  void start(double now);   // FA: el beat vuelve a cero
  void resume();            // FB
  void stop();              // FC
  void tick(double now);    // F8
  bool running() const { return running_; }
  bool locked() const { return times_.size() >= kWindow / 2; }
  double bpm() const;
  double beat(double now) const;

 private:
  static constexpr size_t kWindow = 96;  // cuatro negras: promedia la fluctuación
  std::deque<double> times_;
  long ticks_ = 0;
  bool running_ = false;
};

}  // namespace vj::shortcuts
