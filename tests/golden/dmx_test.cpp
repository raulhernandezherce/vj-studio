#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "compositor/CompositionModule.h"
#include "core/CommandBus.h"
#include "core/ModuleRegistry.h"
#include "core/SystemStats.h"
#include "core/generated/Parameters.gen.h"
#include "dmx/DmxModule.h"
#include "output/OutputModule.h"
#include "project/ProjectModule.h"

using namespace vj;
using namespace vj::dmx;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {
std::map<std::string, FixtureType> types() {
  std::map<std::string, FixtureType> t;
  for (const auto& b : builtinTypes()) t[b.name] = b;
  return t;
}
Fixture strip(int id, int pixels, const char* type = "RGB") {
  Fixture f;
  f.id = id;
  f.name = "F" + std::to_string(id);
  f.type = type;
  f.columns = pixels;
  return f;
}
}  // namespace

TEST(Dmx, SixteenRgbPixelsTakeFortyEightChannels) { EXPECT_EQ(channelCount(strip(1, 16), types().at("RGB")), 48); }

TEST(Dmx, ConsecutiveFixturesStartAt1_49_97) {
  dmx::Setup s;
  s.fixtures = {strip(1, 16), strip(2, 16), strip(3, 16)};
  autoAssign(s, types());
  EXPECT_EQ(s.fixtures[0].channel, 1);
  EXPECT_EQ(s.fixtures[1].channel, 49);
  EXPECT_EQ(s.fixtures[2].channel, 97);
  EXPECT_TRUE(overlaps(s, types()).empty());
  s.fixtures[1].channel = 40;
  EXPECT_EQ(overlaps(s, types()).size(), 1u);
}

TEST(Dmx, PixelsNeverSplitAcrossUniverses) {
  dmx::Setup s;
  s.fixtures = {strip(1, 171)};  // 170 píxeles caben en 510 canales
  std::vector<uint8_t> px(4, 255);
  const auto out = render(s, types(), px, 1, 1);
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(out.at(1)[509], 255);
  EXPECT_EQ(out.at(1)[510], 0);
  EXPECT_EQ(out.at(2)[0], 255);
  EXPECT_EQ(out.at(2)[2], 255);
  EXPECT_EQ(out.at(2)[3], 0);
}

TEST(Dmx, SamplesItsZoneWithColorOrderAndFlip) {
  // Imagen de 2 píxeles: rojo a la izquierda, azul a la derecha.
  const std::vector<uint8_t> px = {255, 0, 0, 255, 0, 0, 255, 255};
  dmx::Setup s;
  Fixture f = strip(1, 2, "GRB");
  s.fixtures = {f};
  auto out = render(s, types(), px, 2, 1).at(1);
  EXPECT_EQ(out[1], 255) << "G R B: el rojo va en el segundo canal";
  EXPECT_EQ(out[5], 255);
  s.fixtures[0].flipX = true;
  out = render(s, types(), px, 2, 1).at(1);
  EXPECT_EQ(out[2], 255) << "volteado, el azul va primero";
  s.fixtures[0].flipX = false;
  s.fixtures[0].brightness = 0.5;
  EXPECT_NEAR(render(s, types(), px, 2, 1).at(1)[1], 128, 1);
}

TEST(Dmx, WhiteChannelAndFixedValues) {
  auto t = types();
  t["Prueba"] = {"Prueba", {"w", "l", "200"}};
  dmx::Setup s;
  s.fixtures = {strip(1, 1, "Prueba")};
  const std::vector<uint8_t> px = {100, 100, 100, 255};
  const auto out = render(s, t, px, 1, 1).at(1);
  EXPECT_EQ(out[0], 100);
  EXPECT_EQ(out[1], 100);
  EXPECT_EQ(out[2], 200);
}

TEST(Dmx, ArtNetPacketsFollowTheSpec) {
  Universe u{};
  u[0] = 7;
  const auto p = artDmxPacket(18, 5, u);
  ASSERT_EQ(p.size(), 530u);
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(p.data())), "Art-Net");
  EXPECT_EQ(p[8], 0x00);  // OpOutput, little endian
  EXPECT_EQ(p[9], 0x50);
  EXPECT_EQ(p[11], 14);
  EXPECT_EQ(p[12], 5);
  EXPECT_EQ(p[14], 17) << "subred y universo: Art-Net cuenta desde 0";
  EXPECT_EQ(p[16], 2);  // 512 canales, big endian
  EXPECT_EQ(p[17], 0);
  EXPECT_EQ(p[18], 7);
  EXPECT_EQ(artSyncPacket()[9], 0x52);
}

TEST(Dmx, BadSetupIsRejected) { EXPECT_THROW(setupFromJson(json{{"format", "otro"}}), std::runtime_error); }

TEST(Stats, ReportsThisProcess) {
  SystemSampler s;
  s.sample();
  volatile double x = 0;
  const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
  while (std::chrono::steady_clock::now() < until) x = x + 1;
  const auto r = s.sample();
  EXPECT_GT(r.cpu, 50);
  EXPECT_LT(r.cpu, 150) << "un hilo ocupado es un núcleo";
  EXPECT_GT(r.memoryMb, 1);
  EXPECT_GE(r.gpu, -1);
}

class DmxOutput : public ::testing::Test {
 protected:
  void SetUp() override {
    dir = fs::temp_directory_path() / ("vj_dmx_" + std::to_string(getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    params.defineAll(schema::parameters());
    params.set("/composition/width", int64_t{64});
    params.set("/composition/height", int64_t{36});
    modules.add(std::make_unique<compositor::CompositionModule>(compositor::CompositionShape{2, 2, 1}));
    output::OutputOptions o;
    o.hidden = true;
    o.documents = dir.string();
    modules.add(std::make_unique<output::OutputModule>(o));
    modules.add(std::make_unique<DmxModule>(dir.string()));
    if (modules.startAll(ctx) != "") GTEST_SKIP() << "sin OpenGL";
    rx = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(bind(rx, reinterpret_cast<sockaddr*>(&a), sizeof a), 0);
    socklen_t len = sizeof a;
    getsockname(rx, reinterpret_cast<sockaddr*>(&a), &len);
    timeval t{0, 20000};
    setsockopt(rx, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof t);
    params.set("/dmx/port", int64_t{ntohs(a.sin_port)});
    params.set("/dmx/target", std::string("127.0.0.1"));
  }
  void TearDown() override {
    if (rx >= 0) close(rx);
    modules.stopAll();
    fs::remove_all(dir);
  }
  fs::path dir;
  ParameterRegistry params;
  EventBus events;
  ModuleRegistry modules;
  CommandBus commands;
  EngineContext ctx{params, events, modules, &commands, nullptr};
  int rx = -1;
};

TEST_F(DmxOutput, CompositionColorReachesTheNetwork) {
  auto* comp = modules.get<compositor::CompositionModule>("composition")->composition();
  params.set("/composition/layers/1/clips/1/path", std::string("vj://solid/FF8000"));
  for (int i = 0; i < 4; ++i) {
    comp->tick(0);
    comp->waitForLoads();
  }
  params.trigger("/composition/layers/1/clips/1/connect");
  dmx::Setup s;
  s.fixtures = {strip(1, 16)};
  commands.run("dmx.set", {{"setup", toJson(s)}});
  params.set("/dmx/enabled", true);
  uint8_t buf[600];
  ssize_t got = 0;
  int ops = 0;
  uint8_t r = 0, g = 0;
  for (int i = 0; i < 100 && !(r && ops >= 2); ++i) {
    modules.tickAll(0.02);
    got = recv(rx, buf, sizeof buf, 0);
    if (got == 530) {
      r = buf[18];
      g = buf[19];
    }
    if (got > 0) ++ops;
  }
  EXPECT_GT(r, 250);
  EXPECT_NEAR(g, 128, 2);
  EXPECT_EQ(params.getString("/dmx/status"), "enviando 1 universos");
}

TEST_F(DmxOutput, DelayHoldsTheFrame) {
  dmx::Setup s;
  s.fixtures = {strip(1, 1)};
  commands.run("dmx.set", {{"setup", toJson(s)}});
  params.set("/dmx/delay", int64_t{1000});
  params.set("/dmx/enabled", true);
  uint8_t buf[600];
  for (int i = 0; i < 10; ++i) {
    modules.tickAll(0.05);
    EXPECT_LT(recv(rx, buf, sizeof buf, 0), 0) << "con 1 s de retardo, a 0,5 s no ha salido nada";
  }
  bool arrived = false;
  for (int i = 0; i < 30 && !arrived; ++i) {
    modules.tickAll(0.05);
    arrived = recv(rx, buf, sizeof buf, 0) > 0;
  }
  EXPECT_TRUE(arrived);
}

TEST_F(DmxOutput, SetupAndCustomFixtureSurviveRestart) {
  commands.run("dmx.type.save", {{"name", "Par"}, {"channels", {"r", "g", "b", "w", "255"}}});
  dmx::Setup s;
  s.fixtures = {strip(1, 2, "Par")};
  const json r = commands.run("dmx.set", {{"setup", toJson(s)}});
  EXPECT_EQ(r["channels"]["1"], 10);
  ModuleRegistry again;
  CommandBus bus;
  ParameterRegistry p2;
  p2.defineAll(schema::parameters());
  EngineContext c2{p2, events, again, &bus, nullptr};
  again.add(std::make_unique<DmxModule>(dir.string()));
  ASSERT_EQ(again.startAll(c2), "");
  EXPECT_EQ(bus.run("dmx.get")["fixtures"].size(), 1u);
  bool found = false;
  for (const json& t : bus.run("dmx.types")) found = found || t["name"] == "Par";
  EXPECT_TRUE(found);
  again.stopAll();
  EXPECT_THROW(commands.run("dmx.type.save", {{"name", "../x"}, {"channels", {"r"}}}), std::runtime_error);
}

TEST(Recovery, DirtyProjectSurvivesAKilledEngineAndCleanExitLeavesNothing) {
  const auto dir = fs::temp_directory_path() / ("vj_rcv_" + std::to_string(getpid()));
  fs::remove_all(dir);
  fs::create_directories(dir);
  const auto backup = dir / "copia.vjcomp", copy = dir / ".recovery" / "recovery.vjcomp";
  struct App {
    ParameterRegistry params;
    EventBus events;
    ModuleRegistry modules;
    CommandBus commands;
  };
  const auto boot = [&](App& a, EngineContext& c) {
    a.params.defineAll(schema::parameters());
    a.modules.add(std::make_unique<compositor::CompositionModule>(compositor::CompositionShape{2, 2, 1}));
    a.modules.add(std::make_unique<project::ProjectModule>(dir.string()));
    ASSERT_EQ(a.modules.startAll(c), "");
  };
  {
    App a;
    EngineContext c{a.params, a.events, a.modules, &a.commands, nullptr};
    boot(a, c);
    a.modules.tickAll(0);
    a.commands.run("deck.add");
    a.modules.tickAll(6);
    ASSERT_TRUE(fs::exists(copy));
    fs::copy_file(copy, backup);  // el proceso muerto no llega a borrar la copia
    a.modules.stopAll();
    EXPECT_FALSE(fs::exists(copy)) << "la salida limpia no deja copia";
  }
  fs::create_directories(copy.parent_path());
  fs::copy_file(backup, copy);
  App a;
  EngineContext c{a.params, a.events, a.modules, &a.commands, nullptr};
  boot(a, c);
  a.modules.tickAll(0);
  EXPECT_TRUE(a.params.getBool("/project/recovered"));
  EXPECT_TRUE(a.params.getBool("/project/dirty"));
  EXPECT_EQ(json::parse(a.params.getString("/project/decks")).size(), 2u);
  a.modules.stopAll();
  fs::remove_all(dir);
}

TEST(Recovery, DamagedCopyStillBoots) {
  const auto dir = fs::temp_directory_path() / ("vj_rcd_" + std::to_string(getpid()));
  fs::remove_all(dir);
  fs::create_directories(dir / ".recovery");
  std::ofstream(dir / ".recovery" / "recovery.vjcomp") << "{roto";
  ParameterRegistry params;
  EventBus events;
  ModuleRegistry modules;
  CommandBus commands;
  params.defineAll(schema::parameters());
  EngineContext c{params, events, modules, &commands, nullptr};
  modules.add(std::make_unique<compositor::CompositionModule>(compositor::CompositionShape{2, 2, 1}));
  modules.add(std::make_unique<project::ProjectModule>(dir.string()));
  ASSERT_EQ(modules.startAll(c), "");
  modules.tickAll(0);
  EXPECT_FALSE(params.getBool("/project/recovered"));
  modules.stopAll();
  fs::remove_all(dir);
}
