#include "ffgl/FfglPlugin.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>

#include "core/generated/Effects.gen.h"

using nlohmann::json;
namespace fs = std::filesystem;

extern char** environ;

namespace vj::ffgl {
namespace {

abi::Mixed call(abi::MainFn fn, uint32_t code, uint32_t value = 0, abi::InstanceId id = nullptr) {
  abi::Mixed in;
  in.UIntValue = value;
  return fn(code, in, id);
}

abi::Mixed callPtr(abi::MainFn fn, uint32_t code, void* value, abi::InstanceId id = nullptr) {
  abi::Mixed in;
  in.PointerValue = value;
  return fn(code, in, id);
}

float asFloat(uint32_t bits) {
  float f;
  std::memcpy(&f, &bits, sizeof f);
  return f;
}

// Un bundle de macOS guarda el binario en Contents/MacOS.
std::string binaryOf(const std::string& path) {
  if (!fs::is_directory(path)) return path;
  const fs::path dir = fs::path(path) / "Contents" / "MacOS";
  std::error_code ec;
  for (const auto& f : fs::directory_iterator(dir, ec)) return f.path().string();
  throw std::runtime_error("bundle sin binario");
}

void* open(const std::string& path, abi::MainFn& fn) {
  void* handle = dlopen(binaryOf(path).c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!handle) throw std::runtime_error(std::string("no se pudo cargar: ") + dlerror());
  fn = reinterpret_cast<abi::MainFn>(dlsym(handle, "plugMain"));
  if (!fn) {
    dlclose(handle);
    throw std::runtime_error("no es un plugin FFGL");
  }
  return handle;
}

std::string text(const char* s, size_t max) {
  return std::string(s, strnlen(s, max));
}

}  // namespace

json probePlugin(const std::string& path) {
  abi::MainFn fn = nullptr;
  void* handle = open(path, fn);
  if (call(fn, abi::kInitialise).UIntValue != abi::kSuccess) throw std::runtime_error("el plugin no se inicializó");
  const auto* info = static_cast<const abi::PluginInfo*>(call(fn, abi::kGetInfo).PointerValue);
  if (!info) throw std::runtime_error("el plugin no se describe");
  if (info->APIMajorVersion < 2) throw std::runtime_error("versión de FFGL no admitida");

  json out = {{"id", text(info->PluginUniqueID, 4)}, {"name", text(info->PluginName, 16)}, {"type", info->PluginType}};
  json params = json::array();
  const uint32_t count = call(fn, abi::kGetNumParameters).UIntValue;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t type = call(fn, abi::kGetParameterType, i).UIntValue;
    const char* name = static_cast<const char*>(call(fn, abi::kGetParameterName, i).PointerValue);
    json p = {{"index", i}, {"name", name ? text(name, 16) : "Parámetro"}};
    if (type == abi::kTypeText || type == abi::kTypeFile || type == abi::kTypeBuffer) {
      p["kind"] = "unsupported";
    } else {
      p["kind"] = type == abi::kTypeBoolean || type == abi::kTypeEvent ? "bool" : "float";
      p["default"] = asFloat(call(fn, abi::kGetParameterDefault, i).UIntValue);
      abi::GetRange range{i, {0, 1}};
      if (callPtr(fn, abi::kGetRange, &range).UIntValue != abi::kSuccess || range.range.max <= range.range.min) range.range = {0, 1};
      p["min"] = range.range.min;
      p["max"] = range.range.max;
    }
    params.push_back(std::move(p));
  }
  out["params"] = std::move(params);
  call(fn, abi::kDeinitialise);
  dlclose(handle);
  return out;
}

json probeInChild(const std::string& exe, const std::string& path, int timeoutMs) {
  int fds[2];
  if (pipe(fds) != 0) throw std::runtime_error("no se pudo crear la tubería");
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
  posix_spawn_file_actions_addclose(&actions, fds[0]);
  const char* argv[] = {exe.c_str(), "--ffgl-probe", path.c_str(), nullptr};
  pid_t pid = 0;
  const int rc = posix_spawn(&pid, exe.c_str(), &actions, nullptr, const_cast<char* const*>(argv), environ);
  posix_spawn_file_actions_destroy(&actions);
  close(fds[1]);
  if (rc != 0) {
    close(fds[0]);
    throw std::runtime_error("no se pudo lanzar la comprobación");
  }
  std::string out;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  bool timedOut = false;
  for (;;) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
    if (left <= 0) {
      timedOut = true;
      break;
    }
    pollfd p{fds[0], POLLIN, 0};
    if (poll(&p, 1, static_cast<int>(left)) <= 0) continue;
    char buf[4096];
    const ssize_t n = read(fds[0], buf, sizeof buf);
    if (n <= 0) break;
    out.append(buf, static_cast<size_t>(n));
  }
  close(fds[0]);
  if (timedOut) kill(pid, SIGKILL);
  int status = 0;
  waitpid(pid, &status, 0);
  if (timedOut) throw std::runtime_error("el plugin no respondió");
  if (!WIFEXITED(status)) throw std::runtime_error("el plugin se cerró de forma inesperada");
  json doc = json::parse(out, nullptr, false);
  if (doc.is_discarded() || !doc.is_object()) throw std::runtime_error("el plugin se cerró de forma inesperada");
  if (doc.contains("error")) throw std::runtime_error(doc["error"].get<std::string>());
  return doc;
}

PluginRuntime::~PluginRuntime() = default;

void PluginRuntime::add(const std::string& typeId, const std::string& path) {
  libraries_[typeId].path = path;
}

PluginRuntime::Library* PluginRuntime::load(const std::string& typeId) {
  auto it = libraries_.find(typeId);
  if (it == libraries_.end() || it->second.failed) return nullptr;
  Library& lib = it->second;
  if (lib.initialised) return &lib;
  try {
    lib.handle = open(lib.path, lib.main);
    if (call(lib.main, abi::kInitialise).UIntValue != abi::kSuccess) throw std::runtime_error("no se inicializó");
    const auto* info = static_cast<const abi::PluginInfo*>(call(lib.main, abi::kGetInfo).PointerValue);
    lib.type = info->PluginType;
    lib.wantsTime = call(lib.main, abi::kGetPluginCaps, abi::kCapSetTime).UIntValue == 1;
    abi::SetHostInfo host{"VJ Studio", "0.4"};
    callPtr(lib.main, abi::kSetHostInfo, &host);
    lib.initialised = true;
    return &lib;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "vjengine: FFGL %s: %s\n", lib.path.c_str(), e.what());
    lib.failed = true;
    return nullptr;
  }
}

bool PluginRuntime::render(const compositor::PlanEffect& effect, gfx::IRenderBackend& gpu, gfx::TargetId input, gfx::TargetId output,
                           int width, int height, float time, float bpm, float beat) {
  Library* lib = load(effect.type->id);
  if (!lib) return false;
  bool ok = true;
  gpu.runExternal(output, input, [&](uint32_t inputTexture, uint32_t fbo) {
    const std::string key = effect.instance + "@" + std::to_string(width) + "x" + std::to_string(height);
    Instance& inst = instances_[key];
    abi::Viewport vp{0, 0, static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
    if (!inst.id) {
      inst.library = lib;
      inst.width = width;
      inst.height = height;
      inst.id = callPtr(lib->main, abi::kInstantiateGL, &vp).PointerValue;
      if (!inst.id || reinterpret_cast<uintptr_t>(inst.id) == abi::kFail) {
        inst.id = nullptr;
        lib->failed = true;
        ok = false;
        return;
      }
      call(lib->main, abi::kConnect, 0, inst.id);
    }
    inst.used = true;

    for (size_t i = 0; i < effect.extra.size(); ++i) {
      const float v = effect.extra[i].value;
      if (i < inst.values.size() && inst.values[i] == v) continue;
      abi::SetParameter set{};
      set.ParameterNumber = effect.extra[i].index;
      std::memcpy(&set.NewParameterValue.UIntValue, &v, sizeof v);
      callPtr(lib->main, abi::kSetParameter, &set, inst.id);
    }
    inst.values.resize(effect.extra.size());
    for (size_t i = 0; i < effect.extra.size(); ++i) inst.values[i] = effect.extra[i].value;

    if (lib->wantsTime) {
      double t = time;
      callPtr(lib->main, abi::kSetTime, &t, inst.id);
    }
    abi::SetBeatInfo beatInfo{bpm, beat / 4.f - static_cast<float>(static_cast<int>(beat / 4.f))};
    callPtr(lib->main, abi::kSetBeatInfo, &beatInfo, inst.id);

    abi::Texture tex{static_cast<uint32_t>(width), static_cast<uint32_t>(height), static_cast<uint32_t>(width), static_cast<uint32_t>(height), inputTexture};
    abi::Texture* inputs[1] = {&tex};
    abi::ProcessOpenGL process{lib->type == abi::kSource ? 0u : 1u, inputs, fbo};
    ok = callPtr(lib->main, abi::kProcessOpenGL, &process, inst.id).UIntValue == abi::kSuccess;
  });
  return ok;
}

void PluginRuntime::destroy(Instance& inst) {
  if (!inst.id) return;
  call(inst.library->main, abi::kDisconnect, 0, inst.id);
  call(inst.library->main, abi::kDeinstantiateGL, 0, inst.id);
  inst.id = nullptr;
}

void PluginRuntime::endFrame() {
  // Un efecto quitado de su cadena deja de pedirse: se libera aquí, con el contexto activo.
  for (auto it = instances_.begin(); it != instances_.end();) {
    if (it->second.used) {
      it->second.used = false;
      ++it;
    } else {
      destroy(it->second);
      it = instances_.erase(it);
    }
  }
}

void PluginRuntime::releaseGpu() {
  for (auto& [_, inst] : instances_) destroy(inst);
  instances_.clear();
  for (auto& [_, lib] : libraries_) {
    if (lib.initialised) call(lib.main, abi::kDeinitialise);
    lib.initialised = false;
  }
}

}  // namespace vj::ffgl
