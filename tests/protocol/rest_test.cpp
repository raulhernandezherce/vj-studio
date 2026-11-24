#include <gtest/gtest.h>

#include <regex>

#include "control/ControlServer.h"
#include "control/RestApi.h"
#include "core/generated/Parameters.gen.h"

using namespace vj;
using namespace vj::control;
using nlohmann::json;

class Rest : public ::testing::Test {
 protected:
  void SetUp() override {
    params.defineAll(schema::parameters());
    params.instantiate("/composition/layers/1", schema::layerParameters());
    params.instantiate("/composition/layers/1/clips/1", schema::clipParameters());
    params.instantiate("/composition/columns/1", schema::columnParameters());
    commands.add("effects.catalog", [](const json&) { return json::array({"hue"}); });
    commands.add("effects.list", [](const json&) { return json::array(); });
    commands.add("effect.add", [](const json& a) { return json{{"type", a.at("type")}}; });
    commands.add("effect.remove", [](const json&) { return json::object(); });
    commands.add("eco", [](const json& a) { return a; });
    pump = std::thread([this] {
      while (!done) {
        commands.pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    });
  }
  void TearDown() override {
    done = true;
    pump.join();
  }
  HttpResponse call(const std::string& m, const std::string& p, const std::string& b = "") { return handleRest(m, p, b, params, &commands); }
  ParameterRegistry params;
  CommandBus commands;
  std::atomic<bool> done{false};
  std::thread pump;
};

TEST_F(Rest, EveryRouteInTheSchemaResponds) {
  for (const auto& r : restRoutes()) {
    std::string path = r["path"];
    path = std::regex_replace(path, std::regex("\\{ruta\\}"), "composition/master");
    path = std::regex_replace(path, std::regex("\\{entidad\\}"), "composition/layers/1");
    path = std::regex_replace(path, std::regex("\\{nombre\\}"), "eco");
    path = std::regex_replace(path, std::regex("\\{[lc]\\}"), "1");
    path = std::regex_replace(path, std::regex("\\{id\\}"), "3");
    if (r["path"] == "/api/v1/trigger/{ruta}") path = "/api/v1/trigger/composition/eject";
    params.publish("/composition/layers/1/clips/1/thumbnail", "data:image/jpeg;base64,/9j/");
    const std::string body = r["method"] == "PUT" ? R"({"value":0.5})" : r["method"] == "POST" ? R"({"type":"hue"})" : "";
    const auto res = call(r["method"], path, body);
    EXPECT_EQ(res.status, 200) << r["method"] << " " << path << ": " << res.body;
  }
}

TEST_F(Rest, SetGetResetAndErrors) {
  EXPECT_EQ(json::parse(call("PUT", "/api/v1/parameter/composition/master", R"({"value":0.25})").body)["value"], 0.25);
  EXPECT_DOUBLE_EQ(params.getFloat("/composition/master"), 0.25);
  EXPECT_EQ(json::parse(call("GET", "/api/v1/parameter/composition/master").body)["value"], 0.25);
  EXPECT_EQ(json::parse(call("DELETE", "/api/v1/parameter/composition/master").body)["value"], 1.0);
  EXPECT_EQ(call("GET", "/api/v1/parameter/no/existe").status, 404);
  EXPECT_EQ(call("PUT", "/api/v1/parameter/engine/fps", R"({"value":1})").status, 409);
  EXPECT_EQ(call("PUT", "/api/v1/parameter/composition/master", "{no").status, 400);
  EXPECT_EQ(call("GET", "/api/v1/nada").status, 404);
  EXPECT_EQ(call("POST", "/api/v1/command/nada").status, 400);
}

TEST_F(Rest, ThumbnailIsBinaryJpeg) {
  params.publish("/composition/layers/1/clips/1/thumbnail", "data:image/jpeg;base64,/9j/");
  const auto r = call("GET", "/api/v1/composition/layers/1/clips/1/thumbnail");
  EXPECT_EQ(r.contentType, "image/jpeg");
  EXPECT_EQ(static_cast<uint8_t>(r.body[0]), 0xFF);
}

TEST(HostCheck, BlocksDnsRebinding) {
  EXPECT_TRUE(ControlServer::hostAllowed("127.0.0.1:7777"));
  EXPECT_TRUE(ControlServer::hostAllowed("localhost:7777"));
  EXPECT_TRUE(ControlServer::hostAllowed("localhost"));
  EXPECT_FALSE(ControlServer::hostAllowed("evil.example:7777"));
}
