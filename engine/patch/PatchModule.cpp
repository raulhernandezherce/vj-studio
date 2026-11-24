#include "patch/PatchModule.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

#include "core/CommandBus.h"
#include "core/EventBus.h"
#include "core/ModuleRegistry.h"

using nlohmann::json;
namespace fs = std::filesystem;

namespace vj::patch {
namespace {

constexpr const char* kExt = ".vjpatch";
constexpr int kMonitor = 2, kPreviewW = 480, kPreviewH = 270;

std::string slug(const std::string& name) {
  if (name.empty() || name.find_first_of("/\\") != std::string::npos || name[0] == '.') throw std::runtime_error("nombre de parche no válido");
  return name;
}

json readJson(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("no se pudo abrir el parche");
  return json::parse(in);
}

void writeJson(const std::string& path, const json& j) {
  // Primero a un temporal: un fallo a medias no destruye el parche anterior.
  const std::string tmp = path + ".tmp";
  {
    std::ofstream out(tmp);
    out << j.dump(2);
    if (!out) throw std::runtime_error("no se pudo guardar el parche");
  }
  fs::rename(tmp, path);
}

}  // namespace

std::string PatchModule::folder() const { return (fs::path(documents_) / "Patches").string(); }
std::string PatchModule::fileFor(const std::string& name) const { return (fs::path(folder()) / (slug(name) + kExt)).string(); }

Programs& PatchModule::programs(gfx::IRenderBackend& gpu) {
  if (!programs_ || gpu_ != &gpu) {
    programs_ = std::make_unique<Programs>(gpu);
    gpu_ = &gpu;
  }
  return *programs_;
}

void PatchModule::registerPatch(const Patch& patch, const std::string& dir) {
  if (patch.category != "effect" && patch.category != "source") return;
  auto exposed = exposedParams(patch, *library_);
  if (exposed.size() > 12) throw std::runtime_error("un parche admite 12 parámetros expuestos");
  Saved s{patch, dir, {}, exposed};
  s.type.id = "patch:" + patch.name;
  s.type.name = patch.name;
  s.type.category = "Parches";
  s.type.source = patch.category == "source";
  s.type.glsl = "//PATCH";
  for (const Exposed& e : exposed) s.type.params.push_back(e.param);
  module_->composition()->effects().catalog().addExternal(s.type);
  saved_[patch.name] = std::move(s);
}

void PatchModule::scanFolder() {
  std::error_code ec;
  fs::create_directories(folder(), ec);
  const auto load = [&](const fs::path& path) {
    try {
      registerPatch(fromJson(readJson(path.string()), *library_), path.parent_path().string());
    } catch (const std::exception& e) {
      ctx_->events.publish("patch.error", {{"path", path.string()}, {"error", e.what()}});
    }
  };
  for (const auto& entry : fs::directory_iterator(folder(), ec)) {
    if (entry.path().extension() == kExt) load(entry.path());
    if (entry.is_directory())
      for (const auto& inner : fs::directory_iterator(entry.path(), ec))
        if (inner.path().extension() == kExt) load(inner.path());
  }
}

json PatchModule::consolidate(const std::string& name) {
  const Patch original = fromJson(readJson(fileFor(name)), *library_);
  Patch p = original;
  const fs::path dir = fs::path(folder()) / slug(name);
  fs::create_directories(dir);
  int copied = 0;
  for (Node& n : p.nodes) {
    if (n.type != "tex.media") continue;
    const std::string path = n.attrs.value("path", "");
    if (path.empty() || path.rfind("vj://", 0) == 0) continue;
    const fs::path from = fs::path(path).is_absolute() ? fs::path(path) : fs::path(folder()) / path;
    if (!fs::exists(from)) throw std::runtime_error("falta el archivo " + path);
    fs::copy_file(from, dir / from.filename(), fs::copy_options::overwrite_existing);
    n.attrs["path"] = from.filename().string();
    ++copied;
  }
  const std::string file = (dir / (slug(name) + kExt)).string();
  writeJson(file, toJson(p));
  return {{"folder", dir.string()}, {"file", file}, {"copied", copied}};
}

bool PatchModule::start(EngineContext& ctx) {
  ctx_ = &ctx;
  module_ = ctx.modules.get<compositor::CompositionModule>("composition");
  if (!module_) return false;
  library_ = std::make_unique<NodeLibrary>(module_->composition()->effects().catalog());
  module_->setPatches(this);
  scanFolder();
  if (!ctx.commands) return true;
  auto& bus = *ctx.commands;
  bus.add("patch.catalog", [this](const json&) { return library_->describe(); });
  bus.add("patch.get", [this](const json&) { return toJson(working_); });
  bus.add("patch.set", [this](const json& a) {
    std::vector<std::string> warnings;
    Patch p = fromJson(a.at("patch"), *library_, &warnings);
    validate(p, *library_);  // lo incorrecto se rechaza entero: el parche en marcha sigue intacto
    working_ = std::move(p);
    workingDirty_ = true;
    previewNode_ = a.value("preview", 0);
    return json{{"warnings", warnings}};
  });
  bus.add("patch.status", [this](const json&) {
    json errors = json::object();
    if (workingRuntime_)
      for (const auto& [id, message] : workingRuntime_->errors()) errors[std::to_string(id)] = message;
    return json{{"errors", errors}};
  });
  bus.add("patch.list", [this](const json&) {
    json out = json::array();
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(folder(), ec))
      if (entry.path().extension() == kExt) out.push_back(entry.path().stem().string());
    std::sort(out.begin(), out.end());
    return out;
  });
  bus.add("patch.save", [this](const json& a) {
    Patch p = working_;
    p.name = a.value("name", p.name);
    p.category = a.value("category", p.category);
    validate(p, *library_);
    const std::string file = fileFor(p.name);
    fs::create_directories(folder());
    registerPatch(p, folder());  // antes de escribir: si no cabe en el catálogo, no se guarda
    writeJson(file, toJson(p));
    working_ = p;
    return json{{"file", file}};
  });
  bus.add("patch.load", [this](const json& a) {
    std::vector<std::string> warnings;
    Patch p = fromJson(readJson(fileFor(a.at("name").get<std::string>())), *library_, &warnings);
    validate(p, *library_);
    working_ = std::move(p);
    workingDirty_ = true;
    return json{{"patch", toJson(working_)}, {"warnings", warnings}};
  });
  bus.add("patch.consolidate", [this](const json& a) { return consolidate(a.at("name").get<std::string>()); });
  return true;
}

void PatchModule::tick(double dt) {
  time_ += dt;
  auto* output = ctx_->modules.get<output::OutputModule>("output");
  if (!output || !output->backend() || !ctx_->monitors || !ctx_->monitors->wanted() || working_.nodes.empty()) return;
  sinceMonitor_ += dt;
  auto& gpu = *output->backend();
  if (!workingRuntime_) workingRuntime_ = std::make_unique<Runtime>(*library_, ctx_->params, folder());
  if (workingDirty_) {
    workingRuntime_->update(working_, gpu);
    workingDirty_ = false;
  }
  FrameInfo f;
  f.input = output->compositionTexture();
  f.width = kPreviewW * 2;
  f.height = kPreviewH * 2;
  f.time = time_;
  f.dt = dt;
  f.bpm = ctx_->params.getFloat("/composition/tempo/bpm", 120);
  f.beat = time_ * f.bpm / 60;
  gfx::TextureId texture = workingRuntime_->run(gpu, programs(gpu), f);
  if (previewNode_) texture = workingRuntime_->nodeTexture(previewNode_);
  if (!previewTarget_) previewTarget_ = gpu.createTarget(kPreviewW, kPreviewH);
  if (sinceMonitor_ < 1.0 / 20) return;
  sinceMonitor_ = 0;
  gpu.beginPass(previewTarget_, true);
  if (texture) {
    gfx::DrawCall d;
    d.program = programs_->copy;
    d.textures[0] = texture;
    gpu.draw(d);
  }
  auto px = gpu.readback(previewTarget_, kPreviewW, kPreviewH);
  for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;
  ctx_->monitors->send(kMonitor, kPreviewW, kPreviewH, px);
}

bool PatchModule::render(const compositor::PlanEffect& e, gfx::IRenderBackend& gpu, gfx::TargetId input, gfx::TargetId output, int width, int height, float time, float bpm, float beat) {
  const auto saved = saved_.find(e.type->name);
  if (saved == saved_.end()) return false;
  Instance& inst = instances_[e.instance];
  if (!inst.runtime) {
    inst.runtime = std::make_unique<Runtime>(*library_, ctx_->params, saved->second.dir);
    inst.runtime->update(saved->second.patch, gpu);
  }
  inst.used = true;
  std::map<int, float> overrides;
  for (size_t i = 0; i < saved->second.exposed.size() && i < e.values.size(); ++i) overrides[saved->second.exposed[i].node] = e.values[i];
  FrameInfo f;
  f.input = gpu.targetTexture(input);
  f.width = width;
  f.height = height;
  f.time = time;
  f.bpm = bpm;
  f.beat = beat;
  f.dt = 1.0 / 60;
  auto& prog = programs(gpu);
  const gfx::TextureId result = inst.runtime->run(gpu, prog, f, overrides);
  if (!result) return false;
  gpu.beginPass(output, true);
  gfx::DrawCall d;
  d.program = prog.copy;
  d.textures[0] = result;
  gpu.draw(d);
  return true;
}

void PatchModule::endFrame() {
  // La instancia que deja de dibujarse suelta sus destinos; volverá a crearlos si reaparece.
  for (auto it = instances_.begin(); it != instances_.end();) {
    if (it->second.used) {
      it->second.used = false;
      ++it;
    } else {
      if (gpu_) it->second.runtime->release(*gpu_);
      it = instances_.erase(it);
    }
  }
}

void PatchModule::releaseGpu() {
  if (gpu_) {
    for (auto& [_, inst] : instances_) inst.runtime->release(*gpu_);
    if (workingRuntime_) workingRuntime_->release(*gpu_);
    if (previewTarget_) gpu_->destroyTarget(previewTarget_);
  }
  previewTarget_ = gfx::kNoTarget;
  instances_.clear();
  workingRuntime_.reset();
  workingDirty_ = true;
  programs_.reset();
  gpu_ = nullptr;
}

void PatchModule::stop() {
  if (module_) module_->setPatches(nullptr);
}

}  // namespace vj::patch
