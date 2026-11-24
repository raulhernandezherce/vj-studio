#include <gtest/gtest.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <thread>

#include "media/IMediaSource.h"

bool writeFixtureVideo(const std::string& path);
bool writeFixturePng(const std::string& path);

using namespace vj;
using namespace vj::media;

namespace {

const std::string& fixture() {
  static const std::string path = [] {
    auto p = std::filesystem::temp_directory_path() / ("vj_fixture_" + std::to_string(getpid()) + ".mp4");
    EXPECT_TRUE(writeFixtureVideo(p.string()));
    return p.string();
  }();
  return path;
}

FramePtr waitFrame(IMediaSource& s, double t) {
  for (int i = 0; i < 400; ++i) {
    FramePtr f = s.frameAt(t);
    if (f && std::abs(f->time - t) < 0.1) return f;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return nullptr;
}

bool isRed(const VideoFrame& f) {
  const uint8_t* p = f.pixels.data() + (f.height / 2) * f.stride + f.width * 2;
  return p[2] > 200 && p[0] < 60;
}
bool isBlue(const VideoFrame& f) {
  const uint8_t* p = f.pixels.data() + (f.height / 2) * f.stride + f.width * 2;
  return p[0] > 200 && p[2] < 60;
}

}  // namespace

TEST(AvfMedia, ReportsInfo) {
  std::string err;
  auto s = openMedia(fixture(), err);
  ASSERT_TRUE(s) << err;
  EXPECT_NEAR(s->info().duration, 2.0, 0.05);
  EXPECT_EQ(s->info().width, 320);
  EXPECT_EQ(s->info().height, 180);
}

TEST(AvfMedia, DecodesRealPixels) {
  std::string err;
  auto s = openMedia(fixture(), err);
  ASSERT_TRUE(s);
  auto f = waitFrame(*s, 0.1);
  ASSERT_TRUE(f);
  EXPECT_TRUE(isRed(*f));
}

TEST(AvfMedia, JumpsBothWays) {
  std::string err;
  auto s = openMedia(fixture(), err);
  ASSERT_TRUE(s);
  auto f = waitFrame(*s, 1.5);
  ASSERT_TRUE(f);
  EXPECT_TRUE(isBlue(*f));
  f = waitFrame(*s, 0.2);
  ASSERT_TRUE(f);
  EXPECT_TRUE(isRed(*f));
}

TEST(AvfMedia, PlaysBackwardFrameByFrame) {
  std::string err;
  auto s = openMedia(fixture(), err);
  ASSERT_TRUE(s);
  int blue = 0, red = 0;
  for (double t = 1.9; t > 0; t -= 1.0 / 30) {
    FramePtr f;
    for (int i = 0; i < 400 && !(f && std::abs(f->time - t) <= 0.04); ++i) {
      f = s->frameAt(t);
      if (!(f && std::abs(f->time - t) <= 0.04)) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    ASSERT_TRUE(f && std::abs(f->time - t) <= 0.04) << t;
    if (isBlue(*f)) ++blue;
    if (isRed(*f)) ++red;
  }
  EXPECT_GT(blue, 20);
  EXPECT_GT(red, 20);
}

TEST(AvfMedia, ReturnsToStartAfterEnd) {
  std::string err;
  auto s = openMedia(fixture(), err);
  ASSERT_TRUE(s);
  ASSERT_TRUE(waitFrame(*s, 1.95));
  auto f = waitFrame(*s, 0.05);
  ASSERT_TRUE(f);
  EXPECT_TRUE(isRed(*f));
}

TEST(AvfMedia, ReleaseDropsCacheAndStillPlaysAfterwards) {
  std::string err;
  auto s = openMedia(fixture(), err);
  ASSERT_TRUE(s);
  ASSERT_TRUE(waitFrame(*s, 1.0));
  s->release();
  auto f = waitFrame(*s, 1.5);
  ASSERT_TRUE(f);
  EXPECT_TRUE(isBlue(*f));
}

TEST(AvfMedia, MissingAndNonVideoFilesFail) {
  std::string err;
  EXPECT_FALSE(openMedia("/no/existe.mp4", err));
  EXPECT_FALSE(err.empty());
  const auto txt = std::filesystem::temp_directory_path() / "vj_not_video.txt";
  FILE* f = std::fopen(txt.c_str(), "w");
  std::fputs("hola", f);
  std::fclose(f);
  err.clear();
  EXPECT_FALSE(openMedia(txt.string(), err));
  EXPECT_FALSE(err.empty());
}

TEST(Stills, PngKeepsStraightAlpha) {
  const auto png = std::filesystem::temp_directory_path() / ("vj_fixture_" + std::to_string(getpid()) + ".png");
  ASSERT_TRUE(writeFixturePng(png.string()));
  std::string err;
  auto s = openMedia(png.string(), err);
  ASSERT_TRUE(s) << err;
  EXPECT_TRUE(s->info().straightAlpha);
  auto f = s->frameAt(0);
  EXPECT_EQ(f->width, 4);
  EXPECT_EQ(f->format, gfx::PixelFormat::RGBA8);
  EXPECT_GT(f->pixels[0], 250);
  EXPECT_EQ(f->pixels[3], 128);
}
