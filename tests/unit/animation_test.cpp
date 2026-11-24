#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>

#include "animation/Animator.h"
#include "compositor/AudioModule.h"
#include "compositor/CompositionModule.h"
#include "core/CommandBus.h"
#include "core/ModuleRegistry.h"
#include "core/generated/Parameters.gen.h"
#include "project/ProjectModule.h"

using namespace vj;
using namespace vj::animation;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {
double ev(const std::string& s, std::map<std::string, double> v = {}) { return Expression(s).eval(v); }
constexpr double kDt = 1.0 / 60;
}  // namespace

TEST(Expressions, ReferenceSet) {
  EXPECT_DOUBLE_EQ(ev("1 + 2 * 3"), 7);
  EXPECT_DOUBLE_EQ(ev("(1 + 2) * 3"), 9);
  EXPECT_DOUBLE_EQ(ev("2 ^ 3 ^ 2"), 512);
  EXPECT_DOUBLE_EQ(ev("-2 ^ 2"), -4);
  EXPECT_DOUBLE_EQ(ev("1920 / 3"), 640);
  EXPECT_DOUBLE_EQ(ev("7 % 4"), 3);
  EXPECT_NEAR(ev("sin(pi / 2)"), 1, 1e-12);
  EXPECT_NEAR(ev("cos(0) + e"), 1 + 2.71828182845904523536, 1e-12);
  EXPECT_DOUBLE_EQ(ev("min(3, 2) + max(3, 2)"), 5);
  EXPECT_DOUBLE_EQ(ev("clamp(5, 0, 1)"), 1);
  EXPECT_DOUBLE_EQ(ev("mix(2, 4, 0.5)"), 3);
  EXPECT_DOUBLE_EQ(ev("step(0.5, 0.6)"), 1);
  EXPECT_DOUBLE_EQ(ev("smoothstep(0, 1, 0.5)"), 0.5);
  EXPECT_DOUBLE_EQ(ev("fract(1.25)"), 0.25);
  EXPECT_DOUBLE_EQ(ev("x * 2 + y", {{"x", 3}, {"y", 1}}), 7);
  EXPECT_DOUBLE_EQ(ev("noexiste"), 0);
}

TEST(Expressions, SafeOnBadMath) {
  EXPECT_DOUBLE_EQ(ev("1 / 0"), 0);
  EXPECT_DOUBLE_EQ(ev("sqrt(-1)"), 0);
  EXPECT_DOUBLE_EQ(ev("log(0)"), 0);
}

TEST(Expressions, ReportsSyntaxErrors) {
  for (const char* bad : {"1 +", "(1", "foo(1)", "sin(1, 2)", "1 2", "", "$"}) EXPECT_THROW(Expression e(bad), std::runtime_error) << bad;
  EXPECT_THROW(Expression e(std::string(200, '(') + "1" + std::string(200, ')')), std::runtime_error);
}

TEST(Envelope, InterpolatesPoints) {
  const json p = {{0, 0}, {0.5, 1}, {1, 0}};
  EXPECT_DOUBLE_EQ(evalEnvelope(p, 0.25), 0.5);
  EXPECT_DOUBLE_EQ(evalEnvelope(p, 0.5), 1);
  EXPECT_DOUBLE_EQ(evalEnvelope(p, 0.75), 0.5);
  EXPECT_DOUBLE_EQ(evalEnvelope(json(), 0.3), 0.3);
}

class AnimationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    dir = fs::temp_directory_path() / ("vj_anim_" + std::to_string(getpid()));
    fs::create_directories(dir);
    params.defineAll(schema::parameters());
    modules.add(std::make_unique<compositor::CompositionModule>(compositor::CompositionShape{2, 2, 1}));
    modules.add(std::make_unique<Animator>());
    modules.add(std::make_unique<project::ProjectModule>(dir.string()));
    ASSERT_EQ(modules.startAll(ctx), "");
    anim = modules.get<Animator>("animator");
    comp = modules.get<compositor::CompositionModule>("composition")->composition();
    anim->setLevels(&fake);
  }
  void TearDown() override {
    modules.stopAll();
    fs::remove_all(dir);
  }
  void run(double seconds) {
    for (int n = static_cast<int>(std::lround(seconds / kDt)); n > 0; --n) tickOnce(kDt);
  }
  void tickOnce(double dt) {
    commands.pump();
    modules.tickAll(dt);
  }
  double master() { return params.getFloat("/composition/master"); }
  const std::string C = "/composition/layers/1/clips/1", L = "/composition/layers/1";
  struct Fake : ILevels {
    double value = 0;
    std::string source;
    double level(const std::string& s, const std::string&, double, double) override {
      source = s;
      return value;
    }
  } fake;
  fs::path dir;
  ParameterRegistry params;
  EventBus events;
  ModuleRegistry modules;
  CommandBus commands;
  EngineContext ctx{params, events, modules, &commands, nullptr};
  Animator* anim = nullptr;
  compositor::Composition* comp = nullptr;
};

TEST_F(AnimationTest, TimelineRampsAndLoops) {
  anim->set("/composition/master", {{"mode", "timeline"}, {"duration", 2.0}});
  run(1);
  EXPECT_NEAR(master(), 0.5, 0.03);
  run(0.5);
  EXPECT_NEAR(master(), 0.75, 0.03);
  run(0.75);
  EXPECT_NEAR(master(), 0.125, 0.03);
}

TEST_F(AnimationTest, InOutLimitTheRange) {
  anim->set("/composition/master", {{"mode", "timeline"}, {"duration", 1.0}, {"in", 0.2}, {"out", 0.4}, {"playmode", "Play Once and Hold"}});
  run(0.5);
  EXPECT_NEAR(master(), 0.3, 0.02);
  run(1);
  EXPECT_NEAR(master(), 0.4, 1e-6);
}

TEST_F(AnimationTest, PingPongAndBackward) {
  anim->set("/composition/master", {{"mode", "timeline"}, {"duration", 1.0}, {"playmode", "Ping Pong"}});
  run(1.5);
  EXPECT_NEAR(master(), 0.5, 0.03);
  anim->set("/composition/speed", {{"mode", "timeline"}, {"duration", 1.0}, {"direction", "Backward"}, {"out", 0.1}});
  run(0.25);
  EXPECT_NEAR(params.getFloat("/composition/speed"), 0.75, 0.05);
}

TEST_F(AnimationTest, EnvelopeShapesTheCurve) {
  anim->set("/composition/master", {{"mode", "timeline"}, {"duration", 1.0}, {"envelope", json{{0, 0}, {0.5, 1}, {1, 0}}}});
  run(0.25);
  EXPECT_NEAR(master(), 0.5, 0.03);
  run(0.25);
  EXPECT_NEAR(master(), 1, 0.03);
  run(0.25);
  EXPECT_NEAR(master(), 0.5, 0.03);
}

TEST_F(AnimationTest, BpmSyncFollowsBeatWithPhaseLock) {
  anim->set("/composition/master", {{"mode", "bpm"}, {"beats", 4.0}});
  run(1);  // 2 beats de 4 a 120 BPM
  tickOnce(0.2);
  EXPECT_NEAR(master(), 0.5 + 0.1, 0.08);
  params.trigger("/composition/tempo/resync");
  run(0.25);
  EXPECT_LT(master(), 0.2);
}

TEST_F(AnimationTest, BpmWithoutPhaseLockFollowsTempoOnly) {
  run(0.5);
  anim->set("/composition/master", {{"mode", "bpm"}, {"beats", 4.0}, {"phaseLock", false}});
  run(1);
  EXPECT_NEAR(master(), 0.5, 0.03);
}

TEST_F(AnimationTest, ClipPositionDrivesParameter) {
  params.set(C + "/path", std::string("vj://solid/FF0000"));
  tickOnce(0);
  comp->waitForLoads();
  tickOnce(0);
  params.trigger(C + "/connect");
  tickOnce(0);
  anim->set("/composition/master", {{"mode", "clip"}, {"clip", C}});
  run(5);
  EXPECT_NEAR(master(), 0.5, 0.05);
}

TEST_F(AnimationTest, CrossfaderDrivesThroughEnvelope) {
  anim->set("/composition/master", {{"mode", "crossfader"}, {"envelope", json{{0, 1}, {1, 0}}}});
  params.set("/composition/crossfader/phase", 0.25);
  tickOnce(kDt);
  EXPECT_NEAR(master(), 0.75, 1e-6);
}

TEST_F(AnimationTest, FftGainFallAndDrive) {
  anim->set("/composition/master", {{"mode", "fft"}, {"fft", {{"source", "composition"}, {"band", "L"}, {"gain", 2.0}, {"fall", 1.0}}}});
  fake.value = 0.4;
  tickOnce(kDt);
  EXPECT_NEAR(master(), 0.8, 1e-6);
  EXPECT_EQ(fake.source, "composition");
  fake.value = 0;
  run(0.5);
  EXPECT_NEAR(master(), 0.3, 0.03) << "cae a la velocidad de fall";
  anim->set("/composition/speed", {{"mode", "fft"}, {"fft", {{"drive", "<"}, {"gain", 1.0}, {"fall", 1000.0}}}});
  fake.value = 0.25;
  tickOnce(kDt);
  EXPECT_NEAR(params.getFloat("/composition/speed"), 7.5, 1e-3);
}

TEST_F(AnimationTest, ExpressionWithVariables) {
  anim->set("/composition/master", {{"mode", "expression"}, {"expr", "fract(beat / 4)"}});
  run(1);
  EXPECT_NEAR(master(), 0.5, 0.05);
  EXPECT_THROW(anim->set("/composition/master", {{"mode", "expression"}, {"expr", "1 +"}}), std::runtime_error);
}

TEST_F(AnimationTest, BoolAndEnumAndEvent) {
  anim->set(L + "/bypass", {{"mode", "timeline"}, {"duration", 1.0}});
  anim->set(L + "/blend", {{"mode", "timeline"}, {"duration", 2.2}, {"envelope", json{{0, 0}, {1, 1}}}});
  run(0.25);
  EXPECT_FALSE(params.getBool(L + "/bypass"));
  EXPECT_EQ(params.getString(L + "/blend"), "Cut");
  run(0.5);
  EXPECT_TRUE(params.getBool(L + "/bypass"));
  EXPECT_EQ(params.getString(L + "/blend"), "Screen") << "cambia de opción en los instantes de la curva";

  int fired = 0;
  params.addListener("/composition/columns/next", [&](const ParamDesc&, const Value&) { ++fired; });
  anim->set("/composition/columns/next", {{"mode", "timeline"}, {"duration", 1.0}});
  run(2.2);
  EXPECT_EQ(fired, 2);
}

TEST_F(AnimationTest, StartOnClipTriggerOnly) {
  params.set(C + "/path", std::string("vj://solid/FF0000"));
  tickOnce(0);
  comp->waitForLoads();
  tickOnce(0);
  params.set("/composition/master", 0.9);
  anim->set("/composition/master", {{"mode", "timeline"}, {"duration", 1.0}, {"start", {{"clip", true}}}});
  run(0.5);
  EXPECT_DOUBLE_EQ(master(), 0.9) << "no arranca sola";
  params.trigger(C + "/connect");
  run(0.25);
  EXPECT_NEAR(master(), 0.25, 0.05);
}

TEST_F(AnimationTest, StartOnColumnAndManual) {
  anim->set("/composition/master", {{"mode", "timeline"}, {"duration", 1.0}, {"start", {{"column", true}}}});
  anim->set("/composition/speed", {{"mode", "timeline"}, {"duration", 1.0}, {"start", {{"manual", true}}}});
  params.set("/composition/speed", 3.0);
  run(0.1);
  EXPECT_DOUBLE_EQ(params.getFloat("/composition/speed"), 3);
  params.trigger("/composition/columns/2/connect");
  run(0.5);
  EXPECT_NEAR(master(), 0.5, 0.05);
  anim->startNow("/composition/speed");
  run(0.5);
  EXPECT_NEAR(params.getFloat("/composition/speed"), 5, 0.3);
}

TEST_F(AnimationTest, DoesNotDirtyProjectNorHistory) {
  anim->set("/composition/master", {{"mode", "timeline"}, {"duration", 1.0}});
  run(0.5);
  EXPECT_FALSE(params.getBool("/project/dirty"));
  EXPECT_FALSE(params.getBool("/project/canundo"));
}

TEST_F(AnimationTest, SavedAndUndoable) {
  params.set(C + "/path", std::string("vj://solid/FF0000"));
  tickOnce(0);
  comp->waitForLoads();
  tickOnce(0);
  commands.run("animation.set", {{"path", "/composition/master"}, {"config", {{"mode", "timeline"}, {"duration", 3.0}}}});
  commands.run("animation.set", {{"path", C + "/video/opacity"}, {"config", {{"mode", "bpm"}, {"beats", 8.0}}}});
  EXPECT_TRUE(params.getBool("/project/canundo"));
  const auto path = commands.run("project.save", {{"path", (dir / "a").string()}})["path"].get<std::string>();
  commands.run("project.new");
  EXPECT_FALSE(anim->get("/composition/master"));
  commands.run("project.open", {{"path", path}});
  EXPECT_EQ((*anim->get("/composition/master"))["duration"], 3.0);
  EXPECT_EQ((*anim->get(C + "/video/opacity"))["beats"], 8.0);
  commands.run("animation.remove", {{"path", "/composition/master"}});
  EXPECT_FALSE(anim->get("/composition/master"));
  commands.run("history.undo");
  EXPECT_TRUE(anim->get("/composition/master"));
}

TEST_F(AnimationTest, PresetKeepsEverythingButInAndOut) {
  commands.run("animation.set", {{"path", "/composition/master"}, {"config", {{"mode", "timeline"}, {"duration", 3.0}, {"in", 0.1}, {"out", 0.2}, {"playmode", "Ping Pong"}}}});
  commands.run("animation.preset.save", {{"path", "/composition/master"}, {"name", "Vaivén"}});
  commands.run("animation.set", {{"path", "/composition/speed"}, {"config", {{"mode", "bpm"}, {"in", 0.5}, {"out", 0.6}}}});
  commands.run("animation.preset.apply", {{"path", "/composition/speed"}, {"name", "Vaivén"}});
  const auto c = *anim->get("/composition/speed");
  EXPECT_EQ(c["mode"], "timeline");
  EXPECT_EQ(c["duration"], 3.0);
  EXPECT_EQ(c["playmode"], "Ping Pong");
  EXPECT_EQ(c["in"], 0.5);
  EXPECT_EQ(c["out"], 0.6);
}

TEST_F(AnimationTest, RejectsUnknownParameter) {
  EXPECT_THROW(anim->set("/no/existe", {{"mode", "timeline"}}), std::runtime_error);
}
