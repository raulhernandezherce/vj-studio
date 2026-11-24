#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "media/IMediaSource.h"

namespace vj::media {

FramePtr loadCubeLut(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("no se pudo abrir la LUT");
  int n = 0;
  std::vector<float> values;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::stringstream s(line);
    std::string first;
    s >> first;
    if (first == "LUT_3D_SIZE") {
      s >> n;
    } else if (first == "LUT_1D_SIZE") {
      throw std::runtime_error("solo se admiten LUT 3D");
    } else if (first == "DOMAIN_MIN" || first == "DOMAIN_MAX" || first == "TITLE" || first == "LUT_3D_INPUT_RANGE") {
      continue;
    } else if (std::isdigit(static_cast<unsigned char>(first[0])) || first[0] == '-' || first[0] == '.') {
      float g, b;
      s >> g >> b;
      if (!s) throw std::runtime_error("LUT dañada");
      values.push_back(std::stof(first));
      values.push_back(g);
      values.push_back(b);
    }
  }
  if (n < 2 || n > 129 || values.size() != static_cast<size_t>(n) * n * n * 3) throw std::runtime_error("LUT incompleta");
  auto f = std::make_shared<VideoFrame>();
  f->width = n * n;
  f->height = n;
  f->stride = f->width * 4;
  f->format = gfx::PixelFormat::RGBA8;
  f->pixels.resize(static_cast<size_t>(f->stride) * n);
  // .cube recorre R primero y B al final: cada corte es un valor de B.
  const auto byte = [](float v) { return static_cast<uint8_t>(std::min(1.f, std::max(0.f, v)) * 255.f + 0.5f); };
  for (int b = 0; b < n; ++b)
    for (int g = 0; g < n; ++g)
      for (int r = 0; r < n; ++r) {
        const size_t src = (static_cast<size_t>(b) * n * n + static_cast<size_t>(g) * n + r) * 3;
        uint8_t* d = f->pixels.data() + (static_cast<size_t>(g) * f->width + static_cast<size_t>(b) * n + r) * 4;
        d[0] = byte(values[src]);
        d[1] = byte(values[src + 1]);
        d[2] = byte(values[src + 2]);
        d[3] = 255;
      }
  return f;
}

}  // namespace vj::media
