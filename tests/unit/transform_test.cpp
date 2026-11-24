#include <gtest/gtest.h>

#include "compositor/Transform.h"

using namespace vj::compositor;

namespace {
// Aplica la matriz a un vértice del cuadro y devuelve píxeles desde el centro, Y abajo.
struct P { double x, y; };
P apply(const vj::gfx::Mat3& m, double qx, double qy, double W, double H) {
  return {(m[0] * qx + m[3] * qy + m[6]) * W / 2, -(m[1] * qx + m[4] * qy + m[7]) * H / 2};
}
}  // namespace

TEST(Resize, FourModesWith4by3In16by9) {
  double w, h;
  resizedSize(Resize::Fit, 400, 300, 1600, 900, w, h);
  EXPECT_DOUBLE_EQ(w, 1200);
  EXPECT_DOUBLE_EQ(h, 900);
  resizedSize(Resize::Fill, 400, 300, 1600, 900, w, h);
  EXPECT_DOUBLE_EQ(w, 1600);
  EXPECT_DOUBLE_EQ(h, 1200);
  resizedSize(Resize::Stretch, 400, 300, 1600, 900, w, h);
  EXPECT_DOUBLE_EQ(w, 1600);
  EXPECT_DOUBLE_EQ(h, 900);
  resizedSize(Resize::Original, 400, 300, 1600, 900, w, h);
  EXPECT_DOUBLE_EQ(w, 400);
  EXPECT_DOUBLE_EQ(h, 300);
}

TEST(Transform, IdentityFillsTarget) {
  auto m = transformMatrix({}, 800, 600, 800, 600);
  auto p = apply(m, 1, 1, 800, 600);  // esquina superior derecha
  EXPECT_NEAR(p.x, 400, 1e-3);
  EXPECT_NEAR(p.y, -300, 1e-3);
}

TEST(Transform, PositionYGoesDown) {
  TransformValues t;
  t.x = 10;
  t.y = 20;
  auto p = apply(transformMatrix(t, 100, 100, 800, 600), 0, 0, 800, 600);
  EXPECT_NEAR(p.x, 10, 1e-3);
  EXPECT_NEAR(p.y, 20, 1e-3);
}

TEST(Transform, Rotate90Scale50IsClockwise) {
  TransformValues t;
  t.scale = 0.5;
  t.rotation = 90;
  // La esquina superior derecha de un cuadro de 200×100 acaba abajo a la derecha.
  auto p = apply(transformMatrix(t, 200, 100, 800, 600), 1, 1, 800, 600);
  EXPECT_NEAR(p.x, 25, 1e-3);
  EXPECT_NEAR(p.y, 50, 1e-3);
}

TEST(Transform, RotationPivotsOnAnchor) {
  TransformValues t;
  t.rotation = 180;
  t.anchorX = 50;
  // El punto del ancla no se mueve y el centro pasa al otro lado.
  auto m = transformMatrix(t, 100, 100, 800, 600);
  auto anchor = apply(m, 1, 0, 800, 600);
  EXPECT_NEAR(anchor.x, 50, 1e-3);
  EXPECT_NEAR(anchor.y, 0, 1e-3);
  auto center = apply(m, 0, 0, 800, 600);
  EXPECT_NEAR(center.x, 100, 1e-3);
}
