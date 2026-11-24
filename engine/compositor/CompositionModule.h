#pragma once
#include <memory>

#include "compositor/Composition.h"
#include "compositor/Renderer.h"
#include "core/Module.h"

namespace vj::compositor {

// Lo que la salida pide cada fotograma, sin saber cómo se compone.
class IFrameRenderer {
 public:
  virtual ~IFrameRenderer() = default;
  // Devuelve el destino terminado y su tamaño.
  virtual gfx::TargetId renderFrame(gfx::IRenderBackend& backend, int& width, int& height) = 0;
  // Imagen reducida para el monitor Preview. kNoTarget si no hay nada en preview.
  virtual gfx::TargetId renderPreview(gfx::IRenderBackend& backend, int width, int height) = 0;
  virtual gfx::TargetId scaleForMonitor(gfx::TargetId source, int width, int height) = 0;
  virtual gfx::TextureId routedLayer(int layer) = 0;
  virtual gfx::TextureId routedGroup(int group) = 0;
  virtual gfx::TextureId routedBus(int bus) = 0;
  virtual void setRoutedBuses(bool a, bool b) = 0;
  virtual void setRouted(std::map<int, bool> layers, std::map<int, bool> groups) = 0;
  virtual void releaseGpu() = 0;
};

class CompositionModule final : public IModule, public IFrameRenderer {
 public:
  explicit CompositionModule(CompositionShape shape) : shape_(shape) {}
  std::string name() const override { return "composition"; }
  bool start(EngineContext& ctx) override;
  void tick(double dt) override { composition_->tick(dt); }
  void stop() override { composition_.reset(); }
  gfx::TargetId renderFrame(gfx::IRenderBackend& backend, int& width, int& height) override;
  gfx::TargetId renderPreview(gfx::IRenderBackend& backend, int width, int height) override;
  gfx::TargetId scaleForMonitor(gfx::TargetId source, int width, int height) override {
    return renderer_->downscale(source, width, height);
  }
  gfx::TextureId routedLayer(int layer) override { return renderer_->routedLayer(layer); }
  gfx::TextureId routedGroup(int group) override { return renderer_->routedGroup(group); }
  gfx::TextureId routedBus(int bus) override { return renderer_->routedBus(bus); }
  void setRoutedBuses(bool a, bool b) override { composition_->setRoutedBuses(a, b); }
  void setRouted(std::map<int, bool> layers, std::map<int, bool> groups) override { composition_->setRouted(std::move(layers), std::move(groups)); }
  void releaseGpu() override {
    if (external_) external_->releaseGpu();
    if (patches_) patches_->releaseGpu();
    renderer_.reset();
    previewRenderer_.reset();
  }
  Composition* composition() { return composition_.get(); }
  void setExternal(IExternalEffects* external) { external_ = external; }
  void setPatches(IExternalEffects* patches) {
    patches_ = patches;
    if (renderer_) renderer_->setPatches(patches);
    if (previewRenderer_) previewRenderer_->setPatches(patches);
  }

 private:
  CompositionShape shape_;
  std::unique_ptr<Composition> composition_;
  std::unique_ptr<Renderer> renderer_, previewRenderer_;
  IExternalEffects* external_ = nullptr;
  IExternalEffects* patches_ = nullptr;
};

}  // namespace vj::compositor
