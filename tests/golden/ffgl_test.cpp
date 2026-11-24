#include <unistd.h>

#include <filesystem>
#include <memory>

#include "compositor/CompositionModule.h"
#include "core/CommandBus.h"
#include "core/ModuleRegistry.h"
#include "core/generated/Parameters.gen.h"
#include "ffgl/FfglModule.h"
#include "golden/gl_fixture.h"

using namespace vj;
using namespace vj::compositor;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {
constexpr int kW = 64, kH = 36;
}

TEST(FfglProbe, ReadsRealPlugins) {
  const auto info = ffgl::probePlugin(std::string(VJ_FFGL_DIR) + "/Gradients.bundle");
  EXPECT_EQ(info["type"], ffgl::abi::kSource);
  EXPECT_GT(info["params"].size(), 4u);
  const auto effect = ffgl::probePlugin(std::string(VJ_FFGL_DIR) + "/AddSubtract.bundle");
  EXPECT_EQ(effect["type"], ffgl::abi::kEffect);
}

TEST(FfglProbe, ChildSurvivesCrashingPlugin) {
  EXPECT_THROW(ffgl::probeInChild(VJ_ENGINE_EXE, std::string(VJ_FFGL_DIR) + "/Crash.bundle", 5000), std::runtime_error);
  const auto ok = ffgl::probeInChild(VJ_ENGINE_EXE, std::string(VJ_FFGL_DIR) + "/Gradients.bundle", 5000);
  EXPECT_EQ(ok["type"], ffgl::abi::kSource);
}

class Ffgl : public GlContext {
 protected:
  void SetUp() override {
    GlContext::SetUp();
    if (IsSkipped()) return;
    dir_ = fs::temp_directory_path() / ("vj_ffgl_docs_" + std::to_string(getpid()));
    fs::create_directories(dir_ / "Extra Effects");
    for (const char* name : {"AddSubtract", "Gradients", "Particles", "Events", "Crash"})
      fs::copy(fs::path(VJ_FFGL_DIR) / (std::string(name) + ".bundle"), dir_ / "Extra Effects" / (std::string(name) + ".bundle"), fs::copy_options::recursive);
    params_.defineAll(schema::parameters());
    params_.set("/composition/width", int64_t{kW});
    params_.set("/composition/height", int64_t{kH});
    modules_.add(std::make_unique<CompositionModule>(CompositionShape{2, 2, 1}));
    modules_.add(std::make_unique<ffgl::FfglModule>(dir_.string(), VJ_ENGINE_EXE));
    ASSERT_EQ(modules_.startAll(ctx_), "");
    module_ = modules_.get<CompositionModule>("composition");
    comp_ = module_->composition();
    params_.set("/composition/layers/1/blend", std::string("Alpha"));
  }
  void TearDown() override {
    if (module_) module_->releaseGpu();
    modules_.stopAll();
    fs::remove_all(dir_);
    GlContext::TearDown();
  }
  std::string C() { return "/composition/layers/1/clips/1"; }
  void clip(const std::string& path) {
    params_.set(C() + "/path", path);
    params_.set(C() + "/resize", std::string("Stretch"));
    comp_->tick(0);
    comp_->waitForLoads();
    comp_->tick(0);
    params_.trigger(C() + "/connect");
    comp_->tick(0);
  }
  std::vector<uint8_t> frame(double dt = 1.0 / 60) {
    comp_->tick(dt);
    int w, h;
    const auto t = module_->renderFrame(*backend_, w, h);
    return backend_->readback(t, w, h);
  }
  std::string idOf(const std::string& name) {
    for (const auto& t : comp_->effects().catalog().list())
      if (t.category == "FFGL" && t.name.find(name) != std::string::npos) return t.id;
    return {};
  }
  fs::path dir_;
  ParameterRegistry params_;
  EventBus events_;
  ModuleRegistry modules_;
  CommandBus commands_;
  EngineContext ctx_{params_, events_, modules_, &commands_, nullptr};
  CompositionModule* module_ = nullptr;
  Composition* comp_ = nullptr;
};

TEST_F(Ffgl, ScanFindsPluginsAndIgnoresCrashingOne) {
  const json errors = commands_.run("ffgl.rescan");
  ASSERT_EQ(errors.size(), 1u) << errors;
  EXPECT_EQ(errors[0]["file"], "Crash.bundle");
  EXPECT_FALSE(idOf("Gradient").empty());
  EXPECT_FALSE(idOf("AddSub").empty());
}

TEST_F(Ffgl, SourcePluginDraws) {
  const std::string id = idOf("Gradient");
  ASSERT_FALSE(id.empty());
  clip("vj://source/x");
  comp_->effects().add(C(), id);
  const auto px = frame();
  int lit = 0;
  for (size_t i = 0; i < px.size(); i += 4) lit += px[i] + px[i + 1] + px[i + 2] > 30;
  EXPECT_GT(lit, kW * kH / 2);
}

TEST_F(Ffgl, ParametersReachThePlugin) {
  const std::string id = idOf("Gradient");
  clip("vj://source/x");
  const int e = comp_->effects().add(C(), id);
  const auto before = frame();
  const std::string prefix = comp_->effects().instancePath(C(), e);
  for (const char* p : {"p0", "p1", "p2"}) params_.set(prefix + "/" + p, 0.0);
  EXPECT_NE(frame(), before);
}

TEST_F(Ffgl, EffectPluginProcessesInput) {
  const std::string id = [&] {
    for (const auto& t : comp_->effects().catalog().list())
      if (t.category == "FFGL" && !t.source) return t.id;
    return std::string{};
  }();
  ASSERT_FALSE(id.empty());
  clip("vj://solid/404040");
  const auto plain = frame();
  const int e = comp_->effects().add("/composition/layers/1", id);
  const std::string prefix = comp_->effects().instancePath("/composition/layers/1", e);
  const auto type = comp_->effects().catalog().find(id);
  for (const auto& p : type->params) params_.set(prefix + "/" + p.name, 1.0);
  const auto px = frame();
  EXPECT_NE(px, plain);
  EXPECT_GT(px[0], plain[0]) << "AddSubtract al máximo aclara la imagen";
}

TEST_F(Ffgl, TransformFeedbackPluginRunsAndNextPassIsClean) {
  const std::string id = idOf("Particles");
  ASSERT_FALSE(id.empty());
  clip("vj://source/x");
  comp_->effects().add(C(), id);
  std::vector<uint8_t> first = frame(), last;
  for (int i = 0; i < 10; ++i) last = frame();
  EXPECT_NE(first, last) << "las partículas se mueven";

  // El plugin deja el contexto a su manera: una capa nuestra encima debe verse perfecta.
  const std::string top = "/composition/layers/2";
  params_.set(top + "/blend", std::string("Alpha"));
  params_.set(top + "/clips/1/path", std::string("vj://solid/FF0000"));
  params_.set(top + "/clips/1/resize", std::string("Stretch"));
  comp_->tick(0);
  comp_->waitForLoads();
  comp_->tick(0);
  params_.trigger(top + "/clips/1/connect");
  comp_->tick(0);
  const auto px = frame();
  for (size_t i = 0; i < px.size(); i += 4) {
    ASSERT_EQ(px[i], 255);
    ASSERT_EQ(px[i + 1], 0);
  }
}

TEST_F(Ffgl, RemovedEffectFreesInstance) {
  const std::string id = idOf("Gradient");
  clip("vj://source/x");
  const int e = comp_->effects().add(C(), id);
  frame();
  comp_->effects().remove(C(), e);
  const auto px = frame();
  for (size_t i = 0; i < px.size(); ++i) ASSERT_EQ(px[i], 0);
  comp_->effects().add(C(), id);
  EXPECT_NE(frame(), px);
}
