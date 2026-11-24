#include "patch/Runtime.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

#include "compositor/Renderer.h"
#include "compositor/Transform.h"
#include "core/generated/BlendModes.gen.h"

using nlohmann::json;
namespace fs = std::filesystem;

namespace vj::patch {
namespace {

const char* kSolidFs = "void main() { color = uParams[0]; }\n";
const char* kMixerFs = "void main() { color = mix(texture(uTex0, vPos), texture(uTex1, vPos), uParams[0].x); }\n";
const char* kShapeFs = R"(#version 410 core
uniform vec4 uParams[4];
out vec4 color;
void main() { color = uParams[0]; }
)";

int optionIndex(const NodeType& type, const char* attr, const std::string& value) {
  for (const Attr& a : type.attrs)
    if (a.name == attr)
      for (size_t i = 0; i < a.options.size(); ++i)
        if (a.options[i] == value) return static_cast<int>(i);
  return 0;
}

std::string attrString(const NodeType& type, const Node& node, const char* name) {
  if (node.attrs.contains(name) && node.attrs[name].is_string()) return node.attrs[name];
  for (const Attr& a : type.attrs)
    if (a.name == name && a.def.is_string()) return a.def;
  return "";
}

Value inputDefault(const Port& p, const json& values) {
  Value v;
  const json* given = values.contains(p.name) ? &values[p.name] : nullptr;
  if (p.type == PortType::Color) {
    v.c = {1, 1, 1, 1};
    if (given && given->is_array() && given->size() == 4)
      for (int i = 0; i < 4; ++i) v.c[i] = (*given)[i].get<float>();
  } else if (p.type == PortType::String) {
    v.s = given && given->is_string() ? given->get<std::string>() : p.text;
  } else if (p.type != PortType::Texture) {
    v.n = given && given->is_number() ? given->get<double>() : given && given->is_boolean() ? (given->get<bool>() ? 1 : 0) : p.def;
  }
  return v;
}

std::string asText(const Value& v, PortType from) {
  if (from == PortType::String) return v.s;
  if (from == PortType::Color) return std::to_string(v.c[0]) + " " + std::to_string(v.c[1]) + " " + std::to_string(v.c[2]) + " " + std::to_string(v.c[3]);
  const double r = std::round(v.n);
  return std::fabs(r - v.n) < 1e-9 ? std::to_string(static_cast<long long>(r)) : std::to_string(v.n);
}

}  // namespace

Programs::Programs(gfx::IRenderBackend& gpu) : gpu_(gpu) {
  const std::string common = compositor::Renderer::commonShader();
  solid = gpu.createProgram(common + kSolidFs);
  mixer = gpu.createProgram(common + kMixerFs);
  copy = gpu.createProgram(common + "void main() { color = texture(uTex0, vPos); }\n");
  clip = gpu.createProgram(compositor::Renderer::clipShader());
  blend = gpu.createProgram(compositor::Renderer::blendShader());
  transition = gpu.createProgram(compositor::Renderer::transitionShader());
  shape = gpu.createMeshProgram(kShapeFs);
  const uint8_t none[4] = {0, 0, 0, 0};
  empty = gpu.uploadTexture(gfx::kNoTexture, 1, 1, gfx::PixelFormat::RGBA8, 4, none);
}

Programs::~Programs() { gpu_.destroyTexture(empty); }

gfx::ProgramId Programs::effect(const schema::EffectType& type, std::string& error) {
  const std::string key = type.id + "\n" + type.glsl;
  auto it = effects_.find(key);
  if (it != effects_.end()) return it->second;
  gfx::ProgramId id = 0;
  try {
    id = gpu_.createProgram(compositor::Renderer::effectShader(type));
  } catch (const std::exception& e) {
    error = e.what();
  }
  effects_[key] = id;
  return id;
}

void Runtime::freeTargets(gfx::IRenderBackend& gpu, State& s) {
  for (auto& t : s.targets) {
    if (t) gpu.destroyTarget(t);
    t = gfx::kNoTarget;
  }
}

void Runtime::update(const Patch& patch, gfx::IRenderBackend& gpu) {
  patch_ = patch;
  order_ = evaluationOrder(patch_);
  nodes_.clear();
  incoming_.clear();
  feedback_.clear();
  for (const Node& n : patch_.nodes) nodes_[n.id] = &n;
  for (const Link& l : patch_.links) incoming_[l.to].push_back(&l);
  for (const auto& [from, to] : order_.delayed) feedback_.insert(from);
  for (auto it = states_.begin(); it != states_.end();) {
    if (nodes_.count(it->first)) {
      ++it;
    } else {
      freeTargets(gpu, it->second);
      if (it->second.frameTexture) gpu.destroyTexture(it->second.frameTexture);
      it = states_.erase(it);
    }
  }
  errors_.clear();
}

gfx::TextureId Runtime::nodeTexture(int id) const {
  auto it = states_.find(id);
  return it == states_.end() || it->second.out.empty() ? gfx::kNoTexture : it->second.out[0].tex;
}

void Runtime::release(gfx::IRenderBackend& gpu) {
  for (auto& [_, s] : states_) {
    freeTargets(gpu, s);
    if (s.frameTexture) gpu.destroyTexture(s.frameTexture);
  }
  states_.clear();
}

gfx::TextureId Runtime::run(gfx::IRenderBackend& gpu, Programs& programs, const FrameInfo& frame, const std::map<int, float>& overrides) {
  gfx::TextureId result = gfx::kNoTexture;
  for (int id : order_.nodes) {
    const Node& node = *nodes_.at(id);
    const NodeType* type = lib_.find(node.type);
    if (!type) continue;  // nodo de una versión más nueva: se conserva sin ejecutarlo
    State& state = states_[id];
    state.out.resize(type->outputs.size());

    std::vector<Value> in;
    in.reserve(type->inputs.size());
    for (const Port& p : type->inputs) {
      Value v = inputDefault(p, node.values);
      if (const auto it = incoming_.find(id); it != incoming_.end()) {
        for (const Link* l : it->second) {
          if (l->toPort != p.name) continue;
          const NodeType* src = lib_.find(nodes_.at(l->from)->type);
          if (!src) continue;
          const auto port = std::find_if(src->outputs.begin(), src->outputs.end(), [&](const Port& q) { return q.name == l->fromPort; });
          auto& sourceState = states_[l->from];
          sourceState.out.resize(src->outputs.size());
          const auto index = static_cast<size_t>(port - src->outputs.begin());
          if (index >= sourceState.out.size()) continue;
          const Value& got = sourceState.out[index];
          // Un bucle lee aquí lo que dejó el fotograma anterior.
          v = got;
          if (p.type == PortType::String && port->type != PortType::String) v.s = asText(got, port->type);
          if (!std::isfinite(v.n)) v.n = 0;
        }
      }
      in.push_back(std::move(v));
    }
    if (const auto o = overrides.find(id); o != overrides.end() && !in.empty()) in[0].n = o->second;

    if (type->kind == Kind::Data) {
      std::string error;
      EvalContext c{in, state.out, state.memory, node.attrs, frame.time, frame.beat, frame.bpm, frame.dt, &params_, &error};
      type->eval(c);
      for (Value& v : state.out)
        if (!std::isfinite(v.n)) v.n = 0;
      if (!error.empty()) errors_[id] = error;
      continue;
    }
    if (type->kind == Kind::TextureIn) {
      state.out[0].tex = frame.input;
      continue;
    }
    if (type->kind == Kind::TextureOut) {
      result = in[0].tex;
      continue;
    }
    drawNode(gpu, programs, node, *type, state, in, frame);
  }
  return result;
}

void Runtime::drawNode(gfx::IRenderBackend& gpu, Programs& programs, const Node& node, const NodeType& type, State& s, const std::vector<Value>& in, const FrameInfo& frame) {
  const auto dim = [&](const char* name, int fallback) {
    const int v = node.attrs.value(name, 0);
    return v > 0 ? std::min(v, 8192) : fallback;
  };
  const int w = dim("width", frame.width), h = dim("height", frame.height);
  const bool deep = attrString(type, node, "depth") == "16";
  if (w != s.width || h != s.height || deep != s.deep) {
    freeTargets(gpu, s);
    s.width = w;
    s.height = h;
    s.deep = deep;
  }
  const bool loops = feedback_.count(node.id) != 0;
  s.current = loops ? 1 - s.current : 0;
  gfx::TargetId& target = s.targets[s.current];
  if (!target) {
    target = gpu.createTarget(w, h, deep);
    gpu.clearTarget(target, 0, 0, 0, 0);
  }

  const auto tex = [&](size_t i) { return i < in.size() && in[i].tex ? in[i].tex : programs.empty; };
  gfx::DrawCall d;
  d.params[12] = static_cast<float>(w);
  d.params[13] = static_cast<float>(h);
  d.params[14] = static_cast<float>(frame.time);
  d.params[15] = static_cast<float>(frame.beat);
  switch (type.kind) {
    case Kind::Effect: {
      std::string error;
      d.program = programs.effect(*type.effect, error);
      if (!d.program) {
        errors_[node.id] = error;
        break;
      }
      const bool source = type.effect->source;
      d.textures[0] = source ? programs.empty : tex(0);
      d.textures[1] = programs.empty;
      const size_t first = source ? 0 : 1;
      for (size_t i = first; i < in.size() && i - first < 12; ++i) d.params[i - first] = static_cast<float>(in[i].n);
      break;
    }
    case Kind::Isf: {
      const std::string code = attrString(type, node, "code");
      if (code != s.isfCode) {
        s.isfCode = code;
        s.isf.reset();
        errors_.erase(node.id);
        try {
          s.isf = std::make_unique<schema::EffectType>(compositor::parseIsf("patch-isf-" + std::to_string(node.id), code));
        } catch (const std::exception& e) {
          errors_[node.id] = e.what();
        }
      }
      if (s.isf) {
        std::string error;
        d.program = programs.effect(*s.isf, error);
        if (!d.program) errors_[node.id] = error;
        for (size_t i = 0; i < s.isf->params.size() && i < 12; ++i) d.params[i] = static_cast<float>(s.isf->params[i].defaultValue);
      }
      d.textures[0] = tex(0);
      d.textures[1] = programs.empty;
      break;
    }
    case Kind::Blend:
      d.program = programs.blend;
      d.textures[0] = tex(0);
      d.textures[1] = tex(1);
      d.params[0] = static_cast<float>(in[2].n);
      d.mode = optionIndex(type, "mode", attrString(type, node, "mode"));
      break;
    case Kind::Mixer:
    case Kind::Transition:
      d.program = type.kind == Kind::Mixer ? programs.mixer : programs.transition;
      d.textures[0] = tex(0);
      d.textures[1] = tex(1);
      d.params[0] = static_cast<float>(in[2].n);
      if (type.kind == Kind::Transition) d.mode = optionIndex(type, "mode", attrString(type, node, "mode"));
      break;
    case Kind::Solid:
      d.program = programs.solid;
      {
        const auto& c = in[0].c;
        d.params[0] = c[0] * c[3];
        d.params[1] = c[1] * c[3];
        d.params[2] = c[2] * c[3];
        d.params[3] = c[3];
      }
      break;
    case Kind::Shape: {
      const int count = std::clamp(node.attrs.value("instances", 1), 1, 100000);
      const int side = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count))));
      std::vector<float> v;
      v.reserve(static_cast<size_t>(count) * 36);
      const double size = in[1].n, turn = in[2].n * 6.283185307179586, spread = in[3].n;
      const double half = size / (count > 1 ? side : 1), aspect = static_cast<double>(h) / w, cs = std::cos(turn), sn = std::sin(turn);
      const auto corner = [&](double cx, double cy, double x, double y) {
        v.push_back(static_cast<float>(cx + (x * cs - y * sn) * aspect));
        v.push_back(static_cast<float>(cy + x * sn + y * cs));
        v.insert(v.end(), {0.f, 0.f, 0.f, 0.f});
      };
      for (int i = 0; i < count; ++i) {
        const double cx = count > 1 ? ((i % side + 0.5) / side * 2 - 1) * spread : 0;
        const double cy = count > 1 ? ((i / side + 0.5) / side * 2 - 1) * spread : 0;
        corner(cx, cy, -half, -half);
        corner(cx, cy, half, -half);
        corner(cx, cy, half, half);
        corner(cx, cy, -half, -half);
        corner(cx, cy, half, half);
        corner(cx, cy, -half, half);
      }
      gpu.beginPass(target, true);
      gfx::MeshCall m;
      m.program = programs.shape;
      m.vertices = v.data();
      m.vertexCount = static_cast<int>(v.size() / 6);
      const auto& c = in[0].c;
      m.params[0] = c[0] * c[3];
      m.params[1] = c[1] * c[3];
      m.params[2] = c[2] * c[3];
      m.params[3] = c[3];
      m.blendOver = true;
      gpu.drawMesh(m);
      s.out[0].tex = gpu.targetTexture(target);
      return;
    }
    case Kind::Media: {
      const std::string path = attrString(type, node, "path");
      if (path != s.mediaPath) {
        s.mediaPath = path;
        s.source.reset();
        s.hold.reset();
        s.lastFrame = nullptr;
        errors_.erase(node.id);
        if (!path.empty()) {
          // Abrir un archivo puede tardar: la salida sigue mientras tanto.
          const fs::path full = fs::path(path).is_absolute() || path.rfind("vj://", 0) == 0 ? fs::path(path) : fs::path(base_) / path;
          s.opening = std::async(std::launch::async, [p = full.string()] {
            std::string error;
            auto src = media::openMedia(p, error);
            if (!src) throw std::runtime_error(error);
            return src;
          });
        }
      }
      if (s.opening.valid() && s.opening.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
          s.source = s.opening.get();
        } catch (const std::exception& e) {
          errors_[node.id] = e.what();
        }
      }
      gpu.beginPass(target, true);
      s.out[0].tex = gpu.targetTexture(target);
      if (!s.source) return;
      const double length = s.source->info().duration;
      const auto f = s.source->frameAt(length > 0 ? std::fmod(frame.time, length) : 0);
      if (!f) return;
      if (f.get() != s.lastFrame) {
        s.frameTexture = gpu.uploadTexture(s.frameTexture, f->width, f->height, f->format, f->stride, f->pixels.data());
        s.lastFrame = f.get();
        s.hold = f;
      }
      d.program = programs.clip;
      d.textures[0] = s.frameTexture;
      d.matrix = compositor::transformMatrix({}, w, h, w, h);
      d.params[0] = d.params[1] = d.params[2] = d.params[3] = d.params[4] = 1;
      d.params[5] = 0;
      gpu.draw(d);
      return;
    }
    default:
      break;
  }
  if (!d.program) {
    // Un nodo que falla deja pasar lo que le llega: la salida en directo no se apaga.
    s.out[0].tex = tex(0);
    return;
  }
  gpu.beginPass(target, true);
  gpu.draw(d);
  s.out[0].tex = gpu.targetTexture(target);
}

}  // namespace vj::patch
