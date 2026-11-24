#include <gtest/gtest.h>

#include <atomic>
#include <thread>

#include "core/ParameterRegistry.h"

using namespace vj;

namespace {
ParamDesc floatParam(const std::string& path, bool ro = false) {
  return {path, ParamType::Float, Value{0.5}, 0, 1, ro, "", "", "", "M0"};
}
ParamDesc trigger(const std::string& path) {
  return {path, ParamType::Trigger, Value{false}, 0, 0, false, "", "", "", "M0"};
}
}  // namespace

TEST(ParameterRegistry, ClampsToRange) {
  ParameterRegistry r;
  r.define(floatParam("/a"));
  EXPECT_EQ(r.set("/a", 3.0), ParamError::None);
  EXPECT_DOUBLE_EQ(r.getFloat("/a"), 1.0);
  r.set("/a", -3.0);
  EXPECT_DOUBLE_EQ(r.getFloat("/a"), 0.0);
}

TEST(ParameterRegistry, RejectsWrongTypeAndNaN) {
  ParameterRegistry r;
  r.define(floatParam("/a"));
  EXPECT_EQ(r.set("/a", std::string("x")), ParamError::TypeMismatch);
  EXPECT_EQ(r.set("/a", std::nan("")), ParamError::TypeMismatch);
  EXPECT_DOUBLE_EQ(r.getFloat("/a"), 0.5);
}

TEST(ParameterRegistry, ReadOnlyOnlyByPublish) {
  ParameterRegistry r;
  r.define(floatParam("/a", true));
  EXPECT_EQ(r.set("/a", 0.1), ParamError::ReadOnly);
  EXPECT_EQ(r.reset("/a"), ParamError::ReadOnly);
  EXPECT_EQ(r.publish("/a", 0.1), ParamError::None);
  EXPECT_DOUBLE_EQ(r.getFloat("/a"), 0.1);
}

TEST(ParameterRegistry, ResetRestoresDefault) {
  ParameterRegistry r;
  r.define(floatParam("/a"));
  r.set("/a", 0.9);
  EXPECT_EQ(r.reset("/a"), ParamError::None);
  EXPECT_DOUBLE_EQ(r.getFloat("/a"), 0.5);
}

TEST(ParameterRegistry, UnknownPath) {
  ParameterRegistry r;
  EXPECT_EQ(r.set("/x", 1.0), ParamError::UnknownPath);
  EXPECT_EQ(r.trigger("/x"), ParamError::UnknownPath);
  EXPECT_FALSE(r.get("/x"));
}

TEST(ParameterRegistry, DuplicateDefineThrows) {
  ParameterRegistry r;
  r.define(floatParam("/a"));
  EXPECT_THROW(r.define(floatParam("/a")), std::logic_error);
}

TEST(ParameterRegistry, ListenerFiresOnlyOnChange) {
  ParameterRegistry r;
  r.define(floatParam("/a"));
  int calls = 0;
  r.addListener("/a", [&](const ParamDesc&, const Value&) { ++calls; });
  r.set("/a", 0.5);
  EXPECT_EQ(calls, 0);
  r.set("/a", 0.6);
  r.set("/a", 0.6);
  EXPECT_EQ(calls, 1);
}

TEST(ParameterRegistry, WildcardAndRemoval) {
  ParameterRegistry r;
  r.define(floatParam("/a"));
  r.define(floatParam("/b"));
  int calls = 0;
  auto id = r.addListener("", [&](const ParamDesc&, const Value&) { ++calls; });
  r.set("/a", 0.1);
  r.set("/b", 0.1);
  EXPECT_EQ(calls, 2);
  r.removeListener(id);
  r.set("/a", 0.2);
  EXPECT_EQ(calls, 2);
}

TEST(ParameterRegistry, ListenerMayWriteAnotherParameter) {
  ParameterRegistry r;
  r.define(floatParam("/a"));
  r.define(floatParam("/b"));
  r.addListener("/a", [&](const ParamDesc&, const Value& v) { r.set("/b", std::get<double>(v)); });
  r.set("/a", 0.25);
  EXPECT_DOUBLE_EQ(r.getFloat("/b"), 0.25);
}

TEST(ParameterRegistry, TriggerSemantics) {
  ParameterRegistry r;
  r.define(trigger("/t"));
  r.define(floatParam("/a"));
  int calls = 0;
  r.addListener("/t", [&](const ParamDesc&, const Value&) { ++calls; });
  EXPECT_EQ(r.trigger("/t"), ParamError::None);
  EXPECT_EQ(r.trigger("/t"), ParamError::None);
  EXPECT_EQ(calls, 2);
  EXPECT_EQ(r.trigger("/a"), ParamError::NotATrigger);
  EXPECT_EQ(r.set("/t", true), ParamError::IsTrigger);
  EXPECT_FALSE(r.get("/t"));
}

TEST(ParameterRegistry, IntRoundsAndAcceptsFloat) {
  ParameterRegistry r;
  r.define({"/i", ParamType::Int, Value{int64_t{0}}, 0, 10, false, "", "", "", "M0"});
  r.set("/i", 2.6);
  EXPECT_EQ(r.getInt("/i"), 3);
  r.set("/i", int64_t{50});
  EXPECT_EQ(r.getInt("/i"), 10);
}

TEST(ParameterRegistry, ConcurrentWritersKeepValueInRange) {
  ParameterRegistry r;
  r.define(floatParam("/a"));
  std::atomic<int> seen{0};
  r.addListener("/a", [&](const ParamDesc&, const Value& v) {
    const double d = std::get<double>(v);
    if (d >= 0 && d <= 1) ++seen;
  });
  std::vector<std::thread> ts;
  for (int t = 0; t < 4; ++t)
    ts.emplace_back([&, t] {
      for (int i = 0; i < 2000; ++i) r.set("/a", (i % 100) / 100.0 + t * 0.001);
    });
  for (auto& t : ts) t.join();
  EXPECT_GT(seen.load(), 0);
}

TEST(ParameterRegistry, EnumAcceptsOnlyItsOptions) {
  ParameterRegistry r;
  r.define({"/e", ParamType::Enum, Value{std::string("A")}, 0, 0, false, "", "", "", "M1", {"A", "B"}});
  EXPECT_EQ(r.set("/e", std::string("B")), ParamError::None);
  EXPECT_EQ(r.getString("/e"), "B");
  EXPECT_EQ(r.set("/e", std::string("C")), ParamError::TypeMismatch);
  EXPECT_EQ(r.set("/e", 1.0), ParamError::TypeMismatch);
  EXPECT_EQ(r.getString("/e"), "B");
}

TEST(ParameterRegistry, InstantiatePrefixesTemplate) {
  ParameterRegistry r;
  r.instantiate("/layers/2", {floatParam("video/opacity")});
  EXPECT_TRUE(r.describe("/layers/2/video/opacity"));
}
