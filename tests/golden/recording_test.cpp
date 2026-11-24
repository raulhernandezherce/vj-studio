#include <gtest/gtest.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <thread>

#include "compositor/CompositionModule.h"
#include "core/CommandBus.h"
#include "core/ModuleRegistry.h"
#include "core/generated/Parameters.gen.h"
#include "media/IMediaSource.h"
#include "output/OutputModule.h"

using namespace vj;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {
std::vector<uint8_t> solid(int w, int h, uint8_t r, uint8_t g, uint8_t b) {
  std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
  for (size_t i = 0; i < px.size(); i += 4) {
    px[i] = r;
    px[i + 1] = g;
    px[i + 2] = b;
    px[i + 3] = 255;
  }
  return px;
}

media::FramePtr readFrame(media::IMediaSource& s, double t) {
  for (int i = 0; i < 400; ++i) {
    auto f = s.frameAt(t);
    if (f && std::abs(f->time - t) < 0.1) return f;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return nullptr;
}

fs::path tempDir(const char* name) {
  auto d = fs::temp_directory_path() / (std::string(name) + std::to_string(getpid()));
  fs::remove_all(d);
  fs::create_directories(d);
  return d;
}
}  // namespace

class RecorderCodec : public ::testing::TestWithParam<const char*> {};

TEST_P(RecorderCodec, WrittenFileReadsBackWithItsColors) {
  const auto dir = tempDir("vj_rec_");
  const auto path = dir / (std::string(GetParam()) == "ProRes 422" ? "a.mov" : "a.mp4");
  auto rec = media::makeRecorder(path.string(), 320, 180, GetParam());
  for (int i = 0; i < 30; ++i) {
    rec->addFrame(i < 15 ? solid(320, 180, 255, 0, 0) : solid(320, 180, 0, 0, 255), i / 30.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  ASSERT_TRUE(rec->finish());
  EXPECT_EQ(rec->framesWritten() + rec->framesDropped(), 30);
  EXPECT_EQ(rec->framesDropped(), 0);
  std::string err;
  auto src = media::openMedia(path.string(), err);
  ASSERT_TRUE(src) << err;
  EXPECT_EQ(src->info().width, 320);
  const auto check = [&](double t, bool red) {
    auto f = readFrame(*src, t);
    ASSERT_TRUE(f);
    const uint8_t* p = f->pixels.data() + (f->height / 2) * f->stride + f->width * 2;
    EXPECT_GT(p[red ? 2 : 0], 200);
    EXPECT_LT(p[red ? 0 : 2], 60);
  };
  check(0.2, true);
  check(0.75, false);
  fs::remove_all(dir);
}

INSTANTIATE_TEST_SUITE_P(Codecs, RecorderCodec, ::testing::Values("H.264", "HEVC", "ProRes 422"));

TEST(Recorder, FullQueueDropsAndCountsInsteadOfBlocking) {
  const auto dir = tempDir("vj_recq_");
  auto rec = media::makeRecorder((dir / "a.mp4").string(), 1920, 1080, "H.264");
  const auto frame = solid(1920, 1080, 10, 20, 30);
  const auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < 200; ++i) rec->addFrame(frame, i / 30.0);
  EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(2));
  rec->finish();
  EXPECT_EQ(rec->framesWritten() + rec->framesDropped(), 200);
  fs::remove_all(dir);
}

TEST(Recorder, OddSizeAndBadPathFail) {
  EXPECT_THROW(media::makeRecorder("/tmp/x.mp4", 321, 180, "H.264"), std::runtime_error);
  EXPECT_THROW(media::makeRecorder("/no/existe/x.mp4", 320, 180, "H.264"), std::runtime_error);
}

TEST(Recorder, FrameOfAnotherSizeIsDroppedNotReadOutOfBounds) {
  const auto dir = tempDir("vj_recs_");
  auto rec = media::makeRecorder((dir / "a.mp4").string(), 320, 180, "H.264");
  rec->addFrame(solid(320, 180, 1, 2, 3), 0);
  rec->addFrame(solid(160, 90, 1, 2, 3), 0.1);
  EXPECT_TRUE(rec->finish());
  EXPECT_EQ(rec->framesWritten(), 1);
  EXPECT_EQ(rec->framesDropped(), 1);
  fs::remove_all(dir);
}

TEST(Cameras, ListingNeverAsksPermissionAndUnknownIdFails) {
  for (const auto& d : media::listCaptureDevices()) EXPECT_FALSE(d.id.empty());
  std::string err;
  EXPECT_FALSE(media::openMedia("vj://camera/no-existe", err));
  EXPECT_FALSE(err.empty());
}

class Recording : public ::testing::Test {
 protected:
  void SetUp() override {
    dir = tempDir("vj_recm_");
    params.defineAll(schema::parameters());
    params.set("/composition/width", int64_t{320});
    params.set("/composition/height", int64_t{180});
    modules.add(std::make_unique<compositor::CompositionModule>(compositor::CompositionShape{2, 2, 1}));
    output::OutputOptions o;
    o.hidden = true;
    o.documents = dir.string();
    modules.add(std::make_unique<output::OutputModule>(o));
    if (modules.startAll(ctx) != "") GTEST_SKIP() << "sin OpenGL";
    comp = modules.get<compositor::CompositionModule>("composition")->composition();
    params.set("/composition/layers/1/blend", std::string("Alpha"));
  }
  void TearDown() override {
    modules.stopAll();
    fs::remove_all(dir);
  }
  void clip(const std::string& path) {
    params.set("/composition/layers/1/clips/1/path", path);
    for (int i = 0; i < 4; ++i) {
      comp->tick(0);
      comp->waitForLoads();
    }
    comp->tick(0);
  }
  void tickUntil(const std::string& status) {
    for (int i = 0; i < 2000 && params.getString("/record/status") != status; ++i) {
      modules.tickAll(0);
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_EQ(params.getString("/record/status"), status);
  }
  fs::path dir;
  ParameterRegistry params;
  EventBus events;
  ModuleRegistry modules;
  CommandBus commands;
  EngineContext ctx{params, events, modules, &commands, nullptr};
  compositor::Composition* comp = nullptr;
};

TEST_F(Recording, RecordButtonWritesCompositionToRecordedFolder) {
  clip("vj://solid/00FF00");
  params.trigger("/composition/layers/1/clips/1/connect");
  modules.tickAll(0);
  params.set("/record/active", true);
  tickUntil("grabando");
  for (int i = 0; i < 60; ++i) {
    modules.tickAll(1.0 / 60);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  EXPECT_EQ(params.getString("/record/status"), "grabando");
  params.set("/record/active", false);
  modules.tickAll(1.0 / 60);
  EXPECT_EQ(params.getString("/record/status"), "cerrando") << "el cierre no bloquea la salida";
  tickUntil("parado");
  EXPECT_EQ(params.getInt("/record/dropped"), 0);
  EXPECT_GE(params.getInt("/record/frames"), 28);
  const std::string file = params.getString("/record/file");
  EXPECT_EQ(fs::path(file).parent_path(), dir / "Recorded");
  std::string err;
  auto src = media::openMedia(file, err);
  ASSERT_TRUE(src) << err;
  EXPECT_NEAR(src->info().duration, 1.0, 0.1);
  auto f = readFrame(*src, 0.5);
  ASSERT_TRUE(f);
  EXPECT_GT(f->pixels[static_cast<size_t>(f->height / 2) * f->stride + 1], 200);
}

TEST_F(Recording, UnwritableFolderReportsErrorAndClearsButton) {
  params.set("/preferences/recordfolder", std::string("/no/existe"));
  params.set("/record/active", true);
  for (int i = 0; i < 400 && params.getBool("/record/active"); ++i) {
    modules.tickAll(0);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  EXPECT_FALSE(params.getBool("/record/active"));
  EXPECT_EQ(params.getString("/record/status").rfind("error", 0), 0u);
}

TEST_F(Recording, ClipRenderIncludesEffectsAndKeepsOutputAlive) {
  clip("vj://solid/FF0000");
  comp->effects().add("/composition/layers/1/clips/1", "invert");
  params.set("/composition/layers/1/clips/1/transport/duration", 1.0);
  const auto job = commands.run("clip.render", {{"layer", 1}, {"column", 1}, {"fps", 30}});
  bool rendered = false;
  events.subscribe("clip.rendered", [&](const std::string&, const json& d) { rendered = d["ok"]; });
  for (int i = 0; i < 2000 && !rendered; ++i) {
    modules.tickAll(0.01);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  ASSERT_TRUE(rendered);
  std::string err;
  auto src = media::openMedia(job["path"], err);
  ASSERT_TRUE(src) << err;
  auto f = readFrame(*src, 1.0);
  ASSERT_TRUE(f);
  const uint8_t* p = f->pixels.data() + (f->height / 2) * f->stride + f->width * 2;
  EXPECT_GT(p[0], 200) << "cian: el efecto está en el archivo";
  EXPECT_LT(p[2], 60);
  EXPECT_THROW({ (void)commands.run("clip.render", {{"layer", 1}, {"column", 2}}); }, std::runtime_error);
}
