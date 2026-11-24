#pragma once
#include <nlohmann/json.hpp>
#include <string>

#include "core/Module.h"
#include "ffgl/FfglPlugin.h"

namespace vj::compositor {
class Composition;
class CompositionModule;
}

namespace vj::ffgl {

// Busca plugins en Extra Effects y en las carpetas de las preferencias, y los añade al catálogo.
class FfglModule final : public IModule {
 public:
  // engineExe: binario para explorar en un proceso aparte; vacío, en este proceso.
  FfglModule(std::string documents, std::string engineExe) : documents_(std::move(documents)), exe_(std::move(engineExe)) {}
  std::string name() const override { return "ffgl"; }
  bool start(EngineContext& ctx) override;
  void stop() override;
  nlohmann::json scan();

 private:
  void scanFolder(const std::string& folder, nlohmann::json& errors);

  EngineContext* ctx_ = nullptr;
  compositor::CompositionModule* module_ = nullptr;
  std::string documents_, exe_;
  std::unique_ptr<PluginRuntime> runtime_;
};

}  // namespace vj::ffgl
