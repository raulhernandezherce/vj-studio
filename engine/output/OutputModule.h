#pragma once
#include <future>
#include <atomic>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "compositor/CompositionModule.h"
#include "core/Module.h"
#include "core/ParameterRegistry.h"
#include "gfx/IRenderBackend.h"
#include "media/IMediaSource.h"
#include "output/ScreenRenderer.h"
#include "output/ScreenSetup.h"

struct GLFWwindow;

namespace vj::output {

struct OutputOptions {
  bool hidden = false;        // sin ventanas visibles (pruebas)
  bool hideDockIcon = false;  // motor lanzado por la interfaz
  bool closeQuits = true;     // cerrar la ventana cierra el motor
  std::string documents;      // carpeta de escenarios
};

// Salida: un contexto OpenGL principal, oculto, y una ventana por cada pantalla física.
// Las ventanas comparten texturas con el principal; todo se dibuja allí y solo se presenta en ellas.
class OutputModule final : public IModule {
 public:
  explicit OutputModule(OutputOptions options) : options_(std::move(options)) {}
  std::string name() const override { return "output"; }
  bool start(EngineContext& ctx) override;
  void tick(double dt) override;
  void stop() override;

  gfx::IRenderBackend* backend() { return backend_.get(); }
  std::vector<uint8_t> smallFrame(int width, int height) {
    return last_ ? backend_->readback(renderer_->scaleForMonitor(last_, width, height), width, height) : std::vector<uint8_t>{};
  }
  gfx::TextureId compositionTexture() { return last_ ? backend_->targetTexture(last_) : gfx::kNoTexture; }
  bool writeSnapshot(const std::string& path);
  const ScreenSetup& setup() const { return setup_; }

 private:
  struct Window {
    GLFWwindow* handle = nullptr;
    int screen = 0;
    std::string device;
    bool fullscreen = false;
    int monitor = -1;
  };
  void registerCommands();
  void applySetup(ScreenSetup next, bool remember);
  void syncWindows();
  void closeWindow(Window& w);
  void applyRouting();
  void publishDisplays();
  void sendMonitors(gfx::TargetId comp);
  void updateRecording(gfx::TargetId comp, double dt);
  void startRecording();
  void stopRecording();
  void stepClipRender();
  std::string recordFolder() const;
  std::string presetPath(const std::string& name) const;
  gfx::TextureId sourceTexture(const std::string& type, int index, gfx::TargetId comp);

  OutputOptions options_;
  EngineContext* ctx_ = nullptr;
  compositor::IFrameRenderer* renderer_ = nullptr;
  GLFWwindow* main_ = nullptr;
  std::unique_ptr<gfx::IRenderBackend> backend_;
  std::unique_ptr<ScreenRenderer> screens_;
  ScreenSetup setup_ = defaultSetup();
  std::deque<ScreenSetup> undo_, redo_;
  std::map<int, Window> windows_;
  std::map<int, RenderedScreen> rendered_;
  gfx::TargetId last_ = gfx::kNoTarget;
  int compW_ = 0, compH_ = 0, fbW_ = 0, fbH_ = 0;
  double sinceMonitor_ = 0, sinceDisplays_ = 1e9, time_ = 0;
  gfx::TargetId previewTarget_ = gfx::kNoTarget;
  bool usePreview_ = false, dockPolicyApplied_ = false;
  std::atomic<bool> dirty_{true};
  // Abrir y cerrar un archivo puede tardar segundos: nunca en el hilo de salida.
  using RecorderPtr = std::unique_ptr<media::VideoRecorder>;
  struct Closed {
    bool ok = false;
    long written = 0, dropped = 0;
  };
  static std::future<Closed> closeInBackground(RecorderPtr recorder);
  RecorderPtr recorder_;
  std::future<RecorderPtr> opening_;
  std::future<Closed> closing_;
  double recordClock_ = 0, recordNext_ = 0;
  struct ClipRender {
    RecorderPtr recorder;
    std::future<RecorderPtr> opening;
    std::future<Closed> closing;
    compositor::Composition* comp = nullptr;
    int layer = 0, column = 0, frame = 0, total = 0, fps = 30, waited = 0;
    std::string path;
    bool done = false;
  };
  std::unique_ptr<ClipRender> render_;
  std::vector<ParameterRegistry::ListenerId> listeners_;
};

}  // namespace vj::output
