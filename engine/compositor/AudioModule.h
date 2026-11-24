#pragma once
#include <future>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "animation/Levels.h"
#include "audio/AudioEngine.h"
#include "core/Module.h"
#include "core/ParameterRegistry.h"

namespace vj::compositor {

class Composition;

// Une la composición con el mezclador y el dispositivo. Abre los dispositivos fuera del hilo
// principal, de modo que cambiar de salida nunca detiene el vídeo.
class AudioModule final : public IModule, public animation::ILevels {
 public:
  explicit AudioModule(bool useDevice) : useDevice_(useDevice) {}
  std::string name() const override { return "audio"; }
  bool start(EngineContext& ctx) override;
  void tick(double dt) override;
  void stop() override;
  double level(const std::string& source, const std::string& owner, double lowHz, double highHz) override;
  audio::AudioEngine* engine() { return engine_.get(); }

 private:
  void reopen();
  int tapFor(const std::string& source, const std::string& owner) const;

  bool useDevice_;
  EngineContext* ctx_ = nullptr;
  Composition* comp_ = nullptr;
  std::unique_ptr<audio::AudioEngine> engine_;
  std::unique_ptr<audio::AudioDevice> output_, input_;
  std::future<std::unique_ptr<audio::AudioDevice>> openingOutput_, openingInput_;
  std::string wantedKey_;
  std::map<int, std::vector<float>> spectra_;
  double sinceMeter_ = 0, sinceDevices_ = 1e9;
  std::vector<ParameterRegistry::ListenerId> listeners_;
};

}  // namespace vj::compositor
