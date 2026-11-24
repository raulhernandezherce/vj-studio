#include "audio/Fft.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace vj::audio {
namespace {

void fft(std::vector<std::complex<float>>& a) {
  const size_t n = a.size();
  for (size_t i = 1, j = 0; i < n; ++i) {
    size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(a[i], a[j]);
  }
  for (size_t len = 2; len <= n; len <<= 1) {
    const float angle = -6.28318530718f / static_cast<float>(len);
    const std::complex<float> step(std::cos(angle), std::sin(angle));
    for (size_t i = 0; i < n; i += len) {
      std::complex<float> w(1, 0);
      for (size_t k = 0; k < len / 2; ++k) {
        const auto u = a[i + k], v = a[i + k + len / 2] * w;
        a[i + k] = u + v;
        a[i + k + len / 2] = u - v;
        w *= step;
      }
    }
  }
}

}  // namespace

std::vector<float> spectrum(const float* s) {
  std::vector<std::complex<float>> a(kFftSize);
  for (size_t i = 0; i < kFftSize; ++i) {
    const float w = 0.5f - 0.5f * std::cos(6.28318530718f * static_cast<float>(i) / (kFftSize - 1));
    a[i] = s[i] * w;
  }
  fft(a);
  std::vector<float> out(kFftSize / 2);
  // 4/N compensa el reparto en dos mitades (2) y la ganancia de la ventana de Hann (0,5).
  for (size_t i = 0; i < out.size(); ++i) out[i] = std::abs(a[i]) * 4.f / kFftSize;
  return out;
}

float bandLevel(const std::vector<float>& s, double lowHz, double highHz, int rate) {
  const double hzPerBin = static_cast<double>(rate) / kFftSize;
  const size_t a = std::min(s.size() - 1, static_cast<size_t>(lowHz / hzPerBin));
  const size_t b = std::min(s.size(), static_cast<size_t>(std::ceil(highHz / hzPerBin)) + 1);
  float peak = 0;
  for (size_t i = a; i < b; ++i) peak = std::max(peak, s[i]);
  return peak;
}

}  // namespace vj::audio
