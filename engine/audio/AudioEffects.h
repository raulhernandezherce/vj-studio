#pragma once
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace vj::audio {

// Estado de un efecto de audio. Se crea fuera del hilo de audio (reserva memoria) y
// process() no reserva ni bloquea. Sin efecto activo, un dato nulo en el plan.
class AudioEffect {
 public:
  virtual ~AudioEffect() = default;
  // values en el orden del catálogo; el último es siempre Dry/Wet.
  virtual void process(float* left, float* right, int frames, const std::array<float, 12>& values) = 0;
};

std::shared_ptr<AudioEffect> makeAudioEffect(const std::string& type);

}  // namespace vj::audio
