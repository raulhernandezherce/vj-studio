#include <gtest/gtest.h>

#include <cmath>

#include "output/ScreenSetup.h"

using namespace vj::output;
using nlohmann::json;

TEST(Perspective, MapsCornersExactly) {
  const std::array<Point, 4> q = {Point{0.1, 0.2}, Point{0.9, 0.1}, Point{0.8, 0.9}, Point{0.2, 0.7}};
  const auto h = perspective(q);
  const Point units[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  for (int i = 0; i < 4; ++i) {
    const Point p = applyPerspective(h, units[i]);
    EXPECT_NEAR(p.x, q[static_cast<size_t>(i)].x, 1e-9);
    EXPECT_NEAR(p.y, q[static_cast<size_t>(i)].y, 1e-9);
  }
}

TEST(Perspective, IdentityAndPerspectiveKeepsLinesStraight) {
  const auto id = perspective({Point{0, 0}, Point{1, 0}, Point{1, 1}, Point{0, 1}});
  EXPECT_NEAR(applyPerspective(id, {0.3, 0.6}).x, 0.3, 1e-12);
  // El centro de un trapecio no es el promedio de las esquinas: es el cruce de las diagonales.
  const std::array<Point, 4> q = {Point{0.3, 0}, Point{0.7, 0}, Point{1, 1}, Point{0, 1}};
  const Point c = applyPerspective(perspective(q), {0.5, 0.5});
  EXPECT_NEAR(c.x, 0.5, 1e-9);
  EXPECT_GT(c.y, 0.5 - 0.3) ;
  EXPECT_LT(c.y, 0.5);
}

TEST(Warp, IdentityGridLeavesPointsAndBendMoves) {
  Slice s;
  s.warpColumns = s.warpRows = 3;
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c) s.warp.push_back({c / 2.0, r / 2.0});
  for (const char* mode : {"linear", "bezier"}) {
    s.warpMode = mode;
    const Point p = evalWarp(s, {0.3, 0.7});
    EXPECT_NEAR(p.x, 0.3, 1e-9) << mode;
    EXPECT_NEAR(p.y, 0.7, 1e-9) << mode;
  }
  s.warpMode = "linear";
  s.warp[4] = {0.5, 0.7};  // centro hacia abajo
  EXPECT_NEAR(evalWarp(s, {0.5, 0.5}).y, 0.7, 1e-9);
  EXPECT_NEAR(evalWarp(s, {0.5, 0.25}).y, 0.35, 1e-9);
  EXPECT_NEAR(evalWarp(s, {0, 0}).y, 0, 1e-9);
}

TEST(Polygon, TriangulatesConcaveShapesWithFullArea) {
  const std::vector<Point> hexagon = {{0.25, 0}, {0.75, 0}, {1, 0.5}, {0.75, 1}, {0.25, 1}, {0, 0.5}};
  auto tri = triangulate(hexagon);
  EXPECT_EQ(tri.size(), 12u);
  const auto area = [](const std::vector<Point>& t) {
    double a = 0;
    for (size_t i = 0; i + 2 < t.size(); i += 3) a += std::abs((t[i + 1].x - t[i].x) * (t[i + 2].y - t[i].y) - (t[i + 1].y - t[i].y) * (t[i + 2].x - t[i].x)) / 2;
    return a;
  };
  EXPECT_NEAR(area(tri), 0.75, 1e-9);
  const std::vector<Point> arrow = {{0, 0}, {1, 0.5}, {0, 1}, {0.3, 0.5}};  // cóncava
  EXPECT_NEAR(area(triangulate(arrow)), 0.35, 1e-9);
  std::vector<Point> reversed(hexagon.rbegin(), hexagon.rend());
  EXPECT_NEAR(area(triangulate(reversed)), 0.75, 1e-9);
}

TEST(Polygon, RejectsSelfCrossing) {
  const std::vector<Point> bow = {{0, 0}, {1, 1}, {1, 0}, {0, 1}};
  EXPECT_TRUE(selfIntersects(bow));
  EXPECT_TRUE(triangulate(bow).empty());
  EXPECT_TRUE(triangulate({{0, 0}, {1, 1}}).empty());
}

TEST(EdgeBlend, ComplementaryCurvesSumToOne) {
  for (double x = 0; x <= 1; x += 0.05) EXPECT_NEAR(blendCurve(x, 2, 0.5, 1) + blendCurve(1 - x, 2, 0.5, 1), 1, 1e-9);
  EXPECT_DOUBLE_EQ(blendCurve(0, 2, 0.5, 1.8), 0);
  EXPECT_DOUBLE_EQ(blendCurve(1, 2, 0.5, 1.8), 1);
}

TEST(SetupJson, RoundTripAndPortable) {
  ScreenSetup s = defaultSetup();
  s.screens[0].device = "display:1";
  s.screens[0].slices[0].inX = 0.25;
  s.screens[0].slices[0].polygon = {{0, 0}, {1, 0}, {0.5, 1}};
  s.screens[0].masks.push_back({{{0, 0}, {1, 0}, {0, 1}}, false});
  const ScreenSetup back = fromJson(toJson(s));
  EXPECT_EQ(toJson(back), toJson(s));
}

TEST(SetupJson, SameDeviceTwiceFallsBackToVirtual) {
  json j = toJson(defaultSetup());
  j["screens"][0]["device"] = "display:0";
  json second = j["screens"][0];
  second["id"] = 2;
  j["screens"].push_back(second);
  const auto s = fromJson(j);
  EXPECT_EQ(s.screens[0].device, "display:0");
  EXPECT_EQ(s.screens[1].device, "virtual");
}

TEST(SetupJson, RepairsAndRejects) {
  json j = toJson(defaultSetup());
  j["screens"][0]["slices"] = json::array();
  EXPECT_EQ(fromJson(j).screens[0].slices.size(), 1u) << "una pantalla siempre tiene al menos un slice";
  EXPECT_THROW(fromJson({{"format", "otra"}}), std::runtime_error);
  json bad = toJson(defaultSetup());
  bad["screens"][0]["slices"][0]["polygon"] = {{0, 0}, {1, 1}, {1, 0}, {0, 1}};
  EXPECT_THROW(fromJson(bad), std::runtime_error);
  bad = toJson(defaultSetup());
  bad["screens"].push_back(bad["screens"][0]);
  EXPECT_THROW(fromJson(bad), std::runtime_error) << "ids repetidos";
  bad = toJson(defaultSetup());
  bad["screens"][0]["slices"][0]["sourceType"] = "camara";
  EXPECT_THROW(fromJson(bad), std::runtime_error);
}

TEST(MeshBuild, RectangleCoversTheSliceAndFlipsTexture) {
  Slice s;
  const Mesh m = buildMesh(s);
  ASSERT_FALSE(m.vertices.empty());
  float minX = 9, maxX = -9;
  for (const auto& v : m.vertices) {
    minX = std::min(minX, v.x);
    maxX = std::max(maxX, v.x);
  }
  EXPECT_FLOAT_EQ(minX, -1);
  EXPECT_FLOAT_EQ(maxX, 1);
  s.flipH = true;
  const Mesh f = buildMesh(s);
  EXPECT_FLOAT_EQ(f.vertices[0].u, 1 - m.vertices[0].u);
}
