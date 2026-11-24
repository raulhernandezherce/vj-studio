#include <gtest/gtest.h>

#include <vector>

#include "compositor/ClipClock.h"

using namespace vj::compositor;

namespace {
ClockConfig cfg(PlayMode m = PlayMode::Loop, Direction d = Direction::Forward) {
  ClockConfig c;
  c.length = 10;
  c.mode = m;
  c.direction = d;
  return c;
}
}  // namespace

TEST(ClipClock, ForwardLoopWraps) {
  ClipClock k;
  auto c = cfg();
  k.restart(c);
  EXPECT_FALSE(k.advance(9, c).looped);
  auto s = k.advance(2, c);
  EXPECT_TRUE(s.looped);
  EXPECT_NEAR(k.position(), 1, 1e-9);
}

TEST(ClipClock, BackwardStartsAtEndAndWraps) {
  ClipClock k;
  auto c = cfg(PlayMode::Loop, Direction::Backward);
  k.restart(c);
  EXPECT_NEAR(k.position(), 10, 1e-9);
  k.advance(3, c);
  EXPECT_NEAR(k.position(), 7, 1e-9);
  EXPECT_TRUE(k.advance(8, c).looped);
  EXPECT_NEAR(k.position(), 9, 1e-9);
}

TEST(ClipClock, InOutBoundLoop) {
  ClipClock k;
  auto c = cfg();
  c.in = 0.2;
  c.out = 0.4;
  k.restart(c);
  EXPECT_NEAR(k.position(), 2, 1e-9);
  EXPECT_TRUE(k.advance(2.5, c).looped);
  EXPECT_NEAR(k.position(), 2.5, 1e-9);
}

TEST(ClipClock, PingPongBouncesAndCountsFullTrip) {
  ClipClock k;
  auto c = cfg(PlayMode::PingPong);
  k.restart(c);
  EXPECT_FALSE(k.advance(11, c).looped);
  EXPECT_NEAR(k.position(), 9, 1e-9);
  EXPECT_FALSE(k.advance(5, c).looped);
  EXPECT_NEAR(k.position(), 4, 1e-9);
  EXPECT_TRUE(k.advance(5, c).looped);
  EXPECT_NEAR(k.position(), 1, 1e-9);
}

TEST(ClipClock, OnceEjectFinishesOnce) {
  ClipClock k;
  auto c = cfg(PlayMode::OnceEject);
  k.restart(c);
  EXPECT_FALSE(k.advance(9, c).finished);
  EXPECT_TRUE(k.advance(2, c).finished);
  EXPECT_FALSE(k.advance(2, c).finished);
}

TEST(ClipClock, OnceHoldStaysOnLastFrame) {
  ClipClock k;
  auto c = cfg(PlayMode::OnceHold);
  k.restart(c);
  k.advance(15, c);
  EXPECT_NEAR(k.position(), 10, 1e-9);
  k.advance(1, c);
  EXPECT_NEAR(k.position(), 10, 1e-9);
}

TEST(ClipClock, RandomJumpsInsideRangeAtInterval) {
  ClipClock k(7);
  auto c = cfg(PlayMode::Random);
  c.in = 0.1;
  c.out = 0.9;
  c.randomInterval = 0.5;
  k.restart(c);
  std::vector<double> seen;
  for (int i = 0; i < 40; ++i) {
    k.advance(0.25, c);
    EXPECT_GE(k.position(), 1);
    EXPECT_LE(k.position(), 9);
    seen.push_back(k.position());
  }
  EXPECT_EQ(seen[0], 1) << "no salta antes del intervalo";
  EXPECT_NE(seen[1], 1);
}

TEST(ClipClock, PauseHolds) {
  ClipClock k;
  auto c = cfg();
  k.restart(c);
  k.advance(1, c);
  c.direction = Direction::Pause;
  k.advance(5, c);
  EXPECT_NEAR(k.position(), 1, 1e-9);
}

TEST(ClipClock, RateScales) {
  ClipClock k;
  auto c = cfg();
  c.rate = 2.5;
  k.restart(c);
  k.advance(2, c);
  EXPECT_NEAR(k.position(), 5, 1e-9);
}
