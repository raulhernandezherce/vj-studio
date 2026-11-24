#pragma once
#include "gfx/IRenderBackend.h"

namespace vj::compositor {

enum class Resize { Fit, Fill, Stretch, Original };

struct TransformValues {
  double x = 0, y = 0;              // píxeles, Y hacia abajo
  double scale = 1, scaleW = 1, scaleH = 1;
  double rotation = 0;              // grados, sentido horario
  double anchorX = 0, anchorY = 0;  // píxeles desde el centro
  bool identity() const {
    return x == 0 && y == 0 && scale == 1 && scaleW == 1 && scaleH == 1 && rotation == 0;
  }
};

// Tamaño del contenido en la composición según el ajuste.
void resizedSize(Resize mode, double srcW, double srcH, double compW, double compH, double& w, double& h);

// Lleva el cuadro [-1,1]² al espacio del destino.
// Orden: escala y rotación alrededor del ancla, y después posición.
gfx::Mat3 transformMatrix(const TransformValues& t, double contentW, double contentH, double compW, double compH);

}  // namespace vj::compositor
