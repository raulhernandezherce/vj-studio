#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "media/IMediaSource.h"

namespace vj::media {
namespace {

std::shared_ptr<VideoFrame> blank(int w, int h, double t) {
  auto f = std::make_shared<VideoFrame>();
  f->width = w;
  f->height = h;
  f->stride = w * 4;
  f->format = gfx::PixelFormat::RGBA8;
  f->time = t;
  f->pixels.resize(static_cast<size_t>(f->stride) * h);
  return f;
}

// Carta de barras con una franja que barre la imagen en 2 s. Determinista: t decide cada píxel.
class PatternSource final : public IMediaSource {
 public:
  PatternSource(int w, int h) : w_(w), h_(h) {}
  MediaInfo info() const override { return {0, w_, h_, false}; }
  FramePtr frameAt(double t) override {
    static const uint8_t bars[8][3] = {{192, 192, 192}, {192, 192, 0}, {0, 192, 192}, {0, 192, 0},
                                       {192, 0, 192},   {192, 0, 0},   {0, 0, 192},   {0, 0, 0}};
    auto f = blank(w_, h_, t);
    const int sweep = static_cast<int>(std::fmod(t, 2.0) / 2.0 * w_);
    const int barW = std::max(1, w_ / 8), sweepW = std::max(1, w_ / 64);
    for (int y = 0; y < h_; ++y) {
      uint8_t* row = f->pixels.data() + static_cast<size_t>(y) * f->stride;
      const bool band = y > h_ * 3 / 4;
      for (int x = 0; x < w_; ++x) {
        const uint8_t* c = bars[std::min(7, x / barW)];
        const uint8_t grey = static_cast<uint8_t>(x * 255 / std::max(1, w_ - 1));
        const bool on = band && x >= sweep && x < sweep + sweepW;
        row[x * 4 + 0] = on ? 255 : band ? grey : c[0];
        row[x * 4 + 1] = on ? 255 : band ? grey : c[1];
        row[x * 4 + 2] = on ? 255 : band ? grey : c[2];
        row[x * 4 + 3] = 255;
      }
    }
    return f;
  }

 private:
  int w_, h_;
};

class StaticSource final : public IMediaSource {
 public:
  explicit StaticSource(FramePtr f) : frame_(std::move(f)) {}
  MediaInfo info() const override { return {0, frame_->width, frame_->height, false}; }
  FramePtr frameAt(double) override { return frame_; }

 private:
  FramePtr frame_;
};

FramePtr solid(const std::string& hex) {
  const unsigned long v = std::strtoul(hex.c_str(), nullptr, 16);
  auto f = blank(16, 9, 0);
  const uint8_t a = hex.size() >= 8 ? v & 0xFF : 255;
  const unsigned long rgb = hex.size() >= 8 ? v >> 8 : v;
  for (size_t i = 0; i < f->pixels.size(); i += 4) {
    f->pixels[i] = (rgb >> 16) & 0xFF;
    f->pixels[i + 1] = (rgb >> 8) & 0xFF;
    f->pixels[i + 2] = rgb & 0xFF;
    f->pixels[i + 3] = a;
  }
  return f;
}

// Círculo blanco sobre negro, para probar máscaras.
FramePtr circle(int w, int h) {
  auto f = blank(w, h, 0);
  const double r = h * 0.25;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const bool in = std::hypot(x + 0.5 - w / 2.0, y + 0.5 - h / 2.0) <= r;
      uint8_t* p = f->pixels.data() + static_cast<size_t>(y) * f->stride + x * 4;
      p[0] = p[1] = p[2] = in ? 255 : 0;
      p[3] = 255;
    }
  return f;
}

}  // namespace

SourcePtr makeGenerator(const std::string& path, int width, int height) {
  const std::string name = path.substr(5);  // tras "vj://"
  if (name == "pattern") return std::make_shared<PatternSource>(width, height);
  if (name == "circle") return std::make_shared<StaticSource>(circle(width, height));
  // Las fuentes generativas son un efecto en cabeza de cadena: parten de un lienzo vacío.
  if (name.rfind("source/", 0) == 0) return std::make_shared<StaticSource>(blank(16, 9, 0));
  if (name.rfind("solid/", 0) == 0) return std::make_shared<StaticSource>(solid(name.substr(6)));
  return nullptr;
}

}  // namespace vj::media
