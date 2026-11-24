#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "compositor/Transform.h"
#include "media/IMediaSource.h"

namespace vj::schema {
struct EffectType;
}

namespace vj::compositor {

struct PlanEffect {
  const schema::EffectType* type = nullptr;
  std::array<float, 12> values = {};
  float opacity = 1;
  int blend = 1;  // Alpha
  media::FramePtr aux;  // textura de apoyo: LUT o texto
  std::string instance;  // ruta de la instancia, para efectos externos con estado propio
  struct ExternalValue {
    uint32_t index;
    float value;
  };
  std::vector<ExternalValue> extra;
};
using PlanChain = std::vector<PlanEffect>;

// Efectos que no son un shader nuestro (plugins FFGL). Se llaman en el hilo de render.
class IExternalEffects {
 public:
  virtual ~IExternalEffects() = default;
  virtual bool render(const PlanEffect& effect, gfx::IRenderBackend& gpu, gfx::TargetId input, gfx::TargetId output,
                      int width, int height, float time, float bpm, float beat) = 0;
  virtual void endFrame() = 0;
  virtual void releaseGpu() = 0;
};

// Descripción de un fotograma, sin GPU: la produce Composition y la ejecuta Renderer.
struct PlanClip {
  media::FramePtr frame;
  uint64_t key = 0;  // identifica la textura reutilizable
  Resize resize = Resize::Fit;
  TransformValues transform;
  float opacity = 1;
  std::array<bool, 3> channels = {true, true, true};
  bool useAlpha = true;
  bool straightAlpha = false;
  bool hidden = false;  // vídeo quitado: solo suena
  PlanChain effects;
};

enum class MaskMode { Disabled, AllBelow, OneBelow };

struct PlanLayer {
  int bus = 0;  // 1 A, 2 B
  float busOpacity = 1;  // opacidad sin el crossfader, para las salidas por bus
  std::optional<PlanClip> current;
  std::optional<PlanClip> previous;  // saliente durante una transición
  float mix = 1;
  int transition = 0;
  int blend = 0;
  float opacity = 1;
  bool visible = true;
  MaskMode mask = MaskMode::Disabled;
  TransformValues transform;
  int group = 0;
  PlanChain effects;
  bool routed = false;        // va a un slice y sale de la mezcla
  bool ignoreBypass = false;
};

struct PlanGroup {
  int blend = 0;
  float opacity = 1;
  bool visible = true;
  TransformValues transform;
  PlanChain effects;
  bool routed = false;
  bool ignoreBypass = false;
};

struct RenderPlan {
  int width = 1920, height = 1080;
  float master = 1;
  bool bypass = false;
  bool routeBus[2] = {false, false};
  float time = 0, beat = 0, bpm = 120;
  PlanChain effects;  // de composición, sobre la mezcla final
  std::vector<PlanLayer> layers;  // índice 0: la capa 1, la de abajo
  std::vector<PlanGroup> groups;  // índice 0: el grupo 1
};

}  // namespace vj::compositor
