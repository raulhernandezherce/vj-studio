#pragma once
#include <cstddef>
#include <vector>

namespace vj::audio {

inline constexpr size_t kFftSize = 2048;

// Magnitudes de 0 a Nyquist (kFftSize/2 valores), con ventana de Hann y normalizadas
// para que un seno de amplitud 1 dé 1 en su bin.
std::vector<float> spectrum(const float* samples);

// Energía media en un rango de frecuencias.
float bandLevel(const std::vector<float>& spectrum, double lowHz, double highHz, int sampleRate);

}  // namespace vj::audio
