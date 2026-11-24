#include "golden/gl_fixture.h"

using namespace vj;

using GlBackend = GlContext;

namespace {
const char* kShow = R"(#version 410 core
uniform sampler2D uTex0;
in vec2 vUv;
out vec4 color;
void main() { color = texture(uTex0, vUv); }
)";
}  // namespace

TEST_F(GlBackend, ReportsName) { EXPECT_EQ(backend_->name(), "OpenGL 4.1"); }

TEST_F(GlBackend, Bgra8ChannelOrder) {
  const uint8_t px[] = {10, 20, 200, 255};
  const auto tex = backend_->uploadTexture(gfx::kNoTexture, 1, 1, gfx::PixelFormat::BGRA8, 4, px);
  const auto t = backend_->createTarget(8, 8);
  backend_->beginPass(t, true);
  gfx::DrawCall d;
  d.program = backend_->createProgram(kShow);
  d.textures[0] = tex;
  backend_->draw(d);
  const auto out = backend_->readback(t, 8, 8);
  EXPECT_EQ(out[0], 200);
  EXPECT_EQ(out[2], 10);
}

TEST_F(GlBackend, TopRowStaysOnTopInReadback) {
  const uint8_t px[] = {255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 255, 0, 0, 0, 255};
  const auto tex = backend_->uploadTexture(gfx::kNoTexture, 2, 2, gfx::PixelFormat::RGBA8, 8, px);
  const auto t = backend_->createTarget(8, 8);
  backend_->beginPass(t, true);
  gfx::DrawCall d;
  d.program = backend_->createProgram(kShow);
  d.textures[0] = tex;
  backend_->draw(d);
  const auto out = backend_->readback(t, 8, 8);
  EXPECT_GT(out[0], 200);
  EXPECT_LT(out[static_cast<size_t>(7) * 8 * 4], 50);
}

TEST_F(GlBackend, BrokenShaderThrows) {
  EXPECT_THROW(backend_->createProgram("no es glsl"), std::runtime_error);
}
