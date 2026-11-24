#include "compositor/Transform.h"

#include <algorithm>
#include <cmath>

namespace vj::compositor {

void resizedSize(Resize mode, double srcW, double srcH, double compW, double compH, double& w, double& h) {
  switch (mode) {
    case Resize::Stretch:
      w = compW;
      h = compH;
      return;
    case Resize::Original:
      w = srcW;
      h = srcH;
      return;
    case Resize::Fit:
    case Resize::Fill: {
      const double sx = compW / srcW, sy = compH / srcH;
      const double s = mode == Resize::Fit ? std::min(sx, sy) : std::max(sx, sy);
      w = srcW * s;
      h = srcH * s;
      return;
    }
  }
}

gfx::Mat3 transformMatrix(const TransformValues& t, double contentW, double contentH, double compW, double compH) {
  const double rad = t.rotation * 3.14159265358979323846 / 180.0;
  const double c = std::cos(rad), s = std::sin(rad);
  const double sx = t.scale * t.scaleW * contentW / 2, sy = t.scale * t.scaleH * contentH / 2;
  // p' = R·S·(p) con el ancla como pivote, en píxeles con Y abajo; el cuadro tiene Y arriba.
  const double m00 = c * sx, m01 = s * sy;   // x' = m00·qx + m01·qy
  const double m10 = s * sx, m11 = -c * sy;  // y' = m10·qx + m11·qy
  const double ax = t.anchorX, ay = t.anchorY;
  const double tx = t.x + ax - (c * ax - s * ay), ty = t.y + ay - (s * ax + c * ay);
  const double nx = 2 / compW, ny = -2 / compH;
  return {static_cast<float>(m00 * nx), static_cast<float>(m10 * ny), 0,
          static_cast<float>(m01 * nx), static_cast<float>(m11 * ny), 0,
          static_cast<float>(tx * nx),  static_cast<float>(ty * ny),  1};
}

}  // namespace vj::compositor
