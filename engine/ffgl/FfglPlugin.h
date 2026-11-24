#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "compositor/RenderPlan.h"
#include "ffgl/FfglAbi.h"
#include "gfx/IRenderBackend.h"

namespace vj::ffgl {

// Lee la descripción de un plugin en este proceso. Lanza std::runtime_error si no es válido.
nlohmann::json probePlugin(const std::string& path);

// Lo mismo en un proceso hijo: un plugin que se cuelga o se bloquea no afecta al motor.
nlohmann::json probeInChild(const std::string& engineExe, const std::string& path, int timeoutMs);

// Biblioteca cargada e instancias vivas, siempre en el hilo de render.
class PluginRuntime final : public compositor::IExternalEffects {
 public:
  ~PluginRuntime() override;
  void add(const std::string& typeId, const std::string& path);
  bool render(const compositor::PlanEffect& effect, gfx::IRenderBackend& gpu, gfx::TargetId input, gfx::TargetId output,
              int width, int height, float time, float bpm, float beat) override;
  void endFrame() override;
  void releaseGpu() override;

 private:
  struct Library {
    std::string path;
    void* handle = nullptr;
    abi::MainFn main = nullptr;
    bool initialised = false;
    bool failed = false;
    uint32_t type = abi::kEffect;
    bool wantsTime = false;
  };
  struct Instance {
    Library* library = nullptr;
    abi::InstanceId id = nullptr;
    int width = 0, height = 0;
    std::vector<float> values;
    bool used = false;
  };
  Library* load(const std::string& typeId);
  void destroy(Instance& instance);

  std::map<std::string, Library> libraries_;
  std::map<std::string, Instance> instances_;
};

}  // namespace vj::ffgl
