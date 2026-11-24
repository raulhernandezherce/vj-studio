#include <gtest/gtest.h>
#include <CoreMIDI/CoreMIDI.h>
#include <mach/mach_time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <chrono>
#include <filesystem>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>

#include "compositor/CompositionModule.h"
#include "core/CommandBus.h"
#include "core/ModuleRegistry.h"
#include "core/generated/Parameters.gen.h"
#include "shortcuts/ArtNet.h"
#include "shortcuts/MidiClock.h"
#include "shortcuts/ShortcutModule.h"

using namespace vj;
using namespace vj::shortcuts;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {
ControlEvent note(const std::string& k, bool down, double v = 1) {
  ControlEvent e;
  e.key = k;
  e.button = true;
  e.down = down;
  e.value = v;
  return e;
}
ControlEvent fader(const std::string& k, double v) {
  ControlEvent e;
  e.key = k;
  e.value = v;
  return e;
}
std::string artDmx(int universe, const std::vector<uint8_t>& ch) {
  std::string p("Art-Net\0", 8);
  p += {'\0', 0x50, 0, 14, 0, 0};
  p.push_back(static_cast<char>(universe & 0xFF));
  p.push_back(static_cast<char>(universe >> 8));
  p.push_back(static_cast<char>(ch.size() >> 8));
  p.push_back(static_cast<char>(ch.size() & 0xFF));
  p.append(ch.begin(), ch.end());
  return p;
}
}  // namespace

class Shortcuts : public ::testing::Test {
 protected:
  void SetUp() override {
    dir = fs::temp_directory_path() / ("vj_sc_" + std::to_string(getpid()) + "_" + std::to_string(counter++));
    params.defineAll(schema::parameters());
    params.instantiate("/composition/layers/1", schema::layerParameters());
    params.instantiate("/composition/layers/2", schema::layerParameters());
    params.instantiate("/composition/layers/1/clips/1", schema::clipParameters());
    e = std::make_unique<ShortcutEngine>(params);
  }
  void TearDown() override { fs::remove_all(dir); }
  static inline int counter = 0;
  fs::path dir;
  ParameterRegistry params;
  std::unique_ptr<ShortcutEngine> e;
};

TEST_F(Shortcuts, LearnAssignsNextGesture) {
  e->learn("/composition/bypass");
  e->handle(note("key:b", true));
  e->handle(note("key:b", false));
  ASSERT_EQ(e->list().size(), 1u);
  EXPECT_EQ(e->list()[0]["mode"], "toggle");
  EXPECT_FALSE(params.getBool("/composition/bypass")) << "aprender no ejecuta";
  e->handle(note("key:b", true));
  EXPECT_TRUE(params.getBool("/composition/bypass"));
  e->handle(note("key:b", true));
  EXPECT_FALSE(params.getBool("/composition/bypass"));
}

TEST_F(Shortcuts, Modes) {
  e->add({{"key", "a"}, {"target", "/composition/bypass"}, {"mode", "piano"}});
  e->handle(note("a", true));
  EXPECT_TRUE(params.getBool("/composition/bypass"));
  e->handle(note("a", false));
  EXPECT_FALSE(params.getBool("/composition/bypass"));

  e->add({{"key", "b"}, {"target", "/composition/master"}, {"mode", "value"}, {"value", 0.3}});
  e->handle(note("b", true));
  EXPECT_DOUBLE_EQ(params.getFloat("/composition/master"), 0.3);

  e->add({{"key", "c"}, {"target", "/composition/master"}, {"mode", "range"}, {"min", 0.2}, {"max", 0.8}});
  e->handle(note("c", true));
  EXPECT_DOUBLE_EQ(params.getFloat("/composition/master"), 0.8);
  e->handle(note("c", false));
  EXPECT_DOUBLE_EQ(params.getFloat("/composition/master"), 0.2);

  e->add({{"key", "d"}, {"target", "/composition/master"}, {"mode", "velocity"}});
  e->handle(note("d", true, 0.5));
  EXPECT_DOUBLE_EQ(params.getFloat("/composition/master"), 0.5);

  e->add({{"key", "f"}, {"target", "/composition/master"}, {"mode", "absolute"}, {"invert", true}});
  e->handle(fader("f", 0.25));
  EXPECT_DOUBLE_EQ(params.getFloat("/composition/master"), 0.75);
}

TEST_F(Shortcuts, EnumCyclesAndRelativeEncoder) {
  e->add({{"key", "n"}, {"target", "/composition/layers/1/blend"}, {"mode", "next"}});
  e->handle(note("n", true));
  EXPECT_EQ(params.getString("/composition/layers/1/blend"), "Alpha");
  e->add({{"key", "r"}, {"target", "/composition/master"}, {"mode", "relative"}, {"steps", 10.0}});
  ControlEvent t = fader("r", 0);
  t.relative = -2;
  e->handle(t);
  EXPECT_DOUBLE_EQ(params.getFloat("/composition/master"), 0.8);
}

TEST_F(Shortcuts, SelectedTargetFollowsActiveLayer) {
  e->add({{"key", "s"}, {"target", "/composition/selectedlayer/video/opacity"}, {"mode", "absolute"}});
  params.set("/composition/activelayer", int64_t{2});
  e->handle(fader("s", 0.4));
  EXPECT_DOUBLE_EQ(params.getFloat("/composition/layers/2/opacity"), 0.4);
  EXPECT_DOUBLE_EQ(params.getFloat("/composition/layers/1/opacity"), 1);
}

TEST_F(Shortcuts, NoteFiresClipAndFeedbackFollowsState) {
  int pressed = 0, released = 0;
  const std::string c = "/composition/layers/1/clips/1";
  params.addListener(c + "/connect", [&](const ParamDesc&, const Value&) { ++pressed; });
  params.addListener(c + "/release", [&](const ParamDesc&, const Value&) { ++released; });
  e->add({{"key", "midi:x:note:1:60"}, {"target", c + "/connect"}, {"mode", "trigger"}});
  e->handle(note("midi:x:note:1:60", true));
  e->handle(note("midi:x:note:1:60", false));
  EXPECT_EQ(pressed, 1);
  EXPECT_EQ(released, 1);
  const auto f = e->feedback(c + "/connected", Value{true});
  ASSERT_EQ(f.size(), 1u);
  EXPECT_EQ(f[0].first, "midi:x:note:1:60");
  EXPECT_EQ(f[0].second, 1);
}

TEST_F(Shortcuts, UnknownTargetRefused) {
  EXPECT_THROW(e->learn("/no/existe"), std::runtime_error);
}

TEST(MidiClockTest, EstimatesBpmUnderJitterWithoutDrift) {
  MidiClock c;
  c.start(0);
  const double dt = 60.0 / 120 / 24;
  uint32_t r = 1;
  double t = 0;
  const long ticks = 24 * 2 * 600;  // 10 minutos a 120 BPM
  for (long i = 0; i < ticks; ++i) {
    r = r * 1664525u + 1013904223u;
    const double jitter = ((r >> 8) / 16777216.0 - 0.5) * 0.002;  // ±1 ms
    t = (static_cast<double>(i) + 1) * dt;
    c.tick(t + jitter);
    if (i > 200) {
      ASSERT_NEAR(c.bpm(), 120, 1.0);
    }
  }
  EXPECT_NEAR(c.beat(t), 1200, 1.0);
}

TEST(MidiClockTest, StartResetsBeat) {
  MidiClock c;
  c.start(0);
  for (int i = 1; i <= 48; ++i) c.tick(i * 0.02);
  EXPECT_NEAR(c.beat(0.96), 2, 0.01);
  c.start(1);
  EXPECT_NEAR(c.beat(1), 0, 0.01);
}

TEST(ArtDmxParse, ReadsUniverseAndChannels) {
  const auto p = artDmx(3, {10, 20});
  const auto d = parseArtDmx(reinterpret_cast<const uint8_t*>(p.data()), p.size());
  ASSERT_TRUE(d);
  EXPECT_EQ(d->universe, 3);
  EXPECT_EQ(d->channels[1], 20);
  EXPECT_FALSE(parseArtDmx(reinterpret_cast<const uint8_t*>(p.data()), 10));
  std::string bad = p;
  bad[0] = 'X';
  EXPECT_FALSE(parseArtDmx(reinterpret_cast<const uint8_t*>(bad.data()), bad.size()));
}

class ShortcutDevices : public ::testing::Test {
 protected:
  void SetUp() override {
    dir = fs::temp_directory_path() / ("vj_scd_" + std::to_string(getpid()));
    fs::remove_all(dir);
    params.defineAll(schema::parameters());
    modules.add(std::make_unique<compositor::CompositionModule>(compositor::CompositionShape{2, 2, 1}));
    modules.add(std::make_unique<ShortcutModule>(dir.string()));
    ASSERT_EQ(modules.startAll(ctx), "");
  }
  void TearDown() override {
    modules.stopAll();
    fs::remove_all(dir);
  }
  void pump(int ms) {
    for (int i = 0; i < ms / 10; ++i) {
      modules.tickAll(0.01);
      usleep(10000);
    }
  }
  fs::path dir;
  ParameterRegistry params;
  EventBus events;
  ModuleRegistry modules;
  CommandBus commands;
  EngineContext ctx{params, events, modules, &commands, nullptr};
};

TEST_F(ShortcutDevices, ArtNetChannelMovesLayerOpacity) {
  params.set("/preferences/dmx/port", int64_t{16454});
  params.set("/preferences/dmx/input", true);
  commands.run("shortcuts.add", {{"key", "dmx:0:5"}, {"target", "/composition/layers/1/opacity"}, {"mode", "absolute"}});
  pump(50);
  const int fd = socket(AF_INET, SOCK_DGRAM, 0);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(16454);
  inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
  const auto p = artDmx(0, {0, 0, 0, 0, 51});
  sendto(fd, p.data(), p.size(), 0, reinterpret_cast<sockaddr*>(&a), sizeof a);
  close(fd);
  pump(100);
  EXPECT_NEAR(params.getFloat("/composition/layers/1/opacity"), 0.2, 1e-6);
}

TEST_F(ShortcutDevices, VirtualMidiNoteAndClockThroughCoreMidi) {
  MIDIClientRef client = 0;
  ASSERT_EQ(MIDIClientCreate(CFSTR("prueba"), nullptr, nullptr, &client), noErr);
  MIDIEndpointRef src = 0;
  ASSERT_EQ(MIDISourceCreate(client, CFSTR("VJ Prueba"), &src), noErr);
  const auto post = [&](std::vector<Byte> b) {
    Byte buf[64];
    auto* l = reinterpret_cast<MIDIPacketList*>(buf);
    MIDIPacket* p = MIDIPacketListInit(l);
    MIDIPacketListAdd(l, sizeof buf, p, mach_absolute_time(), b.size(), b.data());
    MIDIReceived(src, l);
  };
  modules.tickAll(3);  // fuerza la búsqueda de dispositivos
  pump(100);
  commands.run("shortcuts.add", {{"key", "midi:VJ Prueba:note:1:60"}, {"target", "/composition/bypass"}, {"mode", "toggle"}});
  post({0x90, 60, 100});
  pump(100);
  EXPECT_TRUE(params.getBool("/composition/bypass"));
  EXPECT_FALSE(commands.run("midi.monitor").empty());

  params.set("/preferences/midi/clockin", true);
  post({0xFA});
  const auto begin = std::chrono::steady_clock::now();
  for (int i = 0; i < 120; ++i) {
    post({0xF8});
    modules.tickAll(0);
    usleep(20000);
  }
  const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
  const double sent = 60.0 * 120 / 24 / seconds;  // el ritmo que de verdad salió del bucle
  pump(20);
  EXPECT_NEAR(params.getFloat("/composition/tempo/bpm"), sent, sent * 0.1);
  MIDIEndpointDispose(src);
  MIDIClientDispose(client);
}

TEST_F(ShortcutDevices, PresetExportsAndImports) {
  commands.run("shortcuts.preset.new", {{"name", "APC"}});
  commands.run("shortcuts.add", {{"key", "key:q"}, {"target", "/composition/bypass"}, {"mode", "toggle"}});
  const auto out = dir / "fuera.json";
  commands.run("shortcuts.preset.export", {{"path", out.string()}});
  commands.run("shortcuts.preset.new", {{"name", "Vacío"}});
  EXPECT_TRUE(commands.run("shortcuts.list").empty());
  const auto r = commands.run("shortcuts.preset.import", {{"path", out.string()}});
  EXPECT_EQ(commands.run("shortcuts.list").size(), 1u);
  EXPECT_EQ(r["name"], "fuera");
  EXPECT_THROW(commands.run("shortcuts.preset.import", {{"path", (dir / "no.json").string()}}), std::exception);
}

TEST_F(ShortcutDevices, KeyEventRunsAndReportsHandled) {
  commands.run("shortcuts.add", {{"key", "key:q"}, {"target", "/composition/bypass"}, {"mode", "toggle"}});
  EXPECT_TRUE(commands.run("shortcuts.key", {{"key", "q"}})["handled"]);
  EXPECT_TRUE(params.getBool("/composition/bypass"));
  EXPECT_FALSE(commands.run("shortcuts.key", {{"key", "z"}})["handled"]);
}
