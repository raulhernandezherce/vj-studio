#pragma once
#include <future>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "media/IMediaSource.h"
#include "patch/Patch.h"

namespace vj::patch {

// Programas compartidos por todos los parches en marcha. Viven mientras viva la GPU.
class Programs {
 public:
  explicit Programs(gfx::IRenderBackend& gpu);
  ~Programs();
  gfx::ProgramId effect(const schema::EffectType& type, std::string& error);
  gfx::ProgramId solid = 0, mixer = 0, blend = 0, transition = 0, copy = 0, clip = 0, shape = 0;
  gfx::TextureId empty = gfx::kNoTexture;

 private:
  gfx::IRenderBackend& gpu_;
  std::map<std::string, gfx::ProgramId> effects_;
};

struct FrameInfo {
  gfx::TextureId input = gfx::kNoTexture;
  int width = 0, height = 0;
  double time = 0, beat = 0, bpm = 120, dt = 0;
};

// Un parche en ejecución: una pasada por nodo de textura, con su propio destino.
class Runtime {
 public:
  Runtime(const NodeLibrary& lib, ParameterRegistry& params, std::string baseDir) : lib_(lib), params_(params), base_(std::move(baseDir)) {}
  // Conserva el estado de los nodos que siguen: editar no reinicia la realimentación.
  void update(const Patch& patch, gfx::IRenderBackend& gpu);
  // Devuelve lo que llega a Texture Output, o kNoTexture si no hay ninguno conectado.
  gfx::TextureId run(gfx::IRenderBackend& gpu, Programs& programs, const FrameInfo& frame, const std::map<int, float>& overrides = {});
  gfx::TextureId nodeTexture(int id) const;
  const std::map<int, std::string>& errors() const { return errors_; }
  void release(gfx::IRenderBackend& gpu);

 private:
  struct State {
    std::vector<Value> out;
    std::array<double, 4> memory = {};
    gfx::TargetId targets[2] = {gfx::kNoTarget, gfx::kNoTarget};
    int current = 0, width = 0, height = 0;
    bool deep = false;
    std::string isfCode;
    std::unique_ptr<schema::EffectType> isf;
    std::string mediaPath;
    std::future<media::SourcePtr> opening;
    media::SourcePtr source;
    gfx::TextureId frameTexture = gfx::kNoTexture;
    const media::VideoFrame* lastFrame = nullptr;
    media::FramePtr hold;
  };
  void drawNode(gfx::IRenderBackend& gpu, Programs& programs, const Node& node, const NodeType& type, State& state, const std::vector<Value>& in, const FrameInfo& frame);
  void freeTargets(gfx::IRenderBackend& gpu, State& state);

  const NodeLibrary& lib_;
  ParameterRegistry& params_;
  std::string base_;
  Patch patch_;
  Order order_;
  std::map<int, const Node*> nodes_;
  std::map<int, std::vector<const Link*>> incoming_;
  std::set<int> feedback_;
  std::map<int, State> states_;
  std::map<int, std::string> errors_;
};

}  // namespace vj::patch
