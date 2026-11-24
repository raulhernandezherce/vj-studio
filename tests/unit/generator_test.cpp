#include <gtest/gtest.h>

#include "media/IMediaSource.h"

using namespace vj::media;

TEST(Generators, PatternDeterministicAndAnimated) {
  auto src = makeGenerator("vj://pattern", 320, 180);
  auto a = src->frameAt(0.5), b = src->frameAt(0.5), c = src->frameAt(1.0);
  EXPECT_EQ(a->pixels, b->pixels);
  EXPECT_NE(a->pixels, c->pixels);
  EXPECT_EQ(a->pixels.size(), size_t{320 * 180 * 4});
}

TEST(Generators, SolidReadsHex) {
  std::string err;
  auto f = openMedia("vj://solid/102030", err)->frameAt(0);
  EXPECT_EQ(f->pixels[0], 0x10);
  EXPECT_EQ(f->pixels[2], 0x30);
  EXPECT_EQ(f->pixels[3], 255);
  EXPECT_EQ(openMedia("vj://solid/10203040", err)->frameAt(0)->pixels[3], 0x40);
}

TEST(Generators, UnknownFails) {
  std::string err;
  EXPECT_FALSE(openMedia("vj://nada", err));
  EXPECT_FALSE(err.empty());
}
