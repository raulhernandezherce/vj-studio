#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace vj::shortcuts {

// Segundos del reloj de CoreMIDI: el mismo que marcan los paquetes.
double hostSeconds();

struct MidiMessage {
  std::string device;
  uint8_t status = 0, data1 = 0, data2 = 0;
  double time = 0;  // segundos, reloj del sistema
};

// Todas las fuentes MIDI del sistema, incluidos puertos virtuales. Los mensajes llegan en un
// hilo de CoreMIDI: se encolan y el hilo principal los recoge con take().
class CoreMidiPort {
 public:
  CoreMidiPort();
  ~CoreMidiPort();
  bool ok() const { return ok_; }
  std::vector<MidiMessage> take();
  std::vector<std::string> inputs() const;
  void send(const std::string& device, const std::vector<uint8_t>& bytes);  // device vacío: a todos
  void refresh();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  bool ok_ = false;
};

}  // namespace vj::shortcuts
