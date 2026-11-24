#include <gtest/gtest.h>

#include "compositor/Composition.h"
#include "core/generated/Parameters.gen.h"

using namespace vj;
using namespace vj::compositor;

namespace {
constexpr double kDt = 1.0 / 60;
const std::string kClipA = "vj://solid/FF0000", kClipB = "vj://solid/00FF00";
}  // namespace

class CompositionTest : public ::testing::Test {
 protected:
  CompositionTest() {
    params.defineAll(schema::parameters());
    comp = std::make_unique<Composition>(params, events, CompositionShape{3, 4, 2});
  }
  std::string L(int l) { return "/composition/layers/" + std::to_string(l); }
  std::string C(int l, int c) { return L(l) + "/clips/" + std::to_string(c); }
  void load(int l, int c, const std::string& path = kClipA) {
    params.set(C(l, c) + "/path", path);
    comp->tick(0);
    comp->waitForLoads();
    comp->tick(0);
  }
  void run(double seconds) {
    for (int n = static_cast<int>(std::lround(seconds / kDt)); n > 0; --n) comp->tick(kDt);
  }
  void fire(int l, int c) {
    params.trigger(C(l, c) + "/connect");
    comp->tick(0);
  }
  bool on(int l, int c) { return params.getBool(C(l, c) + "/connected"); }
  int active(int l) { return static_cast<int>(params.getInt(L(l) + "/activeclip")); }

  ParameterRegistry params;
  EventBus events;
  std::unique_ptr<Composition> comp;
};

TEST_F(CompositionTest, CreatesParametersForEveryCell) {
  EXPECT_TRUE(params.describe(C(3, 4) + "/transport/playmode"));
  EXPECT_TRUE(params.describe("/composition/columns/4/connect"));
  EXPECT_TRUE(params.describe("/composition/groups/2/columns/4/connect"));
  EXPECT_FALSE(params.describe(C(4, 1) + "/connect"));
  EXPECT_EQ(params.getInt("/composition/structure/layers"), 3);
}

TEST_F(CompositionTest, StructureGrowsAndShrinksLive) {
  comp->addLayer();
  comp->addColumn();
  EXPECT_EQ(params.getInt("/composition/structure/layers"), 4);
  EXPECT_EQ(params.getInt("/composition/structure/columns"), 5);
  // Las celdas nuevas funcionan igual que las de arranque.
  load(4, 5);
  params.trigger(C(4, 5) + "/connect");
  comp->tick(kDt);
  EXPECT_TRUE(params.getBool(C(4, 5) + "/connected"));
  params.trigger(L(4) + "/eject");
  comp->tick(kDt);
  params.set(C(4, 5) + "/path", std::string{});
  comp->tick(0);
  comp->removeLastColumn();
  comp->removeLastLayer();
  EXPECT_EQ(comp->shape().layers, 3);
  EXPECT_EQ(comp->shape().columns, 4);
  EXPECT_FALSE(params.get(L(4) + "/master").has_value());
  comp->tick(kDt);
}

TEST_F(CompositionTest, StructureKeepsItsLimits) {
  for (int l = comp->shape().layers; l > 1; --l) comp->removeLastLayer();
  EXPECT_THROW(comp->removeLastLayer(), std::runtime_error);
  while (comp->shape().layers < kMaxLayers) comp->addLayer();
  EXPECT_THROW(comp->addLayer(), std::runtime_error);
}

TEST_F(CompositionTest, CrossfaderCurvesChangeBusOpacity) {
  params.set(L(1) + "/crossfaderbus", std::string("B"));
  params.set("/composition/crossfader/phase", 0.25);
  EXPECT_NEAR(comp->plan().layers[0].opacity, 0.5, 1e-5);
  params.set("/composition/crossfader/curve", std::string("Smooth"));
  EXPECT_NEAR(comp->plan().layers[0].opacity, 0.38268, 1e-4);
  params.set("/composition/crossfader/curve", std::string("Cut"));
  EXPECT_NEAR(comp->plan().layers[0].opacity, 1, 1e-5);
  params.set("/composition/crossfader/phase", 0.0);
  EXPECT_NEAR(comp->plan().layers[0].opacity, 0, 1e-5);
}

TEST_F(CompositionTest, TriggerAndEject) {
  load(1, 1);
  EXPECT_EQ(params.getString(C(1, 1) + "/status"), "ready");
  fire(1, 1);
  EXPECT_TRUE(on(1, 1));
  EXPECT_EQ(active(1), 1);
  params.trigger(L(1) + "/eject");
  comp->tick(0);
  EXPECT_FALSE(on(1, 1));
  EXPECT_EQ(active(1), 0);
}

TEST_F(CompositionTest, ConnectOnEmptyCellDoesNothing) {
  fire(1, 2);
  EXPECT_EQ(active(1), 0);
}

TEST_F(CompositionTest, BadPathReportsErrorInCell) {
  params.set(C(1, 1) + "/path", std::string("/no/existe.mp4"));
  comp->tick(0);
  comp->waitForLoads();
  comp->tick(0);
  EXPECT_EQ(params.getString(C(1, 1) + "/status").rfind("error", 0), 0u);
  fire(1, 1);
  EXPECT_EQ(active(1), 0);
}

TEST_F(CompositionTest, ChangingPathWhileLoadingEndsOnLast) {
  params.set(C(1, 1) + "/path", std::string("/no/existe.mp4"));
  comp->tick(0);
  params.set(C(1, 1) + "/path", kClipA);
  for (int i = 0; i < 5; ++i) {
    comp->tick(0);
    comp->waitForLoads();
  }
  comp->tick(0);
  EXPECT_EQ(params.getString(C(1, 1) + "/status"), "ready");
}

TEST_F(CompositionTest, ClearEmptiesCellAndStopsClip) {
  load(1, 1);
  fire(1, 1);
  params.trigger(C(1, 1) + "/clear");
  comp->tick(0);
  EXPECT_EQ(active(1), 0);
  EXPECT_EQ(params.getString(C(1, 1) + "/status"), "empty");
  EXPECT_EQ(params.getString(C(1, 1) + "/path"), "");
}

TEST_F(CompositionTest, ColumnFiresAllInSameTick) {
  load(1, 2);
  load(2, 2);
  load(3, 2);
  params.trigger("/composition/columns/2/connect");
  comp->tick(0);
  EXPECT_TRUE(on(1, 2) && on(2, 2) && on(3, 2));
  EXPECT_EQ(params.getInt("/composition/columns/current"), 2);
}

TEST_F(CompositionTest, IgnoreColumnOnClipAndLayerAndGroup) {
  load(1, 1);
  load(2, 1);
  load(3, 1);
  params.set(C(1, 1) + "/ignorecolumn", true);
  params.set(L(2) + "/ignorecolumn", true);
  params.set(L(3) + "/group", int64_t{1});
  params.set("/composition/groups/1/ignorecolumn", true);
  params.trigger("/composition/columns/1/connect");
  comp->tick(0);
  EXPECT_FALSE(on(1, 1) || on(2, 1) || on(3, 1));
}

TEST_F(CompositionTest, GroupColumnOnlyTouchesItsLayers) {
  load(1, 1);
  load(2, 1);
  params.set(L(2) + "/group", int64_t{1});
  params.trigger("/composition/groups/1/columns/1/connect");
  comp->tick(0);
  EXPECT_FALSE(on(1, 1));
  EXPECT_TRUE(on(2, 1));
  fire(1, 1);
  params.trigger("/composition/groups/1/eject");
  comp->tick(0);
  EXPECT_TRUE(on(1, 1));
  EXPECT_FALSE(on(2, 1));
}

TEST_F(CompositionTest, BeatSnapToBarWithinOneFrame) {
  load(1, 1);
  params.set("/composition/beatsnap", std::string("Bar"));
  run(0.65);  // beat 1.3 a 120 BPM; el siguiente compás es el beat 4, a los 2 s
  fire(1, 1);
  EXPECT_FALSE(on(1, 1));
  double t = 0.65;
  while (!on(1, 1) && t < 3) {
    comp->tick(kDt);
    t += kDt;
  }
  EXPECT_NEAR(t, 2.0, kDt + 1e-9);
}

TEST_F(CompositionTest, ClipSnapOverridesComposition) {
  load(1, 1);
  params.set("/composition/beatsnap", std::string("4 Bars"));
  params.set(C(1, 1) + "/beatsnap", std::string("None"));
  run(0.1);
  fire(1, 1);
  EXPECT_TRUE(on(1, 1));
}

TEST_F(CompositionTest, ClipTargetOwnActiveFree) {
  load(1, 1);
  load(2, 1);
  fire(2, 1);  // capa 2 ocupada
  params.set(C(1, 1) + "/target", std::string("Active Layer"));
  params.set("/composition/activelayer", int64_t{3});
  fire(1, 1);
  EXPECT_EQ(active(3), 1);
  EXPECT_EQ(active(1), 0);
  params.trigger(L(3) + "/eject");
  comp->tick(0);
  params.set(C(1, 1) + "/target", std::string("Free Layer"));
  fire(1, 1);
  EXPECT_EQ(active(1), 1);
  params.trigger(L(1) + "/eject");
  comp->tick(0);
  params.set(C(1, 1) + "/target", std::string("Own Layer"));
  fire(1, 1);
  EXPECT_EQ(active(1), 1);
}

TEST_F(CompositionTest, ToggleAlternates) {
  load(1, 1);
  params.set(C(1, 1) + "/triggerstyle", std::string("Toggle"));
  fire(1, 1);
  EXPECT_TRUE(on(1, 1));
  fire(1, 1);
  EXPECT_FALSE(on(1, 1));
  fire(1, 1);
  EXPECT_TRUE(on(1, 1));
}

TEST_F(CompositionTest, PianoPlaysWhileHeld) {
  load(1, 1);
  params.set(C(1, 1) + "/triggerstyle", std::string("Piano"));
  fire(1, 1);
  EXPECT_TRUE(on(1, 1));
  params.trigger(C(1, 1) + "/release");
  comp->tick(0);
  EXPECT_FALSE(on(1, 1));
}

TEST_F(CompositionTest, NormalRestartsAndReleaseIgnored) {
  load(1, 1);
  fire(1, 1);
  run(1);
  params.trigger(C(1, 1) + "/release");
  fire(1, 1);
  EXPECT_TRUE(on(1, 1));
  run(0.1);
  run(0.1);
  EXPECT_LT(params.getFloat(C(1, 1) + "/transport/position"), 0.05);
}

TEST_F(CompositionTest, FaderStartRestarts) {
  load(1, 1);
  params.set(C(1, 1) + "/faderstart", std::string("On"));
  fire(1, 1);
  run(5);
  EXPECT_GT(params.getFloat(C(1, 1) + "/transport/position"), 0.4);
  params.set(L(1) + "/opacity", 0.0);
  run(0.2);
  params.set(L(1) + "/opacity", 1.0);
  run(0.2);
  EXPECT_LT(params.getFloat(C(1, 1) + "/transport/position"), 0.05);
}

TEST_F(CompositionTest, FaderPickUpResumes) {
  load(1, 1);
  params.set(C(1, 1) + "/faderstart", std::string("Pick-up"));
  fire(1, 1);
  run(5);
  params.set(L(1) + "/opacity", 0.0);
  run(0.2);
  const double held = params.getFloat(C(1, 1) + "/transport/position");
  run(2);
  EXPECT_NEAR(params.getFloat(C(1, 1) + "/transport/position"), held, 0.03);
  params.set(L(1) + "/opacity", 1.0);
  run(1);
  EXPECT_GT(params.getFloat(C(1, 1) + "/transport/position"), held + 0.05);
}

TEST_F(CompositionTest, AutopilotNextWalksAllAndWraps) {
  std::vector<int> seen;
  for (int c = 1; c <= 4; ++c) {
    load(1, c);
    params.set(C(1, c) + "/transport/duration", 0.5);
    params.set(C(1, c) + "/autopilot/action", std::string("Next"));
  }
  fire(1, 1);
  for (int f = 0; f < 240; ++f) {
    comp->tick(kDt);
    if (seen.empty() || seen.back() != active(1)) seen.push_back(active(1));
  }
  ASSERT_GE(seen.size(), 5u);
  EXPECT_EQ(std::vector<int>(seen.begin(), seen.begin() + 5), (std::vector<int>{1, 2, 3, 4, 1}));
}

TEST_F(CompositionTest, AutopilotHonoursLoopsAndLayerDetermined) {
  load(1, 1);
  load(1, 2);
  params.set(L(1) + "/autopilot", std::string("Next"));
  params.set(C(1, 1) + "/transport/duration", 0.5);
  params.set(C(1, 1) + "/autopilot/loops", int64_t{3});
  fire(1, 1);
  run(1.4);
  EXPECT_EQ(active(1), 1);
  run(0.2);
  EXPECT_EQ(active(1), 2);
}

TEST_F(CompositionTest, AutopilotFirstLastAndClip) {
  load(1, 1);
  load(1, 2);
  load(1, 4);
  params.set(C(1, 2) + "/transport/duration", 0.5);
  params.set(C(1, 2) + "/autopilot/action", std::string("Last"));
  fire(1, 2);
  run(0.6);
  EXPECT_EQ(active(1), 4);
  params.set(C(1, 4) + "/transport/duration", 0.5);
  params.set(C(1, 4) + "/autopilot/action", std::string("First"));
  run(0.6);
  EXPECT_EQ(active(1), 1);
  params.set(C(1, 1) + "/transport/duration", 0.5);
  params.set(C(1, 1) + "/autopilot/action", std::string("Clip"));
  params.set(C(1, 1) + "/autopilot/target", int64_t{2});
  run(0.6);
  EXPECT_EQ(active(1), 2);
}

TEST_F(CompositionTest, PlayOnceEjectLeavesLayerEmpty) {
  load(1, 1);
  params.set(C(1, 1) + "/transport/duration", 0.5);
  params.set(C(1, 1) + "/transport/playmode", std::string("Play Once and Eject"));
  fire(1, 1);
  run(0.6);
  EXPECT_EQ(active(1), 0);
}

TEST_F(CompositionTest, PlayOnceHoldKeepsLastFrame) {
  load(1, 1);
  params.set(C(1, 1) + "/transport/duration", 0.5);
  params.set(C(1, 1) + "/transport/playmode", std::string("Play Once and Hold"));
  fire(1, 1);
  run(1);
  EXPECT_EQ(active(1), 1);
  EXPECT_NEAR(params.getFloat(C(1, 1) + "/transport/position"), 1, 1e-6);
}

TEST_F(CompositionTest, ClipTransitionTakesItsDuration) {
  load(1, 1, kClipA);
  load(1, 2, kClipB);
  params.set(C(1, 2) + "/transition/mode", std::string("Clip"));
  params.set(C(1, 2) + "/transition/duration", 2.0);
  params.set(C(1, 2) + "/transition/blend", std::string("Wipe Left"));
  fire(1, 1);
  run(0.1);
  fire(1, 2);
  run(1);
  auto mid = comp->plan();
  ASSERT_TRUE(mid.layers[0].previous);
  EXPECT_NEAR(mid.layers[0].mix, 0.5, 0.02);
  EXPECT_EQ(mid.layers[0].transition, 2);
  run(0.9);
  EXPECT_TRUE(comp->plan().layers[0].previous);
  run(0.2);
  EXPECT_FALSE(comp->plan().layers[0].previous);
  EXPECT_FALSE(on(1, 1));
  EXPECT_TRUE(on(1, 2));
}

TEST_F(CompositionTest, LayerTransitionUsedByDefault) {
  load(1, 1);
  load(1, 2, kClipB);
  params.set(L(1) + "/transition/duration", 1.0);
  fire(1, 1);
  run(0.1);
  fire(1, 2);
  run(0.5);
  EXPECT_NEAR(comp->plan().layers[0].mix, 0.5, 0.03);
}

TEST_F(CompositionTest, BpmSyncIgnoresCompositionSpeed) {
  load(1, 1);
  params.set(C(1, 1) + "/transport/mode", std::string("BPM Sync"));
  params.set(C(1, 1) + "/transport/beats", int64_t{8});  // 4 s a 120 BPM
  params.set("/composition/speed", 5.0);
  fire(1, 1);
  run(2);
  EXPECT_NEAR(params.getFloat(C(1, 1) + "/transport/position"), 0.5, 0.03);
}

TEST_F(CompositionTest, CompositionSpeedScalesTimeline) {
  load(1, 1);
  params.set("/composition/speed", 2.0);
  fire(1, 1);
  run(1);
  EXPECT_NEAR(params.getFloat(C(1, 1) + "/transport/position"), 0.2, 0.02);
}

TEST_F(CompositionTest, CompositionColumnsNextAdvances) {
  load(1, 1);
  load(1, 2);
  params.trigger("/composition/columns/next");
  comp->tick(0);
  EXPECT_EQ(params.getInt("/composition/columns/current"), 2);
  params.trigger("/composition/columns/previous");
  comp->tick(0);
  EXPECT_EQ(params.getInt("/composition/columns/current"), 1);
}

TEST_F(CompositionTest, LayerNextPreviousSkipEmpty) {
  load(1, 1);
  load(1, 3);
  fire(1, 1);
  params.trigger(L(1) + "/next");
  comp->tick(0);
  EXPECT_EQ(active(1), 3);
  params.trigger(L(1) + "/previous");
  comp->tick(0);
  EXPECT_EQ(active(1), 1);
}

TEST_F(CompositionTest, LockBlocksEjectAndChange) {
  load(1, 1);
  load(1, 2);
  fire(1, 1);
  params.set(L(1) + "/lock", true);
  fire(1, 2);
  EXPECT_EQ(active(1), 1);
  params.trigger(L(1) + "/eject");
  comp->tick(0);
  EXPECT_EQ(active(1), 1);
}

TEST_F(CompositionTest, SoloAndBypassInPlan) {
  params.set(L(2) + "/solo", true);
  auto p = comp->plan();
  EXPECT_FALSE(p.layers[0].visible);
  EXPECT_TRUE(p.layers[1].visible);
  params.set(L(2) + "/bypass", true);
  EXPECT_FALSE(comp->plan().layers[1].visible);
}

TEST_F(CompositionTest, CrossfaderScalesBuses) {
  params.set(L(1) + "/crossfaderbus", std::string("A"));
  params.set(L(2) + "/crossfaderbus", std::string("B"));
  params.set("/composition/crossfader/phase", 0.0);
  auto p = comp->plan();
  EXPECT_FLOAT_EQ(p.layers[0].opacity, 1);
  EXPECT_FLOAT_EQ(p.layers[1].opacity, 0);
  EXPECT_FLOAT_EQ(p.layers[2].opacity, 1);
  params.set("/composition/crossfader/phase", 1.0);
  p = comp->plan();
  EXPECT_FLOAT_EQ(p.layers[0].opacity, 0);
  EXPECT_FLOAT_EQ(p.layers[1].opacity, 1);
  params.set("/composition/crossfader/phase", 0.5);
  EXPECT_FLOAT_EQ(comp->plan().layers[0].opacity, 1);
}

TEST_F(CompositionTest, TransportPositionSeeks) {
  load(1, 1);
  fire(1, 1);
  params.set(C(1, 1) + "/transport/direction", std::string("Pause"));
  params.set(C(1, 1) + "/transport/position", 0.7);
  run(0.2);
  EXPECT_NEAR(params.getFloat(C(1, 1) + "/transport/position"), 0.7, 1e-6);
}
