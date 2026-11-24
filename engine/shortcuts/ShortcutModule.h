#pragma once
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/Module.h"
#include "core/ParameterRegistry.h"
#include "shortcuts/ArtNet.h"
#include "shortcuts/CoreMidiPort.h"
#include "shortcuts/MidiClock.h"
#include "shortcuts/ShortcutEngine.h"

namespace vj::compositor {
class Composition;
}

namespace vj::shortcuts {

// Une dispositivos, atajos y presets. Todo se aplica en el hilo principal.
class ShortcutModule final : public IModule {
 public:
  explicit ShortcutModule(std::string documents) : documents_(std::move(documents)) {}
  std::string name() const override { return "shortcuts"; }
  bool start(EngineContext& ctx) override;
  void tick(double dt) override;
  void stop() override;
  ShortcutEngine& engine() { return *engine_; }
  void inject(const ControlEvent& e) { engine_->handle(e); }

 private:
  void registerCommands();
  void handleMidi(const MidiMessage& m);
  void pollDmx();
  void updateClockOutput();
  void savePreset();
  void loadPreset(const std::string& name);
  std::string presetPath(const std::string& name) const;

  EngineContext* ctx_ = nullptr;
  compositor::Composition* comp_ = nullptr;
  std::string documents_;
  std::unique_ptr<ShortcutEngine> engine_;
  std::unique_ptr<CoreMidiPort> midi_;
  ArtNetReceiver artnet_;
  MidiClock clock_;
  double sinceDevices_ = 1e9, clockSeen_ = -1;
  std::map<std::string, double> lastDmx_;
  std::string dmxApplied_;

  std::atomic<bool> clockOut_{false};
  std::thread clockThread_;
  std::atomic<double> bpm_{120};

  std::mutex mutex_;
  std::deque<ControlEvent> external_;
  std::deque<std::pair<std::string, double>> outgoing_;
  std::deque<std::string> monitor_;
  ParameterRegistry::ListenerId listener_ = 0;
  uint64_t handler_ = 0;
};

}  // namespace vj::shortcuts
