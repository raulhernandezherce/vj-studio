#pragma once
#include <map>
#include <memory>
#include <string>

#include "compositor/CompositionModule.h"
#include "core/Module.h"
#include "output/OutputModule.h"
#include "patch/Patch.h"
#include "patch/Runtime.h"

namespace vj::patch {

// Editor de nodos: un parche de trabajo y los parches guardados, que entran en el catálogo
// como efectos o fuentes y se ejecutan dentro de la cadena normal.
class PatchModule final : public IModule, public compositor::IExternalEffects {
 public:
  explicit PatchModule(std::string documents) : documents_(std::move(documents)) {}
  std::string name() const override { return "patch"; }
  bool start(EngineContext& ctx) override;
  void tick(double dt) override;
  void stop() override;

  bool render(const compositor::PlanEffect& effect, gfx::IRenderBackend& gpu, gfx::TargetId input, gfx::TargetId output, int width, int height, float time, float bpm, float beat) override;
  void endFrame() override;
  void releaseGpu() override;

 private:
  struct Saved {
    Patch patch;
    std::string dir;
    schema::EffectType type;
    std::vector<Exposed> exposed;
  };
  struct Instance {
    std::unique_ptr<Runtime> runtime;
    bool used = true;
  };
  std::string folder() const;
  std::string fileFor(const std::string& name) const;
  void registerPatch(const Patch& patch, const std::string& dir);
  void scanFolder();
  Programs& programs(gfx::IRenderBackend& gpu);
  nlohmann::json consolidate(const std::string& name);

  EngineContext* ctx_ = nullptr;
  compositor::CompositionModule* module_ = nullptr;
  std::string documents_;
  std::unique_ptr<NodeLibrary> library_;
  Patch working_;
  std::unique_ptr<Runtime> workingRuntime_;
  bool workingDirty_ = true;
  int previewNode_ = 0;
  gfx::TargetId previewTarget_ = gfx::kNoTarget;
  gfx::IRenderBackend* gpu_ = nullptr;
  std::unique_ptr<Programs> programs_;
  std::map<std::string, Saved> saved_;
  std::map<std::string, Instance> instances_;
  double time_ = 0, sinceMonitor_ = 0;
};

}  // namespace vj::patch
