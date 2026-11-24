#include "gfx/IRenderBackend.h"

namespace vj::gfx {
namespace {

class NullBackend final : public IRenderBackend {
 public:
  std::string name() const override { return "null"; }
  TextureId uploadTexture(TextureId id, int, int, PixelFormat, int, const void*) override {
    return id != kNoTexture ? id : ++next_;
  }
  void destroyTexture(TextureId) override {}
  TargetId createTarget(int, int, bool) override { return ++next_; }
  void destroyTarget(TargetId) override {}
  TextureId targetTexture(TargetId id) override { return id; }
  ProgramId createProgram(const std::string&) override { return ++next_; }
  void beginPass(TargetId, bool) override {}
  void draw(const DrawCall&) override {}
  ProgramId createMeshProgram(const std::string&) override { return ++next_; }
  void drawMesh(const MeshCall&) override {}
  void clearTarget(TargetId, float, float, float, float) override {}
  void setPresentContext(int) override {}
  void releasePresentContext(int) override {}
  bool runExternal(TargetId, TargetId, const std::function<void(uint32_t, uint32_t)>&) override { return false; }
  void present(TextureId, int, int, const DrawParams&) override {}
  std::vector<uint8_t> readback(TargetId, int width, int height) override {
    return std::vector<uint8_t>(static_cast<size_t>(width) * height * 4, 0);
  }

 private:
  uint32_t next_ = 0;
};

}  // namespace

std::unique_ptr<IRenderBackend> makeNullBackend() { return std::make_unique<NullBackend>(); }

}  // namespace vj::gfx
