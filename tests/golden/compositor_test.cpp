#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>

#include "compositor/Composition.h"
#include "compositor/Renderer.h"
#include "core/generated/BlendModes.gen.h"
#include "core/generated/Parameters.gen.h"
#include "golden/gl_fixture.h"

bool writeFixturePng(const std::string& path);

using namespace vj;
using namespace vj::compositor;

namespace {

constexpr int kW = 64, kH = 36;
struct Rgb { double r, g, b; };

std::string hex(Rgb c) {
  char s[8];
  std::snprintf(s, sizeof s, "%02X%02X%02X", int(c.r * 255 + .5), int(c.g * 255 + .5), int(c.b * 255 + .5));
  return std::string("vj://solid/") + s;
}

// Referencia independiente del shader, escrita por canal.
double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }
double blendChannel(const std::string& m, double b, double s) {
  if (m == "Add") return std::min(1.0, b + s);
  if (m == "Alpha") return s;
  if (m == "Darken") return std::min(b, s);
  if (m == "Multiply") return b * s;
  if (m == "Burn") return b >= 1 ? 1 : s <= 0 ? 0 : 1 - std::min(1.0, (1 - b) / s);
  if (m == "Lighten") return std::max(b, s);
  if (m == "Screen") return 1 - (1 - b) * (1 - s);
  if (m == "Dodge") return b <= 0 ? 0 : s >= 1 ? 1 : std::min(1.0, b / (1 - s));
  if (m == "Hard Light") return s < 0.5 ? 2 * b * s : 1 - 2 * (1 - b) * (1 - s);
  if (m == "Overlay") return b < 0.5 ? 2 * b * s : 1 - 2 * (1 - b) * (1 - s);
  if (m == "Soft Light") {
    const double d = b <= 0.25 ? ((16 * b - 12) * b + 4) * b : std::sqrt(b);
    return s <= 0.5 ? b - (1 - 2 * s) * b * (1 - b) : b + (2 * s - 1) * (d - b);
  }
  if (m == "Subtract") return std::max(0.0, b - s);
  if (m == "Difference") return std::abs(b - s);
  if (m == "Difference I") return 1 - std::abs(b - s);
  if (m == "Exclusion") return b + s - 2 * b * s;
  return -1;
}
double luma(Rgb c) { return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; }
double smooth(double a, double b, double x) {
  const double t = clamp01((x - a) / (b - a));
  return t * t * (3 - 2 * t);
}
// Mezcla de opacos: devuelve el canal esperado sobre fondo opaco.
double expected(const std::string& m, double b, double s, double l, double o, int ch) {
  if (m == "Cut") return o >= 0.5 ? s : b;
  if (m == "Luma Key") { const double a = smooth(0.1, 0.4, l) * o; return s * a + b * (1 - a); }
  if (m == "Luma Key I") { const double a = (1 - smooth(0.1, 0.4, l)) * o; return s * a + b * (1 - a); }
  if (m == "Luma is Alpha") { const double a = l * o; return s * a + b * (1 - a); }
  if (m == "50Mask") return b * (1 - o) + b * l * o;
  if (m == "RGB") { const double k = clamp01(o * 3 - ch); return b + (s - b) * k; }
  const double r = clamp01(blendChannel(m, b, s));
  return b * (1 - o) + r * o;
}

}  // namespace

class Compositor : public GlContext {
 protected:
  void SetUp() override {
    GlContext::SetUp();
    if (IsSkipped()) return;
    params_.defineAll(schema::parameters());
    params_.set("/composition/width", int64_t{kW});
    params_.set("/composition/height", int64_t{kH});
    comp_ = std::make_unique<Composition>(params_, events_, CompositionShape{4, 2, 2});
    renderer_ = std::make_unique<Renderer>(*backend_);
  }
  void TearDown() override {
    renderer_.reset();
    comp_.reset();
    GlContext::TearDown();
  }
  std::string L(int l) { return "/composition/layers/" + std::to_string(l); }
  std::string C(int l, int c = 1) { return L(l) + "/clips/" + std::to_string(c); }
  void clip(int l, const std::string& path, int c = 1) {
    params_.set(C(l, c) + "/path", path);
    comp_->tick(0);
    comp_->waitForLoads();
    comp_->tick(0);
    params_.trigger(C(l, c) + "/connect");
    comp_->tick(0);
  }
  void frame(double dt = 1.0 / 60) {
    comp_->tick(dt);
    const auto plan = comp_->plan();
    last_ = renderer_->render(plan);
    pixels_ = backend_->readback(last_, plan.width, plan.height);
    width_ = plan.width;
  }
  // Compuesto sobre negro, como lo presenta la salida.
  Rgb at(double u, double v) {
    const size_t i = (static_cast<size_t>(v * (pixels_.size() / 4 / width_)) * width_ + static_cast<size_t>(u * width_)) * 4;
    return {pixels_[i] / 255.0, pixels_[i + 1] / 255.0, pixels_[i + 2] / 255.0};
  }
  double alpha(double u, double v) {
    const size_t i = (static_cast<size_t>(v * kH) * kW + static_cast<size_t>(u * kW)) * 4;
    return pixels_[i + 3] / 255.0;
  }
  void expectColor(Rgb got, Rgb want, double tol = 1.5 / 255, const std::string& what = "") {
    EXPECT_NEAR(got.r, want.r, tol) << what;
    EXPECT_NEAR(got.g, want.g, tol) << what;
    EXPECT_NEAR(got.b, want.b, tol) << what;
  }

  ParameterRegistry params_;
  EventBus events_;
  std::unique_ptr<Composition> comp_;
  std::unique_ptr<Renderer> renderer_;
  gfx::TargetId last_ = 0;
  std::vector<uint8_t> pixels_;
  int width_ = kW;
};

TEST_F(Compositor, EveryBlendModeMatchesReference) {
  const Rgb pairs[][2] = {{{0.2, 0.4, 0.6}, {0.8, 0.5, 0.1}}, {{0.9, 0.1, 0.5}, {0.3, 0.7, 0.2}}, {{0, 1, 0.5}, {1, 0, 0.5}}};
  const auto stateful = [](const std::string& m) { return m == "Displace"; };
  for (const auto& mode : schema::blendModes()) {
    const std::string m = mode.name;
    if (stateful(m)) continue;
    params_.set(L(2) + "/blend", m);
    for (const auto& pair : pairs) {
      const Rgb b = pair[0], s = pair[1];
      params_.set(L(1) + "/blend", std::string("Alpha"));
      clip(1, hex(b));
      clip(2, hex(s));
      for (double o : {1.0, 0.5}) {
        params_.set(L(2) + "/opacity", o);
        frame(0);
        // Se compara contra los valores de 8 bits que realmente entraron a la GPU.
        const auto q = [](double v) { return std::round(v * 255) / 255; };
        const Rgb qb{q(b.r), q(b.g), q(b.b)}, qs{q(s.r), q(s.g), q(s.b)};
        const double l = luma(qs);
        Rgb want{expected(m, qb.r, qs.r, l, o, 0), expected(m, qb.g, qs.g, l, o, 1), expected(m, qb.b, qs.b, l, o, 2)};
        expectColor(at(0.5, 0.5), want, 1.5 / 255, m + " o=" + std::to_string(o));
      }
    }
  }
}

TEST_F(Compositor, DisplaceShiftsPixelsSideways) {
  params_.set(L(1) + "/blend", std::string("Alpha"));
  clip(1, "vj://pattern");
  frame(0);
  const Rgb before = at(0.45, 0.2);
  params_.set(L(2) + "/blend", std::string("Displace"));
  clip(2, "vj://solid/FFFFFF");
  frame(0);
  const Rgb after = at(0.45, 0.2);
  EXPECT_TRUE(std::abs(before.r - after.r) + std::abs(before.g - after.g) + std::abs(before.b - after.b) > 0.1);
}

TEST_F(Compositor, DefaultAddSumsAndBlackIsTransparent) {
  clip(1, "vj://solid/402000");
  clip(2, "vj://solid/000000");
  frame(0);
  expectColor(at(0.5, 0.5), {0x40 / 255.0, 0x20 / 255.0, 0});
  clip(2, "vj://solid/102030");
  frame(0);
  expectColor(at(0.5, 0.5), {0x50 / 255.0, 0x40 / 255.0, 0x30 / 255.0});
}

TEST_F(Compositor, LayerOneIsAtTheBottom) {
  params_.set(L(1) + "/blend", std::string("Alpha"));
  params_.set(L(2) + "/blend", std::string("Alpha"));
  clip(1, "vj://solid/FF0000");
  clip(2, "vj://solid/00FF00");
  frame(0);
  expectColor(at(0.5, 0.5), {0, 1, 0});
}

TEST_F(Compositor, BypassAndSoloWithFourLayers) {
  for (int l = 1; l <= 4; ++l) params_.set(L(l) + "/blend", std::string("Alpha"));
  clip(1, "vj://solid/FF0000");
  clip(2, "vj://solid/00FF00");
  clip(3, "vj://solid/0000FF");
  clip(4, "vj://solid/FFFFFF");
  frame(0);
  expectColor(at(0.5, 0.5), {1, 1, 1});
  params_.set(L(4) + "/bypass", true);
  frame(0);
  expectColor(at(0.5, 0.5), {0, 0, 1});
  params_.set(L(4) + "/bypass", false);
  params_.set(L(2) + "/solo", true);
  frame(0);
  expectColor(at(0.5, 0.5), {0, 1, 0});
  params_.set(L(2) + "/bypass", true);
  frame(0);
  expectColor(at(0.5, 0.5), {0, 0, 0});
}

TEST_F(Compositor, MasterBypassAndResolution) {
  clip(1, "vj://solid/FFFFFF");
  params_.set("/composition/master", 0.5);
  frame(0);
  expectColor(at(0.5, 0.5), {0.5, 0.5, 0.5}, 2.0 / 255);
  params_.set("/composition/bypass", true);
  frame(0);
  expectColor(at(0.5, 0.5), {0, 0, 0});
  params_.set("/composition/bypass", false);
  params_.set("/composition/master", 1.0);
  params_.set("/composition/width", int64_t{128});
  params_.set("/composition/height", int64_t{72});
  frame(0);
  EXPECT_EQ(pixels_.size(), size_t{128 * 72 * 4});
  expectColor(at(0.5, 0.5), {1, 1, 1});
}

TEST_F(Compositor, ResizeFourModesOn4by3) {
  // Generador de 16×9 en composición 4:3 equivale al caso inverso del requisito.
  params_.set("/composition/width", int64_t{40});
  params_.set("/composition/height", int64_t{30});
  clip(1, "vj://solid/FFFFFF");
  params_.set(C(1) + "/resize", std::string("Fit"));
  frame(0);
  EXPECT_LT(at(0.5, 0.05).r, 0.1) << "Fit deja bandas";
  params_.set(C(1) + "/resize", std::string("Fill"));
  frame(0);
  EXPECT_GT(at(0.5, 0.05).r, 0.9);
  params_.set(C(1) + "/resize", std::string("Stretch"));
  frame(0);
  EXPECT_GT(at(0.5, 0.05).r, 0.9);
  params_.set(C(1) + "/resize", std::string("Original"));
  frame(0);
  EXPECT_LT(at(0.1, 0.5).r, 0.1) << "Original conserva 16 px de ancho";
  EXPECT_GT(at(0.5, 0.5).r, 0.9);
}

TEST_F(Compositor, TransformRotate90Scale50) {
  params_.set(C(1) + "/resize", std::string("Stretch"));
  clip(1, "vj://solid/FFFFFF");
  params_.set(C(1) + "/transform/scale", 0.5);
  params_.set(C(1) + "/transform/rotation", 90.0);
  frame(0);
  // Cuadro de 32×18 girado: queda de 18 de ancho y 32 de alto, centrado.
  EXPECT_GT(at(0.5, 0.1).r, 0.9);
  EXPECT_LT(at(0.2, 0.5).r, 0.1);
}

TEST_F(Compositor, LayerTransformMovesResult) {
  params_.set(C(1) + "/resize", std::string("Stretch"));
  clip(1, "vj://solid/FFFFFF");
  params_.set(L(1) + "/transform/scale", 0.5);
  params_.set(L(1) + "/transform/x", 16.0);
  frame(0);
  EXPECT_LT(at(0.2, 0.5).r, 0.1);
  EXPECT_GT(at(0.7, 0.5).r, 0.9);
}

TEST_F(Compositor, GroupRendersApartAndBlendsAsOne) {
  // Fondo rojo fuera del grupo; dentro, verde con Multiply.
  params_.set(L(1) + "/blend", std::string("Alpha"));
  clip(1, "vj://solid/FF0000");
  params_.set(L(2) + "/group", int64_t{1});
  params_.set(L(3) + "/group", int64_t{1});
  params_.set(L(2) + "/blend", std::string("Alpha"));
  params_.set(L(3) + "/blend", std::string("Multiply"));
  clip(2, "vj://solid/FFFFFF");
  clip(3, "vj://solid/00FF00");
  frame(0);
  // Dentro del grupo el blanco se multiplica por verde; el rojo de fuera no interviene.
  expectColor(at(0.5, 0.5), {0, 1, 0});
  params_.set("/composition/groups/1/master", 0.5);
  frame(0);
  expectColor(at(0.5, 0.5), {0.5, 0.5, 0}, 2.0 / 255);
  params_.set("/composition/groups/1/master", 1.0);
  params_.set("/composition/groups/1/bypass", true);
  frame(0);
  expectColor(at(0.5, 0.5), {1, 0, 0});
}

TEST_F(Compositor, FiftyMaskCircleCutsLayerBelow) {
  params_.set(L(1) + "/blend", std::string("Alpha"));
  clip(1, "vj://solid/FFFFFF");
  params_.set(L(2) + "/blend", std::string("50Mask"));
  clip(2, "vj://circle");
  frame(0);
  EXPECT_GT(at(0.5, 0.5).r, 0.9);
  EXPECT_LT(at(0.1, 0.1).r, 0.1);
}

TEST_F(Compositor, MaskAllBelowAndOneBelow) {
  params_.set(L(1) + "/blend", std::string("Alpha"));
  params_.set(L(2) + "/blend", std::string("Add"));
  clip(1, "vj://solid/800000");
  clip(2, "vj://solid/008000");
  clip(3, "vj://circle");
  params_.set(L(3) + "/mask", std::string("All Below"));
  frame(0);
  expectColor(at(0.5, 0.5), {0x80 / 255.0, 0x80 / 255.0, 0});
  expectColor(at(0.05, 0.05), {0, 0, 0});

  params_.set(L(3) + "/mask", std::string("One Below"));
  frame(0);
  expectColor(at(0.5, 0.5), {0x80 / 255.0, 0x80 / 255.0, 0});
  expectColor(at(0.05, 0.05), {0x80 / 255.0, 0, 0});  // solo recorta la capa 2
}

TEST_F(Compositor, TransitionWipeProgressesAndEnds) {
  params_.set(L(1) + "/blend", std::string("Alpha"));
  params_.set(L(1) + "/transition/duration", 2.0);
  params_.set(L(1) + "/transition/blend", std::string("Wipe Left"));
  clip(1, "vj://solid/FF0000", 1);
  clip(1, "vj://solid/00FF00", 2);
  frame(1.0);
  expectColor(at(0.1, 0.5), {1, 0, 0});
  expectColor(at(0.9, 0.5), {0, 1, 0});
  frame(1.1);
  expectColor(at(0.1, 0.5), {0, 1, 0});
}

TEST_F(Compositor, TransitionShadersAllRun) {
  for (const auto& t : schema::transitionModes()) {
    params_.set(L(1) + "/transition/duration", 2.0);
    params_.set(L(1) + "/transition/blend", std::string(t.name));
    params_.trigger(L(1) + "/eject");
    comp_->tick(0);
    clip(1, "vj://solid/FF0000", 1);
    clip(1, "vj://solid/00FF00", 2);
    frame(1.0);
    const Rgb c = at(0.5, 0.5);
    EXPECT_GT(c.r + c.g, 0.4) << t.name;
    EXPECT_LT(c.b, 0.1) << t.name;
  }
}

TEST_F(Compositor, ClipVideoPropertiesApply) {
  params_.set(L(1) + "/blend", std::string("Alpha"));
  clip(1, "vj://solid/FFFFFF");
  params_.set(C(1) + "/video/red", false);
  params_.set(C(1) + "/video/opacity", 0.5);
  frame(0);
  expectColor(at(0.5, 0.5), {0, 0.5, 0.5}, 2.0 / 255);
}

TEST_F(Compositor, ClipBlendOverridesLayer) {
  params_.set(L(1) + "/blend", std::string("Alpha"));
  clip(1, "vj://solid/808080");
  clip(2, "vj://solid/808080");
  params_.set(L(2) + "/blend", std::string("Alpha"));
  params_.set(C(2) + "/video/blend", std::string("Multiply"));
  frame(0);
  EXPECT_LT(at(0.5, 0.5).r, 0.3);
}

TEST_F(Compositor, StraightAlphaPngComposesWithoutHalo) {
  const auto png = std::filesystem::temp_directory_path() / ("vj_comp_" + std::to_string(getpid()) + ".png");
  ASSERT_TRUE(writeFixturePng(png.string()));
  params_.set(L(1) + "/blend", std::string("Alpha"));
  clip(1, png.string());
  frame(0);
  EXPECT_EQ(params_.getString(C(1) + "/video/alphatype"), "Straight");
  expectColor(at(0.5, 0.5), {128 / 255.0, 0, 0}, 2.5 / 255);
  EXPECT_NEAR(alpha(0.5, 0.5), 128 / 255.0, 1.5 / 255);
  // Interpretado como premultiplicado, el color queda el doble de brillante: el halo.
  params_.set(C(1) + "/video/alphatype", std::string("Premultiplied"));
  frame(0);
  EXPECT_GT(at(0.5, 0.5).r, 0.9);
}

TEST_F(Compositor, PatternGoldenIsStable) {
  params_.set(L(1) + "/blend", std::string("Alpha"));
  params_.set(C(1) + "/resize", std::string("Stretch"));
  clip(1, "vj://pattern");
  frame(0);
  expectColor(at(0.06, 0.2), {192 / 255.0, 192 / 255.0, 192 / 255.0}, 2.0 / 255);
  expectColor(at(0.2, 0.2), {192 / 255.0, 192 / 255.0, 0}, 2.0 / 255);
  expectColor(at(0.94, 0.2), {0, 0, 0}, 2.0 / 255);
}

TEST_F(Compositor, PreviewRendersApartFromOutput) {
  params_.set(L(1) + "/blend", std::string("Alpha"));
  clip(1, "vj://solid/FF0000", 1);
  params_.set(C(1, 2) + "/path", std::string("vj://solid/0000FF"));
  comp_->tick(0);
  comp_->waitForLoads();
  comp_->tick(0);
  comp_->previewClip(0, 1);
  frame(0);
  expectColor(at(0.5, 0.5), {1, 0, 0});
  Renderer preview(*backend_);
  const auto t = preview.render(comp_->previewPlan(32, 18));
  const auto px = backend_->readback(t, 32, 18);
  const size_t i = (static_cast<size_t>(9) * 32 + 16) * 4;
  EXPECT_GT(px[i + 2], 250);
  EXPECT_LT(px[i], 5);
}

TEST_F(Compositor, DownscaleKeepsTheImage) {
  params_.set(L(1) + "/blend", std::string("Alpha"));
  clip(1, "vj://solid/00FF00");
  frame(0);
  const auto t = renderer_->downscale(last_, 16, 9);
  const auto px = backend_->readback(t, 16, 9);
  EXPECT_GT(px[(4 * 16 + 8) * 4 + 1], 250);
}
