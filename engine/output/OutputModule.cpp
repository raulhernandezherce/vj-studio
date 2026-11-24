#include "output/OutputModule.h"

#include <GLFW/glfw3.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "core/CommandBus.h"
#include "core/EventBus.h"
#include "core/ModuleRegistry.h"
#include "media/IMediaSource.h"

using nlohmann::json;
namespace fs = std::filesystem;

namespace vj::output {

#if defined(__APPLE__)
void hideFromDock();
#endif

namespace {
constexpr size_t kUndoLimit = 100;
}

std::string OutputModule::presetPath(const std::string& name) const {
  std::string clean = name;
  std::replace_if(clean.begin(), clean.end(), [](char c) { return c == '/' || c == '\\' || c == ':'; }, '-');
  if (clean.empty() || clean.front() == '.') throw std::runtime_error("nombre de escenario no válido");
  return (fs::path(options_.documents) / "Presets" / "Screens" / (clean + ".json")).string();
}

bool OutputModule::start(EngineContext& ctx) {
  ctx_ = &ctx;
  renderer_ = ctx.modules.get<compositor::IFrameRenderer>("composition");
  if (!glfwInit()) {
    std::fprintf(stderr, "vjengine: no se pudo iniciar GLFW\n");
    return false;
  }
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  main_ = glfwCreateWindow(16, 16, "VJ Studio", nullptr, nullptr);
  if (!main_) {
    std::fprintf(stderr, "vjengine: no se pudo crear el contexto OpenGL 4.1\n");
    glfwTerminate();
    return false;
  }
  glfwMakeContextCurrent(main_);
  try {
    backend_ = gfx::makeOpenGL41Backend();
    screens_ = std::make_unique<ScreenRenderer>(*backend_);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "vjengine: %s\n", e.what());
    return false;
  }
  ctx.params.publish("/engine/backend", backend_->name());

  std::error_code ec;
  fs::create_directories(fs::path(options_.documents) / "Presets" / "Screens", ec);
  std::ifstream last(fs::path(options_.documents) / "Preferences" / "screens.json");
  if (last) {
    try {
      setup_ = fromJson(json::parse(last));
    } catch (const std::exception&) {
      setup_ = defaultSetup();  // un archivo dañado no impide arrancar
    }
  }
  ctx.params.publish("/output/setup", setup_.name);
  if (ctx.commands) registerCommands();
  for (const char* p : {"/output/visible", "/output/fullscreen", "/output/disabled"})
    listeners_.push_back(ctx.params.addListener(p, [this](const ParamDesc&, const Value&) { dirty_ = true; }));
  return true;
}

void OutputModule::stop() {
  for (auto id : listeners_) ctx_->params.removeListener(id);
  listeners_.clear();
  // Un archivo sin cerrar no se puede abrir: al salir sí se espera.
  if (opening_.valid()) try { recorder_ = opening_.get(); } catch (const std::exception&) {}
  if (recorder_) (void)recorder_->finish();
  recorder_.reset();
  if (closing_.valid()) (void)closing_.get();
  if (render_) {
    if (render_->opening.valid()) try { render_->recorder = render_->opening.get(); } catch (const std::exception&) {}
    if (render_->recorder) (void)render_->recorder->finish();
    if (render_->closing.valid()) (void)render_->closing.get();
  }
  render_.reset();
  if (main_) glfwMakeContextCurrent(main_);
  for (auto& [_, w] : windows_) closeWindow(w);
  windows_.clear();
  if (main_) glfwMakeContextCurrent(main_);
  if (renderer_) renderer_->releaseGpu();
  screens_.reset();
  backend_.reset();
  if (main_) glfwDestroyWindow(main_);
  main_ = nullptr;
  glfwTerminate();
}

void OutputModule::closeWindow(Window& w) {
  if (!w.handle) return;
  glfwMakeContextCurrent(w.handle);
  backend_->setPresentContext(w.screen);
  backend_->releasePresentContext(w.screen);
  glfwMakeContextCurrent(main_);
  glfwDestroyWindow(w.handle);
  w.handle = nullptr;
}

void OutputModule::publishDisplays() {
  int count = 0;
  GLFWmonitor** monitors = glfwGetMonitors(&count);
  json list = json::array();
  for (int i = 0; i < count; ++i) {
    const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
    int x = 0, y = 0;
    glfwGetMonitorPos(monitors[i], &x, &y);
    list.push_back({{"index", i}, {"name", glfwGetMonitorName(monitors[i])}, {"width", mode->width}, {"height", mode->height}, {"refresh", mode->refreshRate}, {"x", x}, {"y", y}});
  }
  ctx_->params.publish("/output/displays", list.dump());
}

void OutputModule::syncWindows() {
  if (!dirty_.exchange(false)) return;
  auto& p = ctx_->params;
  const bool off = p.getBool("/output/disabled") || options_.hidden;
  int count = 0;
  GLFWmonitor** monitors = glfwGetMonitors(&count);

  // Qué ventana necesita cada pantalla.
  std::map<int, const Screen*> wanted;
  if (!off)
    for (const auto& s : setup_.screens)
      if (s.enabled && s.device != "virtual") wanted[s.id] = &s;

  for (auto it = windows_.begin(); it != windows_.end();) {
    auto w = wanted.find(it->first);
    if (w == wanted.end() || w->second->device != it->second.device) {
      closeWindow(it->second);
      it = windows_.erase(it);
    } else {
      ++it;
    }
  }
  for (auto& [id, screen] : wanted) {
    int monitor = -1;
    if (screen->device.rfind("display:", 0) == 0) {
      monitor = std::atoi(screen->device.c_str() + 8);
      if (monitor >= count) continue;  // desconectada: vuelve sola al reaparecer
    }
    const bool mainWindow = screen->id == setup_.screens.front().id;
    // Compatibilidad: la pantalla principal obedece a Salida > Pantalla completa.
    if (monitor < 0 && mainWindow && p.getBool("/output/fullscreen") && count > 0) monitor = count - 1;
    Window& w = windows_[id];
    if (w.handle && w.monitor != monitor) {
      closeWindow(w);
    }
    if (!w.handle) {
      glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
      GLFWmonitor* target = monitor >= 0 ? monitors[monitor] : nullptr;
      const GLFWvidmode* mode = target ? glfwGetVideoMode(target) : nullptr;
      const int width = mode ? mode->width : std::min(screen->width, 1280);
      const int height = mode ? mode->height : std::min(screen->height, 720);
      w.handle = glfwCreateWindow(width, height, (screen->name + " — VJ Studio").c_str(), target, main_);
      w.screen = id;
      w.device = screen->device;
      w.monitor = monitor;
      if (w.handle) {
        // Si la salida tapa la interfaz, esta tecla la apaga aunque la interfaz no responda.
        glfwSetWindowUserPointer(w.handle, this);
        glfwSetKeyCallback(w.handle, [](GLFWwindow* win, int key, int, int action, int mods) {
          if (key == GLFW_KEY_D && action == GLFW_PRESS && (mods & GLFW_MOD_SHIFT) && (mods & (GLFW_MOD_SUPER | GLFW_MOD_CONTROL)))
            static_cast<OutputModule*>(glfwGetWindowUserPointer(win))->ctx_->params.set("/output/disabled", true);
        });
        glfwMakeContextCurrent(w.handle);
        // Solo la primera ventana espera al refresco: N esperas dividirían los fps entre N.
        glfwSwapInterval(windows_.size() == 1 ? 1 : 0);
        glfwMakeContextCurrent(main_);
      }
    }
    if (w.handle) {
      if (p.getBool("/output/visible")) glfwShowWindow(w.handle);
      else glfwHideWindow(w.handle);
    }
  }
}

void OutputModule::applyRouting() {
  std::map<int, bool> layers, groups;
  bool busA = false, busB = false;
  usePreview_ = false;
  for (const auto& s : setup_.screens) {
    if (!s.enabled) continue;
    for (const auto& l : s.slices) {
      if (l.bypass) continue;
      if (l.sourceType == "layer") layers[l.sourceIndex] = layers[l.sourceIndex] || l.ignoreBypass;
      if (l.sourceType == "group") groups[l.sourceIndex] = groups[l.sourceIndex] || l.ignoreBypass;
      if (l.sourceType == "preview") usePreview_ = true;
      if (l.sourceType == "bus") (l.sourceIndex == 2 ? busB : busA) = true;
    }
  }
  if (renderer_) {
    renderer_->setRouted(std::move(layers), std::move(groups));
    renderer_->setRoutedBuses(busA, busB);
  }
}

void OutputModule::applySetup(ScreenSetup next, bool remember) {
  if (remember) {
    undo_.push_back(setup_);
    if (undo_.size() > kUndoLimit) undo_.pop_front();
    redo_.clear();
  }
  setup_ = std::move(next);
  ctx_->params.publish("/output/setup", setup_.name);
  dirty_ = true;
  applyRouting();
  std::error_code ec;
  fs::create_directories(fs::path(options_.documents) / "Preferences", ec);
  std::ofstream(fs::path(options_.documents) / "Preferences" / "screens.json") << toJson(setup_).dump(1);
}

void OutputModule::registerCommands() {
  auto& bus = *ctx_->commands;
  bus.add("output.capture", [this](const json& a) {
    if (!last_) throw std::runtime_error("aún no hay imagen de salida");
    auto px = backend_->readback(last_, compW_, compH_);
    if (!media::writePng(a.at("path").get<std::string>(), compW_, compH_, px)) throw std::runtime_error("no se pudo escribir la imagen");
    return json::object();
  });
  bus.add("clip.render", [this](const json& a) {
    if (render_) throw std::runtime_error("ya hay un render en curso");
    auto* module = ctx_->modules.get<compositor::CompositionModule>("composition");
    auto* comp = module->composition();
    const int l = a.at("layer").get<int>() - 1, c = a.at("column").get<int>() - 1;
    comp->previewClip(l, c);
    auto job = std::make_unique<ClipRender>();
    job->comp = comp;
    job->fps = a.value("fps", 30);
    job->total = std::max(1, static_cast<int>(std::lround(comp->previewLength() * job->fps)));
    std::error_code ec;
    fs::create_directories(recordFolder(), ec);
    const std::string codec = a.value("codec", std::string("H.264"));
    job->path = (fs::path(recordFolder()) / ("clip-" + std::to_string(l + 1) + "-" + std::to_string(c + 1) + "-" + std::to_string(std::time(nullptr)) + (codec == "ProRes 422" ? ".mov" : ".mp4"))).string();
    compW_ = static_cast<int>(ctx_->params.getInt("/composition/width", compW_));
    compH_ = static_cast<int>(ctx_->params.getInt("/composition/height", compH_));
    job->opening = std::async(std::launch::async, media::makeRecorder, job->path, compW_, compH_, codec);
    render_ = std::move(job);
    return json{{"path", render_->path}, {"frames", render_->total}};
  });
  bus.add("media.cameras", [](const json&) {
    json out = json::array();
    for (const auto& d : media::listCaptureDevices()) out.push_back({{"id", d.id}, {"name", d.name}});
    return out;
  });
  bus.add("screens.get", [this](const json&) { return toJson(setup_); });
  bus.add("screens.set", [this](const json& a) {
    applySetup(fromJson(a.at("setup")), a.value("history", true));
    return toJson(setup_);
  });
  bus.add("screens.undo", [this](const json&) {
    if (undo_.empty()) return json::object();
    redo_.push_back(setup_);
    ScreenSetup previous = undo_.back();
    undo_.pop_back();
    applySetup(std::move(previous), false);
    return toJson(setup_);
  });
  bus.add("screens.redo", [this](const json&) {
    if (redo_.empty()) return json::object();
    undo_.push_back(setup_);
    ScreenSetup next = redo_.back();
    redo_.pop_back();
    applySetup(std::move(next), false);
    return toJson(setup_);
  });
  bus.add("screens.preset.list", [this](const json&) {
    json out = json::array();
    std::error_code ec;
    for (const auto& f : fs::directory_iterator(fs::path(options_.documents) / "Presets" / "Screens", ec))
      if (f.path().extension() == ".json") out.push_back(f.path().stem().string());
    return out;
  });
  bus.add("screens.preset.save", [this](const json& a) {
    ScreenSetup copy = setup_;
    copy.name = a.at("name");
    std::ofstream out(presetPath(copy.name));
    if (!out) throw std::runtime_error("no se pudo guardar el escenario");
    out << toJson(copy).dump(1);
    applySetup(std::move(copy), false);
    return json::object();
  });
  bus.add("screens.preset.load", [this](const json& a) {
    std::ifstream in(presetPath(a.at("name")));
    if (!in) throw std::runtime_error("el escenario no existe");
    applySetup(fromJson(json::parse(in)), true);
    return toJson(setup_);
  });
  bus.add("screens.preset.import", [this](const json& a) {
    std::ifstream in(a.at("path").get<std::string>());
    ScreenSetup s = fromJson(json::parse(in));
    s.name = fs::path(a.at("path").get<std::string>()).stem().string();
    std::ofstream(presetPath(s.name)) << toJson(s).dump(1);
    applySetup(std::move(s), true);
    return toJson(setup_);
  });
  bus.add("screens.preset.delete", [this](const json& a) {
    std::error_code ec;
    fs::remove(presetPath(a.at("name")), ec);
    return json::object();
  });
  bus.add("screens.capture", [this](const json& a) {
    auto it = rendered_.find(a.at("id").get<int>());
    if (it == rendered_.end()) throw std::runtime_error("pantalla sin imagen");
    auto px = backend_->readback(it->second.target, it->second.width, it->second.height);
    if (!media::writePng(a.at("path").get<std::string>(), it->second.width, it->second.height, px)) throw std::runtime_error("no se pudo escribir la imagen");
    return json::object();
  });
  bus.add("screens.pixels", [this](const json& a) {
    // Lectura de un punto, para pruebas: x e y normalizadas.
    auto it = rendered_.find(a.at("id").get<int>());
    if (it == rendered_.end()) throw std::runtime_error("pantalla sin imagen");
    const auto& r = it->second;
    auto px = backend_->readback(r.target, r.width, r.height);
    const size_t x = std::min<size_t>(static_cast<size_t>(a.at("x").get<double>() * r.width), static_cast<size_t>(r.width) - 1);
    const size_t y = std::min<size_t>(static_cast<size_t>(a.at("y").get<double>() * r.height), static_cast<size_t>(r.height) - 1);
    const size_t i = (y * static_cast<size_t>(r.width) + x) * 4;
    return json::array({px[i], px[i + 1], px[i + 2], px[i + 3]});
  });
}

std::string OutputModule::recordFolder() const {
  const std::string custom = ctx_->params.getString("/preferences/recordfolder");
  return custom.empty() ? (fs::path(options_.documents) / "Recorded").string() : custom;
}

void OutputModule::startRecording() {
  auto& p = ctx_->params;
  const std::string codec = p.getString("/record/codec");
  const std::time_t now = std::time(nullptr);
  char stamp[32];
  std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", std::localtime(&now));
  std::error_code ec;
  fs::create_directories(recordFolder(), ec);
  std::string path = (fs::path(recordFolder()) / (std::string("grabacion-") + stamp + (codec == "ProRes 422" ? ".mov" : ".mp4"))).string();
  for (int n = 2; fs::exists(path); ++n)
    path = (fs::path(recordFolder()) / (std::string("grabacion-") + stamp + "-" + std::to_string(n) + (codec == "ProRes 422" ? ".mov" : ".mp4"))).string();
  opening_ = std::async(std::launch::async, media::makeRecorder, path, compW_, compH_, codec);
  p.publish("/record/file", path);
  p.publish("/record/status", "preparando");
  p.publish("/record/frames", int64_t{0});
  p.publish("/record/dropped", int64_t{0});
}

std::future<OutputModule::Closed> OutputModule::closeInBackground(RecorderPtr recorder) {
  return std::async(std::launch::async, [r = std::shared_ptr<media::VideoRecorder>(std::move(recorder))] {
    const bool ok = r->finish();
    return Closed{ok, r->framesWritten(), r->framesDropped()};
  });
}

void OutputModule::stopRecording() {
  if (!recorder_) return;
  ctx_->params.publish("/record/status", "cerrando");
  closing_ = closeInBackground(std::move(recorder_));
  recorder_.reset();
}

namespace {
template <class T>
bool ready(const std::future<T>& f) {
  return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}
}  // namespace

void OutputModule::updateRecording(gfx::TargetId comp, double dt) {
  auto& p = ctx_->params;
  if (ready(opening_)) {
    try {
      recorder_ = opening_.get();
      recordClock_ = recordNext_ = 0;
      p.publish("/record/status", "grabando");
    } catch (const std::exception& e) {
      p.publish("/record/status", std::string("error: ") + e.what());
      p.set("/record/active", false);
    }
  }
  if (ready(closing_)) {
    const Closed c = closing_.get();
    p.publish("/record/frames", int64_t{c.written});
    p.publish("/record/dropped", int64_t{c.dropped});
    p.publish("/record/status", c.ok ? "parado" : "error al cerrar el archivo");
  }
  if (opening_.valid() || closing_.valid()) return;
  const bool want = p.getBool("/record/active");
  if (want && !recorder_) startRecording();
  if (!want && recorder_) stopRecording();
  if (!recorder_) return;
  recordClock_ += dt;
  if (recordClock_ < recordNext_) return;
  const double interval = 1.0 / std::max<int64_t>(1, p.getInt("/record/fps", 30));
  recordNext_ += interval * std::ceil((recordClock_ - recordNext_ + 1e-9) / interval);
  recorder_->addFrame(backend_->readback(comp, compW_, compH_), recordClock_);
  p.publish("/record/frames", int64_t{recorder_->framesWritten()});
  p.publish("/record/dropped", int64_t{recorder_->framesDropped()});
}

// Un fotograma por paso: la salida en directo no se detiene mientras se renderiza un clip.
void OutputModule::stepClipRender() {
  if (!render_) return;
  ClipRender& r = *render_;
  auto& p = ctx_->params;
  const auto finishJob = [&](bool ok, const std::string& status) {
    r.comp->previewRelease();
    r.comp->previewStop();
    p.publish("/record/status", status);
    ctx_->events.publish("clip.rendered", {{"path", r.path}, {"ok", ok}});
    render_.reset();
  };
  if (r.closing.valid()) {
    if (!ready(r.closing)) return;
    const bool ok = r.closing.get().ok;
    p.publish("/record/file", r.path);
    finishJob(ok, ok ? "parado" : "error al cerrar el archivo");
    return;
  }
  if (r.opening.valid()) {
    if (!ready(r.opening)) return;
    try {
      r.recorder = r.opening.get();
    } catch (const std::exception& e) {
      finishJob(false, std::string("error: ") + e.what());
      return;
    }
  }
  const double t = static_cast<double>(r.frame) / r.fps;
  r.comp->previewHold(t);
  const double got = r.comp->previewFrameTime();
  const bool ready = !r.comp->previewHasDuration() || (got >= 0 && std::abs(got - t) <= 1.5 / r.fps);
  if (!ready && ++r.waited < 300) return;
  r.waited = 0;
  if (auto t2 = renderer_->renderPreview(*backend_, compW_, compH_)) r.recorder->addFrame(backend_->readback(t2, compW_, compH_), t);
  ++r.frame;
  p.publish("/record/status", "renderizando " + std::to_string(r.frame * 100 / std::max(1, r.total)) + " %");
  if (r.frame >= r.total) r.closing = closeInBackground(std::move(r.recorder));
}

void OutputModule::sendMonitors(gfx::TargetId comp) {
  constexpr int kW = 480, kH = 270;
  auto send = [&](int id, gfx::TargetId t) {
    auto px = backend_->readback(t, kW, kH);
    // La composición es premultiplicada: sobre negro basta con opacar el alfa.
    for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;
    ctx_->monitors->send(id, kW, kH, px);
  };
  send(0, renderer_->scaleForMonitor(comp, kW, kH));
  if (auto preview = renderer_->renderPreview(*backend_, kW, kH)) send(1, preview);
}

gfx::TextureId OutputModule::sourceTexture(const std::string& type, int index, gfx::TargetId comp) {
  if (type == "layer") return renderer_->routedLayer(index);
  if (type == "group") return renderer_->routedGroup(index);
  if (type == "bus") return renderer_->routedBus(index);
  if (type == "preview") return previewTarget_ ? backend_->targetTexture(previewTarget_) : gfx::kNoTexture;
  return backend_->targetTexture(comp);
}

void OutputModule::tick(double dt) {
  time_ += dt;
  glfwPollEvents();
#if defined(__APPLE__)
  if (options_.hideDockIcon && !dockPolicyApplied_) {
    hideFromDock();
    dockPolicyApplied_ = true;
  }
#endif
  auto& p = ctx_->params;
  sinceDisplays_ += dt;
  if (sinceDisplays_ >= 1) {
    sinceDisplays_ = 0;
    const std::string before = p.getString("/output/displays");
    publishDisplays();
    if (p.getString("/output/displays") != before) dirty_ = true;  // pantalla conectada o quitada
  }
  if (dirty_) applyRouting();
  syncWindows();

  for (auto& [_, w] : windows_) {
    if (w.handle && glfwWindowShouldClose(w.handle)) {
      glfwSetWindowShouldClose(w.handle, GLFW_FALSE);
      if (options_.closeQuits) p.trigger("/engine/quit");
      else p.set("/output/visible", false);
    }
  }
  if (!renderer_) return;

  glfwMakeContextCurrent(main_);
  last_ = renderer_->renderFrame(*backend_, compW_, compH_);
  previewTarget_ = usePreview_ ? renderer_->renderPreview(*backend_, compW_, compH_) : gfx::kNoTarget;
  const gfx::TargetId comp = last_;
  rendered_ = screens_->render(setup_, [&](const std::string& type, int index) { return sourceTexture(type, index, comp); },
                               p.getBool("/output/testcard"), p.getBool("/output/identify"), time_);

  updateRecording(comp, dt);
  stepClipRender();

  sinceMonitor_ += dt;
  if (ctx_->monitors && ctx_->monitors->wanted() && sinceMonitor_ >= 1.0 / 25) {
    sinceMonitor_ = 0;
    sendMonitors(comp);
  }

  const double master = p.getFloat("/output/opacity", 1.0);
  bool first = true;
  for (auto& [id, w] : windows_) {
    auto it = rendered_.find(id);
    if (!w.handle || it == rendered_.end()) continue;
    int fw = 0, fh = 0;
    glfwGetFramebufferSize(w.handle, &fw, &fh);
    if (fw <= 0 || fh <= 0) continue;
    if (first) {
      fbW_ = fw;
      fbH_ = fh;
    }
    glfwMakeContextCurrent(w.handle);
    backend_->setPresentContext(id);
    gfx::DrawParams d;
    d.opacity = static_cast<float>(master);
    const double src = static_cast<double>(it->second.width) / it->second.height, dst = static_cast<double>(fw) / fh;
    if (src > dst) d.scaleY = static_cast<float>(dst / src);
    else d.scaleX = static_cast<float>(src / dst);
    backend_->present(backend_->targetTexture(it->second.target), fw, fh, d);
    glfwSwapBuffers(w.handle);
    first = false;
  }
  glfwMakeContextCurrent(main_);
  if (windows_.empty()) {
    fbW_ = compW_;
    fbH_ = compH_;
  }
  p.publish("/output/width", int64_t{fbW_});
  p.publish("/output/height", int64_t{fbH_});
}

bool OutputModule::writeSnapshot(const std::string& path) {
  if (!last_) return false;
  const auto px = backend_->readback(last_, compW_, compH_);
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  std::fprintf(f, "P6\n%d %d\n255\n", compW_, compH_);
  for (size_t i = 0; i + 3 < px.size(); i += 4) std::fwrite(&px[i], 1, 3, f);
  std::fclose(f);
  return true;
}

}  // namespace vj::output
