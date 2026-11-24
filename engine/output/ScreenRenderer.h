#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "gfx/IRenderBackend.h"
#include "media/IMediaSource.h"
#include "output/ScreenSetup.h"

namespace vj::output {

struct RenderedScreen {
  gfx::TargetId target = gfx::kNoTarget;
  int width = 0, height = 0;
};

// Dibuja cada pantalla: sus slices, máscaras y corrección de color.
class ScreenRenderer {
 public:
  using SourceFn = std::function<gfx::TextureId(const std::string& type, int index)>;

  explicit ScreenRenderer(gfx::IRenderBackend& gpu);
  ~ScreenRenderer();

  // Devuelve las pantallas ya compuestas, por id. Una pantalla usada como fuente de otra se
  // pinta antes; un ciclo deja la fuente en negro.
  const std::map<int, RenderedScreen>& render(const ScreenSetup& setup, const SourceFn& source, bool testCard, bool identify, double time);

 private:
  void renderScreen(const Screen& s, const SourceFn& source, bool testCard, bool identify, int number);
  void ensure(const Screen& s);
  gfx::TargetId sliceMask(const Screen& s, const Slice& l);
  void drawMask(const ScreenMask& m, float top, float height, float color);

  gfx::IRenderBackend& gpu_;
  gfx::ProgramId sliceProgram_ = 0, maskProgram_ = 0, colorProgram_ = 0, copyProgram_ = 0, finalProgram_ = 0, cardProgram_ = 0, labelProgram_ = 0;
  struct Targets {
    gfx::TargetId draft = 0, mask = 0, done = 0;
    int w = 0, h = 0;
    // Anillo del retardo: solo existe mientras la pantalla tiene retardo.
    std::vector<gfx::TargetId> ring;
    std::vector<double> stamps;
    size_t head = 0;
  };
  void releaseRing(Targets& t);
  gfx::TargetId delayed(const Screen& s, Targets& t, double time);
  std::map<int, Targets> targets_;
  // Máscaras de slice, en sus coordenadas: se repintan solo si cambian.
  struct SliceMask {
    gfx::TargetId target = 0;
    std::string signature;
  };
  std::map<std::pair<int, int>, SliceMask> sliceMasks_;
  std::map<int, RenderedScreen> out_;
  std::map<int, gfx::TextureId> labels_;
  gfx::TargetId card_ = 0;
  gfx::TextureId empty_ = gfx::kNoTexture;
};

}  // namespace vj::output
