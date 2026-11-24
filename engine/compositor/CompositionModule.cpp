#include "compositor/CompositionModule.h"

namespace vj::compositor {

bool CompositionModule::start(EngineContext& ctx) {
  composition_ = std::make_unique<Composition>(ctx.params, ctx.events, shape_);
  return true;
}

gfx::TargetId CompositionModule::renderFrame(gfx::IRenderBackend& backend, int& width, int& height) {
  if (!renderer_) {
    renderer_ = std::make_unique<Renderer>(backend);
    renderer_->setExternal(external_);
    renderer_->setPatches(patches_);
  }
  const RenderPlan plan = composition_->plan();
  width = plan.width;
  height = plan.height;
  const gfx::TargetId result = renderer_->render(plan);
  if (external_) external_->endFrame();
  if (patches_) patches_->endFrame();
  return result;
}

gfx::TargetId CompositionModule::renderPreview(gfx::IRenderBackend& backend, int width, int height) {
  if (!composition_->previewActive()) return gfx::kNoTarget;
  if (!previewRenderer_) {
    previewRenderer_ = std::make_unique<Renderer>(backend);
    previewRenderer_->setExternal(external_);
    previewRenderer_->setPatches(patches_);
  }
  return previewRenderer_->render(composition_->previewPlan(width, height));
}

}  // namespace vj::compositor
