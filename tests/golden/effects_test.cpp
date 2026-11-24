#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "compositor/Composition.h"
#include "compositor/Renderer.h"
#include "core/generated/Effects.gen.h"
#include "core/generated/Parameters.gen.h"
#include "golden/gl_fixture.h"

using namespace vj;
using namespace vj::compositor;
namespace fs = std::filesystem;

namespace {
constexpr int kW = 64, kH = 36;
constexpr int kTolerance = 2;
struct Rgb { double r, g, b; };
}  // namespace

class Effects : public GlContext {
 protected:
  void SetUp() override {
    GlContext::SetUp();
    if (IsSkipped()) return;
    params_.defineAll(schema::parameters());
    params_.set("/composition/width", int64_t{kW});
    params_.set("/composition/height", int64_t{kH});
    comp_ = std::make_unique<Composition>(params_, events_, CompositionShape{4, 2, 2});
    renderer_ = std::make_unique<Renderer>(*backend_);
    for (int l = 1; l <= 4; ++l) params_.set(L(l) + "/blend", std::string("Alpha"));
  }
  void TearDown() override {
    renderer_.reset();
    comp_.reset();
    GlContext::TearDown();
  }
  std::string L(int l) { return "/composition/layers/" + std::to_string(l); }
  std::string C(int l, int c = 1) { return L(l) + "/clips/" + std::to_string(c); }
  EffectChains& fx() { return comp_->effects(); }
  std::string P(const std::string& entity, int id, const std::string& name) { return fx().instancePath(entity, id) + "/" + name; }
  void clip(int l, const std::string& path) {
    params_.set(C(l) + "/path", path);
    params_.set(C(l) + "/resize", std::string("Stretch"));
    comp_->tick(0);
    comp_->waitForLoads();
    comp_->tick(0);
    params_.trigger(C(l) + "/connect");
    comp_->tick(0);
  }
  void frame() {
    comp_->tick(0);
    pixels_ = backend_->readback(renderer_->render(comp_->plan()), kW, kH);
    EXPECT_TRUE(renderer_->effectErrors().empty()) << renderer_->effectErrors().begin()->second;
  }
  Rgb at(double u, double v) {
    const size_t i = (static_cast<size_t>(v * kH) * kW + static_cast<size_t>(u * kW)) * 4;
    return {pixels_[i] / 255.0, pixels_[i + 1] / 255.0, pixels_[i + 2] / 255.0};
  }
  void expectColor(Rgb got, Rgb want, double tol = 2.5 / 255) {
    EXPECT_NEAR(got.r, want.r, tol);
    EXPECT_NEAR(got.g, want.g, tol);
    EXPECT_NEAR(got.b, want.b, tol);
  }
  ParameterRegistry params_;
  EventBus events_;
  std::unique_ptr<Composition> comp_;
  std::unique_ptr<Renderer> renderer_;
  std::vector<uint8_t> pixels_;
};

// Cada tipo, con sus valores al 60 % del rango, contra su imagen de referencia.
// Rojo fijo y después verde: lo que quede de rojo solo puede venir de la memoria.
class EffectMemoryTest : public Effects {
 protected:
  int start(const std::string& type) {
    clip(1, "vj://solid/FF0000");
    const int id = fx().add(L(1), type);
    return id;
  }
  void toGreen() { clip(1, "vj://solid/00FF00"); }
  void frames(int n) {
    for (int i = 0; i < n; ++i) frame();
  }
};

TEST_F(EffectMemoryTest, TrailsLeaveTheOldImageAndFadeOut) {
  const int id = start("trails");
  params_.set(P(L(1), id, "amount"), 0.9);
  frames(4);
  toGreen();
  frame();
  EXPECT_GT(at(0.5, 0.5).r, 0.7);
  EXPECT_GT(at(0.5, 0.5).g, 0.9);
  frames(80);
  EXPECT_LT(at(0.5, 0.5).r, 0.1);
}

TEST_F(EffectMemoryTest, FrameDelayShowsTheNewImageExactlyLater) {
  const int id = start("frame-delay");
  params_.set(P(L(1), id, "frames"), 2.0);
  frames(5);
  toGreen();
  frames(2);
  EXPECT_GT(at(0.5, 0.5).r, 0.9);
  frame();
  EXPECT_GT(at(0.5, 0.5).g, 0.9);
  EXPECT_LT(at(0.5, 0.5).r, 0.1);
}

TEST_F(EffectMemoryTest, RgbDelayKeepsChannelsApart) {
  const int id = start("rgb-delay");
  params_.set(P(L(1), id, "green"), 3.0);
  frames(5);
  toGreen();
  frame();
  EXPECT_LT(at(0.5, 0.5).g, 0.1);
  frames(4);
  EXPECT_GT(at(0.5, 0.5).g, 0.9);
}

TEST_F(EffectMemoryTest, FreezeHoldsTheImageAndNeverStartsBlack) {
  const int id = start("beat-freeze");
  params_.set(P(L(1), id, "amount"), 1.0);
  frame();
  EXPECT_GT(at(0.5, 0.5).r, 0.9);
  toGreen();
  frames(3);
  EXPECT_GT(at(0.5, 0.5).r, 0.9);
}

TEST_F(EffectMemoryTest, BypassClearsTheMemory) {
  const int id = start("trails");
  params_.set(P(L(1), id, "amount"), 0.95);
  frames(4);
  params_.set(P(L(1), id, "bypass"), true);
  frame();
  toGreen();
  params_.set(P(L(1), id, "bypass"), false);
  frame();
  EXPECT_LT(at(0.5, 0.5).r, 0.1);
}

TEST_F(Effects, EveryCatalogEntryMatchesItsReference) {
  clip(1, "vj://pattern");
  const fs::path dir = fs::path(VJ_GOLDEN_DIR) / "effects";
  frame();
  const auto plain = pixels_;
  for (const auto& t : schema::effectTypes()) {
    if (t.glsl == "//AUDIO") continue;
    SCOPED_TRACE(t.id);
    const int id = fx().add(L(1), t.id);
    for (const auto& p : t.params) {
      if (p.type == "float") params_.set(P(L(1), id, p.name), p.min + 0.6 * (p.max - p.min));
      else if (p.type == "bool") params_.set(P(L(1), id, p.name), true);
    }
    frame();
    const auto path = dir / (t.id + ".rgba");
    if (std::getenv("VJ_UPDATE_GOLDEN")) {
      fs::create_directories(dir);
      std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(pixels_.data()), pixels_.size());
    } else {
      std::ifstream in(path, std::ios::binary);
      ASSERT_TRUE(in) << "falta la referencia";
      std::stringstream ss;
      ss << in.rdbuf();
      const std::string ref = ss.str();
      ASSERT_EQ(ref.size(), pixels_.size());
      size_t bad = 0;
      for (size_t i = 0; i < ref.size(); ++i)
        if (std::abs(int(pixels_[i]) - int(static_cast<uint8_t>(ref[i]))) > kTolerance) ++bad;
      EXPECT_EQ(bad, 0u);
    }
    // LUT sin archivo es la identidad; todo lo demás debe cambiar la imagen.
    // Con memoria, una imagen fija no cambia en el primer fotograma: tienen pruebas propias abajo.
    if (t.id == "lut") EXPECT_EQ(pixels_, plain);
    else if (t.memoryFrames == 0) EXPECT_NE(pixels_, plain);
    fx().remove(L(1), id);
  }
}

TEST_F(Effects, DefaultsOfNeutralEffectsAreIdentity) {
  clip(1, "vj://pattern");
  frame();
  const auto plain = pixels_;
  for (const char* id : {"transform", "brightness-contrast", "hue", "saturation", "gamma", "exposure", "wave-warp", "ripple", "twirl", "fisheye", "shift-rgb", "blur", "sharpen", "vignette", "scanlines", "noise", "zoom-pulse", "pixelate"}) {
    SCOPED_TRACE(id);
    const int e = fx().add(L(1), id);
    frame();
    size_t bad = 0;
    for (size_t i = 0; i < plain.size(); ++i)
      if (std::abs(int(plain[i]) - int(pixels_[i])) > 3) ++bad;
    EXPECT_EQ(bad, 0u);
    fx().remove(L(1), e);
  }
}

TEST_F(Effects, OrderChangesTheImage) {
  clip(1, "vj://solid/804020");
  const int a = fx().add(L(1), "invert");
  const int b = fx().add(L(1), "exposure");
  params_.set(P(L(1), b, "stops"), 1.0);
  frame();  // invertir y después doblar
  expectColor(at(0.5, 0.5), {(1 - 128 / 255.0) * 2, 1, 1});
  fx().move(L(1), a, 1);
  frame();  // doblar y después invertir
  expectColor(at(0.5, 0.5), {0, 1 - 128 / 255.0, 1 - 64 / 255.0});
}

TEST_F(Effects, BypassOpacityAndBlend) {
  clip(1, "vj://solid/FF0000");
  const int e = fx().add(L(1), "invert");
  frame();
  expectColor(at(0.5, 0.5), {0, 1, 1});
  params_.set(P(L(1), e, "bypass"), true);
  frame();
  expectColor(at(0.5, 0.5), {1, 0, 0});
  params_.set(P(L(1), e, "bypass"), false);
  params_.set(P(L(1), e, "opacity"), 0.5);
  frame();
  expectColor(at(0.5, 0.5), {0.5, 0.5, 0.5}, 3.0 / 255);
  params_.set(P(L(1), e, "opacity"), 1.0);
  params_.set(P(L(1), e, "blend"), std::string("Add"));
  frame();
  expectColor(at(0.5, 0.5), {1, 1, 1});
}

TEST_F(Effects, GeometryAndDimmingReplaceTheOriginal) {
  clip(1, "vj://solid/FFFFFF");
  const int t = fx().add(L(1), "transform");
  params_.set(P(L(1), t, "scale"), 0.25);
  frame();
  expectColor(at(0.5, 0.5), {1, 1, 1});
  expectColor(at(0.05, 0.05), {0, 0, 0});  // el original no asoma por detrás
  fx().clear(L(1));
  const int s = fx().add(L(1), "strobe");
  params_.set(P(L(1), s, "amount"), 1.0);
  frame();
  expectColor(at(0.5, 0.5), {0, 0, 0});
}

TEST_F(Effects, ClipThenLayerThenComposition) {
  clip(1, "vj://solid/000000");
  const int c = fx().add(C(1), "brightness-contrast");
  params_.set(P(C(1), c, "brightness"), 0.5);
  const int l = fx().add(L(1), "exposure");
  params_.set(P(L(1), l, "stops"), 1.0);
  fx().add("/composition", "invert");
  frame();  // 0 → 0,5 → 1 → 0
  expectColor(at(0.5, 0.5), {0, 0, 0}, 3.0 / 255);
}

TEST_F(Effects, CompositionEffectActsAfterTheMix) {
  clip(1, "vj://solid/FF0000");
  clip(2, "vj://solid/00FF00");
  params_.set(L(2) + "/blend", std::string("Add"));
  fx().add("/composition", "invert");
  frame();
  expectColor(at(0.5, 0.5), {0, 0, 1});
}

TEST_F(Effects, GroupEffectTouchesOnlyItsLayers) {
  clip(1, "vj://solid/FF0000");
  // Las dos capas del grupo van a la mitad derecha, para ver la de fuera a la izquierda.
  for (int l : {2, 3}) {
    clip(l, "vj://solid/00FF00");
    params_.set(L(l) + "/group", int64_t{1});
  }
  params_.set(C(3) + "/video/opacity", 0.0);
  params_.set("/composition/groups/1/transform/scale", 0.5);
  params_.set("/composition/groups/1/transform/x", 16.0);
  fx().add("/composition/groups/1", "invert");
  frame();
  expectColor(at(0.75, 0.5), {1, 0, 1});
  expectColor(at(0.1, 0.5), {1, 0, 0});
}

TEST_F(Effects, LutAppliesAndInterpolates) {
  const auto cube = fs::temp_directory_path() / ("vj_invert_" + std::to_string(getpid()) + ".cube");
  {
    std::ofstream out(cube);
    out << "TITLE \"invertir\"\nLUT_3D_SIZE 2\n";
    for (int b = 0; b < 2; ++b)
      for (int g = 0; g < 2; ++g)
        for (int r = 0; r < 2; ++r) out << 1 - r << " " << 1 - g << " " << 1 - b << "\n";
  }
  clip(1, "vj://solid/FF4000");
  const int e = fx().add(L(1), "lut");
  params_.set(P(L(1), e, "file"), cube.string());
  frame();
  expectColor(at(0.5, 0.5), {0, 1 - 64 / 255.0, 1}, 5.0 / 255);
  params_.set(P(L(1), e, "opacity"), 0.0);
  frame();
  expectColor(at(0.5, 0.5), {1, 64 / 255.0, 0});
}

TEST(CubeLut, RejectsBadFiles) {
  const auto bad = fs::temp_directory_path() / ("vj_bad_" + std::to_string(getpid()) + ".cube");
  std::ofstream(bad) << "LUT_3D_SIZE 2\n0 0 0\n";
  EXPECT_THROW(media::loadCubeLut(bad.string()), std::runtime_error);
  EXPECT_THROW(media::loadCubeLut("/no/existe.cube"), std::runtime_error);
  std::ofstream(bad) << "LUT_1D_SIZE 2\n0 0 0\n1 1 1\n";
  EXPECT_THROW(media::loadCubeLut(bad.string()), std::runtime_error);
}

TEST_F(Effects, MissingLutReportsAndKeepsRendering) {
  std::string message;
  events_.subscribe("effects.error", [&](const std::string&, const nlohmann::json& d) { message = d["message"]; });
  clip(1, "vj://solid/FF0000");
  const int e = fx().add(L(1), "lut");
  params_.set(P(L(1), e, "file"), std::string("/no/existe.cube"));
  frame();
  EXPECT_FALSE(message.empty());
  expectColor(at(0.5, 0.5), {1, 0, 0});
}

// Ejemplo al estilo de la especificación ISF: entrada de imagen, float y color.
TEST_F(Effects, IsfEffectFromSpec) {
  auto type = parseIsf("isf:prueba", R"(/*{
    "DESCRIPTION": "demo", "CREDIT": "x", "ISFVSN": "2",
    "INPUTS": [
      {"NAME": "inputImage", "TYPE": "image"},
      {"NAME": "level", "TYPE": "float", "DEFAULT": 0.5, "MIN": 0.0, "MAX": 1.0},
      {"NAME": "flip", "TYPE": "bool", "DEFAULT": false},
      {"NAME": "tint", "TYPE": "color", "DEFAULT": [1.0, 0.0, 0.0, 1.0]}
    ]
  }*/
  void main() {
    vec4 c = IMG_NORM_PIXEL(inputImage, isf_FragNormCoord);
    if (flip) c.rgb = 1.0 - c.rgb;
    gl_FragColor = vec4(c.rgb * tint.rgb * level, c.a);
  })");
  EXPECT_FALSE(type.source);
  EXPECT_EQ(type.params.size(), 6u);
  ASSERT_EQ(backend_->createProgram(Renderer::effectShader(type)) != 0, true);
}

TEST_F(Effects, IsfRunsThroughTheChain) {
  const auto dir = fs::temp_directory_path() / ("vj_isf_" + std::to_string(getpid()));
  fs::create_directories(dir);
  std::ofstream(dir / "tinte.fs") << R"(/*{"INPUTS":[{"NAME":"inputImage","TYPE":"image"},{"NAME":"level","TYPE":"float","DEFAULT":0.5,"MIN":0,"MAX":1}]}*/
void main() { gl_FragColor = IMG_THIS_PIXEL(inputImage) * vec4(vec3(level), 1.0); })";
  std::ofstream(dir / "fuente.fs") << R"(/*{"INPUTS":[{"NAME":"a","TYPE":"color","DEFAULT":[0,1,0,1]}]}*/
void main() { gl_FragColor = a * step(0.0, TIME) * step(1.0, RENDERSIZE.x); })";
  EXPECT_TRUE(fx().catalog().scanIsf(dir.string()).empty());
  clip(1, "vj://solid/FFFFFF");
  fx().add(L(1), "isf:tinte");
  frame();
  expectColor(at(0.5, 0.5), {0.5, 0.5, 0.5}, 3.0 / 255);
  fx().clear(L(1));
  fx().add(L(1), "isf:fuente");
  frame();
  expectColor(at(0.5, 0.5), {0, 1, 0});
  fs::remove_all(dir);
}

TEST(Isf, RejectsWhatItCannotRun) {
  EXPECT_THROW(parseIsf("isf:a", "void main() {}"), std::runtime_error);
  EXPECT_THROW(parseIsf("isf:a", R"(/*{"PASSES":[{},{}]}*/ void main() {})"), std::runtime_error);
  EXPECT_THROW(parseIsf("isf:a", R"(/*{"INPUTS":[{"NAME":"o","TYPE":"image"}]}*/ void main() {})"), std::runtime_error);
  EXPECT_THROW(parseIsf("isf:a", R"(/*{"INPUTS":[{"NAME":"o","TYPE":"audio"}]}*/ void main() {})"), std::runtime_error);
}

TEST_F(Effects, BrokenIsfDoesNotStopTheOutput) {
  schema::EffectType t = parseIsf("isf:roto", R"(/*{"INPUTS":[]}*/ void main() { esto no compila })");
  PlanEffect e;
  e.type = &t;
  clip(1, "vj://solid/FF0000");
  comp_->tick(0);
  auto plan = comp_->plan();
  plan.effects.push_back(e);
  pixels_ = backend_->readback(renderer_->render(plan), kW, kH);
  EXPECT_FALSE(renderer_->effectErrors().empty());
  expectColor(at(0.5, 0.5), {1, 0, 0});
}

TEST_F(Effects, TextSourceDrawsAndUpdates) {
  clip(1, "vj://source/text");
  const int e = fx().add(C(1), "text");
  params_.set(P(C(1), e, "size"), 400.0);
  params_.set(P(C(1), e, "text"), std::string("■■■■"));
  frame();
  double lit = 0;
  for (size_t i = 0; i < pixels_.size(); i += 4) lit += pixels_[i];
  EXPECT_GT(lit, 0);
  const auto first = pixels_;
  params_.set(P(C(1), e, "text"), std::string("."));
  frame();
  EXPECT_NE(pixels_, first);
  params_.set(P(C(1), e, "r"), 0.0);
  params_.set(P(C(1), e, "text"), std::string("■■■■"));
  frame();
  for (size_t i = 0; i < pixels_.size(); i += 4) ASSERT_EQ(pixels_[i], 0);
}

TEST_F(Effects, TextIsUpright) {
  clip(1, "vj://source/text");
  const int e = fx().add(C(1), "text");
  params_.set(P(C(1), e, "size"), 300.0);
  params_.set(P(C(1), e, "text"), std::string("■\n "));  // solo la línea superior
  frame();
  double top = 0, bottom = 0;
  for (int y = 0; y < kH; ++y)
    for (int x = 0; x < kW; ++x) (y < kH / 2 ? top : bottom) += pixels_[(static_cast<size_t>(y) * kW + x) * 4];
  EXPECT_GT(top, bottom * 2);
}

TEST_F(Effects, TransformYGoesDownLikeTheRest) {
  clip(1, "vj://solid/FFFFFF");
  const int t = fx().add(L(1), "transform");
  params_.set(P(L(1), t, "scale"), 0.25);
  params_.set(P(L(1), t, "y"), 0.3);
  frame();
  expectColor(at(0.5, 0.8), {1, 1, 1});
  expectColor(at(0.5, 0.2), {0, 0, 0});
}

TEST_F(Effects, SourcesAreGenerative) {
  clip(1, "vj://source/solid");
  const int e = fx().add(C(1), "solid");
  params_.set(P(C(1), e, "g"), 0.0);
  frame();
  expectColor(at(0.5, 0.5), {1, 0, 1});
}

TEST_F(Effects, FrozenClipKeepsItsEffects) {
  clip(1, "vj://solid/FF0000");
  fx().add(C(1), "invert");
  comp_->freezeSlots([](int, int) { return false; });
  fx().clear(C(1));
  frame();
  expectColor(at(0.5, 0.5), {0, 1, 1});
}
