#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#include "animation/Animator.h"
#include "compositor/AudioModule.h"
#include "compositor/CompositionModule.h"
#include "control/ControlModule.h"
#include "control/OscModule.h"
#include "core/CommandBus.h"
#include "core/EventBus.h"
#include "core/ModuleRegistry.h"
#include "core/ParameterRegistry.h"
#include "core/generated/Parameters.gen.h"
#if !defined(_WIN32)
#include "ffgl/FfglModule.h"
#endif
#include "core/SystemStats.h"
#include "dmx/DmxModule.h"
#include "output/OutputModule.h"
#include "patch/PatchModule.h"
#include "project/ProjectModule.h"
#include "shortcuts/ShortcutModule.h"

using namespace vj;

namespace {

std::atomic<bool> gQuit{false};
constexpr int kDefaultPort = 7777;
constexpr double kMinFrameTime = 1.0 / 120.0;

void usage() {
  std::puts(
      "vjengine — motor de VJ Studio\n"
      "  --port N            puerto de la API (por defecto 7777; 0 = libre)\n"
      "  --bind HOST         dirección de escucha (por defecto 127.0.0.1)\n"
      "  --headless          sin ventana ni GPU\n"
      "  --hidden            renderiza con la ventana oculta\n"
      "  --agent             lanzado por la interfaz: sin Dock; cerrar la ventana la oculta\n"
      "  --layers N          capas de la composición (4)\n"
      "  --columns N         columnas de la composición (8)\n"
      "  --opacity V         opacidad inicial de salida\n"
      "  --no-audio          sin dispositivo de audio\n"
      "  --documents DIR     carpeta de documentos (también VJ_DOCUMENTS)\n"
      "  --snapshot RUTA     guarda un PPM de la salida al terminar\n"
      "  --exit-after N      termina tras N fotogramas");
}

}  // namespace

int main(int argc, char** argv) {
#if !defined(_WIN32)
  // Modo interno: describe un plugin y termina. Si el plugin se cuelga, solo cae este proceso.
  if (argc == 3 && std::string(argv[1]) == "--ffgl-probe") {
    try {
      std::puts(ffgl::probePlugin(argv[2]).dump().c_str());
    } catch (const std::exception& e) {
      std::puts(nlohmann::json{{"error", e.what()}}.dump().c_str());
    }
    std::fflush(stdout);
    std::_Exit(0);
  }
#endif

  int port = kDefaultPort, exitAfter = 0;
  std::string bind = "127.0.0.1", snapshot, documents;
  double opacity = -1;
  compositor::CompositionShape shape;
  bool headless = false, audioDevice = true;
  output::OutputOptions outOpts;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    const auto next = [&]() -> const char* {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "falta el valor de %s\n", a.c_str());
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "--port") port = std::atoi(next());
    else if (a == "--bind") bind = next();
    else if (a == "--headless") headless = true;
    else if (a == "--hidden") outOpts.hidden = true;
    else if (a == "--agent") { outOpts.hideDockIcon = true; outOpts.closeQuits = false; }
    else if (a == "--layers") shape.layers = std::max(1, std::atoi(next()));
    else if (a == "--columns") shape.columns = std::max(1, std::atoi(next()));
    else if (a == "--documents") documents = next();
    else if (a == "--no-audio") audioDevice = false;
    else if (a == "--opacity") opacity = std::atof(next());
    else if (a == "--snapshot") snapshot = next();
    else if (a == "--exit-after") exitAfter = std::atoi(next());
    else if (a == "--help") { usage(); return 0; }
    else { std::fprintf(stderr, "opción desconocida: %s\n", a.c_str()); usage(); return 2; }
  }

  if (documents.empty())
    if (const char* env = std::getenv("VJ_DOCUMENTS")) documents = env;
#if defined(_WIN32)
  const char* home = std::getenv("USERPROFILE");
#else
  const char* home = std::getenv("HOME");
#endif
  if (documents.empty()) documents = std::string(home ? home : ".") + "/Documents/VJ Studio";

  ParameterRegistry params;
  EventBus events;
  ModuleRegistry modules;
  CommandBus commands;
  MonitorBus monitors;
  EngineContext ctx{params, events, modules, &commands, &monitors};
  params.defineAll(schema::parameters());
  params.addListener("/engine/quit", [](const ParamDesc&, const Value&) { gQuit = true; });
  std::signal(SIGINT, [](int) { gQuit = true; });
  std::signal(SIGTERM, [](int) { gQuit = true; });

  // Cada hito registra aquí sus módulos, en orden de dependencia.
  modules.add(std::make_unique<compositor::CompositionModule>(shape));
  modules.add(std::make_unique<animation::Animator>());
  modules.add(std::make_unique<project::ProjectModule>(documents));
  modules.add(std::make_unique<compositor::AudioModule>(audioDevice));
#if !defined(_WIN32)
  modules.add(std::make_unique<ffgl::FfglModule>(documents, argv[0]));
#endif
  modules.add(std::make_unique<patch::PatchModule>(documents));
  outOpts.documents = documents;
  if (!headless) modules.add(std::make_unique<output::OutputModule>(outOpts));
  modules.add(std::make_unique<dmx::DmxModule>(documents));
  modules.add(std::make_unique<control::ControlModule>(bind, port));
  modules.add(std::make_unique<control::OscModule>());
  modules.add(std::make_unique<shortcuts::ShortcutModule>(documents));

  if (const std::string failed = modules.startAll(ctx); !failed.empty()) {
    std::fprintf(stderr, "vjengine: falló el módulo %s\n", failed.c_str());
    return 1;
  }
  auto* projectModule = modules.get<project::ProjectModule>("project");
  modules.get<animation::Animator>("animator")->setLevels(modules.get<compositor::AudioModule>("audio"));
  modules.get<control::ControlModule>("control")->setHistoryHook(
      [projectModule](const std::string& path, const Value& before, const Value& after) { projectModule->recordSet(path, before, after); });
  if (opacity >= 0) params.set("/output/opacity", opacity);

  // La interfaz espera esta línea para saber el puerto real.
  std::printf("VJENGINE_READY port=%d backend=%s\n", modules.get<control::ControlModule>("control")->port(),
              params.getString("/engine/backend").c_str());
  std::fflush(stdout);

  using Clock = std::chrono::steady_clock;
  const auto startTime = Clock::now();
  auto last = startTime, fpsMark = startTime;
  int64_t frames = 0, fpsFrames = 0;
  SystemSampler sampler;
  while (!gQuit) {
    const auto now = Clock::now();
    const double dt = std::chrono::duration<double>(now - last).count();
    last = now;
    commands.pump();
    modules.tickAll(dt);
    ++frames;
    ++fpsFrames;
    const double fpsWindow = std::chrono::duration<double>(now - fpsMark).count();
    if (fpsWindow >= 0.5) {
      params.publish("/engine/fps", fpsFrames / fpsWindow);
      const SystemStats stats = sampler.sample();
      params.publish("/engine/cpu", stats.cpu);
      params.publish("/engine/memory", stats.memoryMb);
      params.publish("/engine/gpu", stats.gpu);
      params.publish("/engine/frames", frames);
      params.publish("/engine/uptime", std::chrono::duration<double>(now - startTime).count());
      fpsMark = now;
      fpsFrames = 0;
    }
    if (exitAfter > 0 && frames >= exitAfter) break;
    // El vsync marca el ritmo con la ventana visible; este límite cubre el resto.
    const double spent = std::chrono::duration<double>(Clock::now() - now).count();
    if (spent < kMinFrameTime) std::this_thread::sleep_for(std::chrono::duration<double>(kMinFrameTime - spent));
  }

  if (!snapshot.empty())
    if (auto* out = modules.get<output::OutputModule>("output"); !out || !out->writeSnapshot(snapshot)) {
      std::fprintf(stderr, "vjengine: no se pudo guardar %s\n", snapshot.c_str());
      modules.stopAll();
      return 1;
    }
  commands.close();
  modules.stopAll();
  return 0;
}
