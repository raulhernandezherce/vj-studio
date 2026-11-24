#pragma once
#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "compositor/RenderPlan.h"
#include "core/generated/Effects.gen.h"
#include "gfx/IRenderBackend.h"

namespace vj::compositor {

// Ejecuta un RenderPlan. Solo habla con la GPU a través de IRenderBackend.
class Renderer {
 public:
  explicit Renderer(gfx::IRenderBackend& backend);
  ~Renderer();

  // Devuelve el destino con la composición terminada, premultiplicada.
  gfx::TargetId render(const RenderPlan& plan);
  // Imagen propia de una capa o grupo enrutados en el último render. 0 si no hay.
  gfx::TextureId routedLayer(int layer);
  gfx::TextureId routedGroup(int group);
  gfx::TextureId routedBus(int bus);
  void setExternal(IExternalEffects* external) { external_ = external; }
  void setPatches(IExternalEffects* patches) { patches_ = patches; }
  // Copia reducida de un destino, para los monitores de la interfaz.
  gfx::TargetId downscale(gfx::TargetId source, int width, int height);

  // GLSL completo de cada pasada, expuesto para las pruebas de compilación.
  static std::string commonShader();
  static std::string clipShader();
  static std::string blendShader();
  static std::string transitionShader();
  static std::string effectShader(const schema::EffectType& type);
  // Efectos cuyo shader no compiló; se saltan sin detener la salida.
  const std::map<std::string, std::string>& effectErrors() const { return effectErrors_; }

 private:
  void ensureTargets(int width, int height);
  gfx::TextureId textureFor(const PlanClip& clip);
  void applyChain(const PlanChain& chain, gfx::TargetId target);
  gfx::ProgramId programFor(const schema::EffectType& type);
  void drawClip(const PlanClip& clip, gfx::TargetId into, int w, int h);
  void renderLayer(const PlanLayer& layer, gfx::TargetId into, int w, int h);
  void transformInto(gfx::TargetId from, gfx::TargetId into, const TransformValues& t, int w, int h);
  void blendInto(gfx::TargetId& accum, gfx::TargetId& scratch, gfx::TargetId source, int mode, float opacity);
  gfx::TargetId routeTarget(std::map<int, gfx::TargetId>& pool, int key);
  void copyInto(gfx::TargetId from, gfx::TargetId to);
  void maskInto(gfx::TargetId& accum, gfx::TargetId& scratch, gfx::TargetId mask, float amount);

  gfx::IRenderBackend& gpu_;
  gfx::ProgramId clipProgram_ = 0, blendProgram_ = 0, transitionProgram_ = 0, maskProgram_ = 0, copyProgram_ = 0, mixProgram_ = 0;
  int alphaMode_ = 1;
  int width_ = 0, height_ = 0;
  enum { kAccum, kAccumB, kGroup, kGroupB, kLayer, kLayerB, kClipA, kClipB, kMask, kFx0, kFx1, kTargets };
  std::array<gfx::TargetId, kTargets> targets_ = {};
  struct Cached {
    gfx::TextureId texture = gfx::kNoTexture;
    const media::VideoFrame* frame = nullptr;
    media::FramePtr hold;
    bool used = false;
  };
  std::map<uint64_t, Cached> textures_;
  gfx::TargetId monitor_ = 0;
  std::map<int, gfx::TargetId> routedLayers_, routedGroups_;
  gfx::TargetId busTargets_[2] = {0, 0}, busScratch_[2] = {0, 0};
  gfx::TextureId emptyTexture_ = gfx::kNoTexture;  // uTex1 cuando un efecto no tiene apoyo
  std::map<std::string, gfx::ProgramId> effectPrograms_;
  // Fotogramas guardados por cada instancia de efecto con memoria.
  struct EffectMemory {
    std::vector<gfx::TargetId> ring;
    int filled = 0;
    size_t head = 0;  // último guardado
    bool used = false;
  };
  std::map<std::string, EffectMemory> memory_;
  EffectMemory& memoryFor(const PlanEffect& e);
  void remember(EffectMemory& m, gfx::TargetId source);
  gfx::TextureId recall(const EffectMemory& m, int age);
  void releaseMemory();
  struct AuxTexture {
    gfx::TextureId texture = gfx::kNoTexture;
    media::FramePtr hold;  // mantiene viva la dirección que sirve de clave
  };
  std::map<const media::VideoFrame*, AuxTexture> auxTextures_;
  std::map<std::string, std::string> effectErrors_;
  float time_ = 0, beat_ = 0, bpm_ = 120;
  IExternalEffects* external_ = nullptr;
  IExternalEffects* patches_ = nullptr;
  int monitorW_ = 0, monitorH_ = 0;
};

}  // namespace vj::compositor
