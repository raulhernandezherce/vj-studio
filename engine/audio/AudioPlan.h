#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "audio/AudioData.h"
#include "audio/AudioEffects.h"

namespace vj::audio {

struct EffectStep {
  std::shared_ptr<AudioEffect> effect;
  std::array<float, 12> values = {};
};

struct Voice {
  uint64_t id = 0;       // estable mientras suena, para conservar la posición de lectura
  AudioPtr data;
  double position = 0;   // segundos, según el reloj del clip
  double rate = 1;       // negativo: marcha atrás
  float gainL = 1, gainR = 1;
  int bus = 0;           // índice en AudioPlan::buses
  int tap = -1;          // análisis por clip
  std::vector<EffectStep> effects;
};

struct Bus {
  int parent = -1;       // -1: va al master
  float gainL = 1, gainR = 1;
  int tap = -1;
  std::vector<EffectStep> effects;
};

// Foto de lo que debe sonar. La construye el hilo principal; el de audio solo la lee.
struct AudioPlan {
  std::vector<Bus> buses;  // los hijos van antes que sus padres
  std::vector<Voice> voices;
  std::vector<Voice> previewVoices;
  std::vector<EffectStep> masterEffects;
  float masterL = 1, masterR = 1;
  int masterTap = -1;
  double metronomeBeatsPerSecond = 0;  // 0: apagado
  double beat = 0;
  float metronomeGain = 0;
};

}  // namespace vj::audio
