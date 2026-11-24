#include <gtest/gtest.h>

#include "control/ApiDispatcher.h"
#include "core/generated/Parameters.gen.h"

using namespace vj;
using namespace vj::control;
using nlohmann::json;

class Api : public ::testing::Test {
 protected:
  void SetUp() override { params.defineAll(schema::parameters()); }
  json call(const json& j) { return api.handle(j, subs); }
  ParameterRegistry params;
  ApiDispatcher api{params};
  Subscriptions subs;
};

TEST_F(Api, GetSetResetRoundTrip) {
  auto r = call({{"id", 7}, {"action", "set"}, {"parameter", "/output/opacity"}, {"value", 0.25}});
  EXPECT_TRUE(r["ok"]);
  EXPECT_EQ(r["id"], 7);
  EXPECT_EQ(call({{"action", "get"}, {"parameter", "/output/opacity"}})["value"], 0.25);
  EXPECT_EQ(call({{"action", "reset"}, {"parameter", "/output/opacity"}})["value"], 1.0);
}

TEST_F(Api, SetReturnsClampedValue) {
  auto r = call({{"action", "set"}, {"parameter", "/output/opacity"}, {"value", 9}});
  EXPECT_EQ(r["value"], 1.0);
}

TEST_F(Api, Errors) {
  EXPECT_FALSE(call({{"action", "get"}, {"parameter", "/nope"}})["ok"]);
  EXPECT_FALSE(call({{"action", "set"}, {"parameter", "/engine/fps"}, {"value", 1}})["ok"]);
  EXPECT_FALSE(call({{"action", "set"}, {"parameter", "/output/opacity"}})["ok"]);
  EXPECT_FALSE(call({{"action", "set"}, {"parameter", "/output/opacity"}, {"value", json::array()}})["ok"]);
  EXPECT_FALSE(call({{"action", "boom"}, {"parameter", "/output/opacity"}})["ok"]);
  EXPECT_FALSE(call({{"parameter", "/output/opacity"}})["ok"]);
  EXPECT_FALSE(call(json::array())["ok"]);
  EXPECT_FALSE(json::parse(api.handleText("{no", subs))["ok"]);
}

TEST_F(Api, Trigger) {
  int n = 0;
  params.addListener("/composition/eject", [&](const ParamDesc&, const Value&) { ++n; });
  EXPECT_TRUE(call({{"action", "trigger"}, {"parameter", "/composition/eject"}})["ok"]);
  EXPECT_EQ(n, 1);
  EXPECT_FALSE(call({{"action", "trigger"}, {"parameter", "/output/opacity"}})["ok"]);
  EXPECT_FALSE(call({{"action", "get"}, {"parameter", "/composition/eject"}})["ok"]);
}

TEST_F(Api, SubscribeTracksPaths) {
  auto r = call({{"action", "subscribe"}, {"parameter", "/output/opacity"}});
  EXPECT_EQ(r["value"], 1.0);
  EXPECT_TRUE(subs.matches("/output/opacity"));
  EXPECT_FALSE(subs.matches("/composition/speed"));
  call({{"action", "subscribe"}, {"parameter", "*"}});
  EXPECT_TRUE(subs.matches("/composition/speed"));
  call({{"action", "unsubscribe"}, {"parameter", "*"}});
  call({{"action", "unsubscribe"}, {"parameter", "/output/opacity"}});
  EXPECT_FALSE(subs.matches("/output/opacity"));
  EXPECT_FALSE(call({{"action", "subscribe"}, {"parameter", "/nope"}})["ok"]);
}

TEST_F(Api, ListReturnsEveryParameter) {
  auto r = call({{"action", "list"}});
  EXPECT_EQ(r["parameters"].size(), schema::parameters().size());
}
