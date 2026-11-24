#include "ffgl/FfglModule.h"

#include <filesystem>
#include <sstream>

#include "compositor/CompositionModule.h"
#include "core/CommandBus.h"
#include "core/ModuleRegistry.h"
#include "core/ParameterRegistry.h"
#include "core/generated/Effects.gen.h"

using nlohmann::json;
namespace fs = std::filesystem;

namespace vj::ffgl {
namespace {
constexpr int kProbeTimeoutMs = 5000;
}

bool FfglModule::start(EngineContext& ctx) {
  ctx_ = &ctx;
  module_ = ctx.modules.get<compositor::CompositionModule>("composition");
  if (!module_) return false;
  runtime_ = std::make_unique<PluginRuntime>();
  module_->setExternal(runtime_.get());
  if (ctx.commands) ctx.commands->add("ffgl.rescan", [this](const json&) { return scan(); });
  scan();
  return true;
}

void FfglModule::stop() {
  // La salida ya liberó las instancias con el contexto activo.
  runtime_.reset();
}

json FfglModule::scan() {
  json errors = json::array();
  scanFolder((fs::path(documents_) / "Extra Effects").string(), errors);
  std::stringstream folders(ctx_->params.getString("/preferences/ffglfolders"));
  std::string folder;
  while (std::getline(folders, folder, ':'))
    if (!folder.empty()) scanFolder(folder, errors);
  return errors;
}

void FfglModule::scanFolder(const std::string& folder, json& errors) {
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(folder, ec)) {
    const auto ext = entry.path().extension();
    if (ext != ".bundle" && ext != ".dylib" && ext != ".so") continue;
    const std::string path = entry.path().string();
    try {
      const json info = exe_.empty() ? probePlugin(path) : probeInChild(exe_, path, kProbeTimeoutMs);
      const uint32_t type = info.at("type");
      if (type == abi::kMixer) continue;  // los mezcladores FFGL necesitan dos entradas: pendiente
      schema::EffectType t;
      t.id = "ffgl:" + info.at("id").get<std::string>();
      t.name = info.at("name");
      t.category = "FFGL";
      t.source = type == abi::kSource;
      t.glsl = "//FFGL\n";
      for (const auto& p : info.at("params")) {
        if (p.at("kind") == "unsupported") continue;
        // El índice viaja en el nombre del parámetro: los del plugin no tienen por qué ser únicos.
        const std::string name = "p" + std::to_string(p.at("index").get<int>());
        t.params.push_back({name, p.at("name"), p.at("kind"), p.value("min", 0.0), p.value("max", 1.0), p.value("default", 0.0), ""});
      }
      runtime_->add(t.id, path);
      module_->composition()->effects().catalog().addExternal(std::move(t));
    } catch (const std::exception& e) {
      errors.push_back({{"file", entry.path().filename().string()}, {"error", e.what()}});
    }
  }
}

}  // namespace vj::ffgl
