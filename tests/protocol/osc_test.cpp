#include <gtest/gtest.h>

#include "compositor/CompositionModule.h"
#include "control/OscCodec.h"
#include "control/OscRouter.h"
#include "core/EventBus.h"
#include "core/ModuleRegistry.h"
#include "core/generated/Parameters.gen.h"

using namespace vj;
using namespace vj::control;

class Osc : public ::testing::Test {
 protected:
  void SetUp() override {
    params.defineAll(schema::parameters());
    modules.add(std::make_unique<compositor::CompositionModule>(compositor::CompositionShape{2, 8, 1}));
    ASSERT_EQ(modules.startAll(ctx), "");
    comp = modules.get<compositor::CompositionModule>("composition")->composition();
  }
  void TearDown() override { modules.stopAll(); }
  void send(const std::string& a, std::vector<OscArg> args) { applyOsc({a, std::move(args)}, params); }
  ParameterRegistry params;
  EventBus events;
  ModuleRegistry modules;
  EngineContext ctx{params, events, modules, nullptr, nullptr};
  compositor::Composition* comp = nullptr;
};

TEST(OscCodecTest, RoundTripAllTypes) {
  OscMessage m{"/a/b", {int32_t{7}, 0.25f, std::string("hola"), OscColor{0x11223344}}};
  const std::string e = encodeOsc(m);
  EXPECT_EQ(e.size() % 4, 0u);
  const auto out = parseOsc(reinterpret_cast<const uint8_t*>(e.data()), e.size());
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].address, "/a/b");
  EXPECT_EQ(out[0].args, m.args);
}

TEST(OscCodecTest, BundlesAndGarbage) {
  const std::string b = encodeBundle({{"/x", {int32_t{1}}}, {"/y", {}}});
  EXPECT_EQ(parseOsc(reinterpret_cast<const uint8_t*>(b.data()), b.size()).size(), 2u);
  const uint8_t junk[] = {0xFF, 1, 2, 3, 4, 5, 6, 7};
  EXPECT_TRUE(parseOsc(junk, sizeof junk).empty());
  std::string cut = encodeOsc({"/abc", {std::string("texto largo")}});
  EXPECT_TRUE(parseOsc(reinterpret_cast<const uint8_t*>(cut.data()), cut.size() - 6).empty());
}

TEST_F(Osc, ManualExamples) {
  send("/composition/layers/1/video/opacity", {0.25f});
  EXPECT_DOUBLE_EQ(params.getFloat("/composition/layers/1/opacity"), 0.25);

  params.set("/composition/layers/2/clips/8/path", std::string("vj://solid/FF0000"));
  send("/composition/layers/2/clips/8/transport/position", {0.0f});
  EXPECT_DOUBLE_EQ(params.getFloat("/composition/layers/2/clips/8/transport/position"), 0);

  send("/composition/layers/1/video/mixer/blendmode", {int32_t{3}});
  EXPECT_EQ(params.getString("/composition/layers/1/blend"), "Darken");

  params.set("/composition/activelayer", int64_t{2});
  send("/composition/selectedlayer/video/mixer/blendmode", {std::string("Alpha")});
  EXPECT_EQ(params.getString("/composition/layers/2/blend"), "Alpha");
}

TEST_F(Osc, FloatIsNormalizedAndRelativeUsesRealUnits) {
  const std::string p = "/composition/layers/1/clips/1/transform/x";
  send(p, {0.5f});
  EXPECT_NEAR(params.getFloat(p), 0, 1e-3);
  send(p, {std::string("a"), 320.f});
  EXPECT_DOUBLE_EQ(params.getFloat(p), 320);
  send(p, {std::string("+"), 50.f});
  EXPECT_DOUBLE_EQ(params.getFloat(p), 370);
  send(p, {std::string("-"), 70.f});
  EXPECT_DOUBLE_EQ(params.getFloat(p), 300);
  send(p, {std::string("*"), 2.f});
  EXPECT_DOUBLE_EQ(params.getFloat(p), 600);
}

TEST_F(Osc, QueryReturnsValueAtSameAddress) {
  params.set("/composition/layers/1/blend", std::string("Cut"));
  const auto r = applyOsc({"/composition/layers/1/video/mixer/blendmode", {std::string("?")}}, params);
  ASSERT_TRUE(r);
  EXPECT_EQ(r->address, "/composition/layers/1/video/mixer/blendmode");
  EXPECT_EQ(std::get<int32_t>(r->args[0]), 2);
}

TEST_F(Osc, ClipTriggerPressAndRelease) {
  params.set("/composition/layers/1/clips/1/path", std::string("vj://solid/FF0000"));
  params.set("/composition/layers/1/clips/1/triggerstyle", std::string("Piano"));
  comp->tick(0);
  comp->waitForLoads();
  comp->tick(0);
  send("/composition/layers/1/clips/1/connect", {int32_t{1}});
  comp->tick(0);
  EXPECT_TRUE(params.getBool("/composition/layers/1/clips/1/connected"));
  send("/composition/layers/1/clips/1/connect", {int32_t{0}});
  comp->tick(0);
  EXPECT_FALSE(params.getBool("/composition/layers/1/clips/1/connected"));
}

TEST_F(Osc, TextGoesToTextSource) {
  const int e = comp->effects().add("/composition/layers/1/clips/1", "text");
  const std::string p = comp->effects().instancePath("/composition/layers/1/clips/1", e) + "/text";
  send(p, {std::string("hola por OSC")});
  EXPECT_EQ(params.getString(p), "hola por OSC");
}

TEST_F(Osc, UnknownAddressAndBadValuesAreReported) {
  std::string error;
  applyOsc({"/no/existe", {1.f}}, params, &error);
  EXPECT_FALSE(error.empty());
  error.clear();
  applyOsc({"/composition/master", {}}, params, &error);
  EXPECT_FALSE(error.empty());
  error.clear();
  applyOsc({"/composition/master", {OscColor{1}}}, params, &error);
  EXPECT_FALSE(error.empty());
}

TEST_F(Osc, OutputUsesPublicNames) {
  EXPECT_EQ(parameterToOsc("/composition/layers/1/opacity"), "/composition/layers/1/video/opacity");
  EXPECT_EQ(parameterToOsc("/composition/layers/1/blend"), "/composition/layers/1/video/mixer/blendmode");
  EXPECT_EQ(parameterToOsc("/composition/master"), "/composition/master");
}
