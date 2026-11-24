#include "compositor/EffectChains.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "core/ValueJson.h"
#include "core/generated/BlendModes.gen.h"

using nlohmann::json;
namespace fs = std::filesystem;

namespace vj::compositor {
namespace {

constexpr size_t kMaxFloats = 12;  // los otros 4 huecos de uParams son tamaño y reloj

std::vector<std::string> blendNames() {
  std::vector<std::string> out;
  for (const auto& b : schema::blendModes()) out.push_back(b.name);
  return out;
}

int blendIndex(const std::string& name) {
  const auto& t = schema::blendModes();
  for (size_t i = 0; i < t.size(); ++i)
    if (name == t[i].name) return static_cast<int>(i);
  return 1;
}

}  // namespace

EffectCatalog::EffectCatalog() {
  for (const auto& t : schema::effectTypes()) {
    types_[t.id] = t;
    order_.push_back(t.id);
  }
}

const schema::EffectType* EffectCatalog::find(const std::string& id) const {
  auto it = types_.find(id);
  return it == types_.end() ? nullptr : &it->second;
}

std::vector<schema::EffectType> EffectCatalog::list() const {
  std::vector<schema::EffectType> out;
  for (const auto& id : order_) out.push_back(types_.at(id));
  return out;
}

void EffectCatalog::addExternal(schema::EffectType type) {
  if (!types_.count(type.id)) order_.push_back(type.id);
  types_[type.id] = std::move(type);
}

json EffectCatalog::scanIsf(const std::string& folder) {
  json errors = json::array();
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(folder, ec)) {
    const auto ext = entry.path().extension();
    if (ext != ".fs" && ext != ".isf") continue;
    const std::string id = "isf:" + entry.path().stem().string();
    try {
      std::ifstream in(entry.path());
      std::stringstream text;
      text << in.rdbuf();
      schema::EffectType t = parseIsf(id, text.str());
      if (!types_.count(id)) order_.push_back(id);
      types_[id] = std::move(t);
    } catch (const std::exception& e) {
      errors.push_back({{"file", entry.path().filename().string()}, {"error", e.what()}});
    }
  }
  return errors;
}

schema::EffectType parseIsf(const std::string& id, const std::string& text) {
  const auto open = text.find("/*"), close = text.find("*/");
  if (open == std::string::npos || close == std::string::npos) throw std::runtime_error("falta la cabecera ISF");
  json head = json::parse(text.substr(open + 2, close - open - 2), nullptr, false);
  if (!head.is_object()) throw std::runtime_error("cabecera ISF no válida");
  if (head.contains("PASSES") && head["PASSES"].size() > 1) throw std::runtime_error("ISF de varias pasadas aún no admitido");

  schema::EffectType t;
  t.id = id;
  t.name = id.substr(4);
  t.category = "ISF";
  t.source = true;
  std::string defines;
  size_t slot = 0;
  const auto reserve = [&](size_t n) {
    if (slot + n > kMaxFloats) throw std::runtime_error("el shader tiene demasiadas entradas");
    const size_t at = slot;
    slot += n;
    return at;
  };
  const auto u = [](size_t i) { return "uParams[" + std::to_string(i / 4) + "][" + std::to_string(i % 4) + "]"; };
  const auto number = [](const json& j, const char* key, double fallback) {
    const auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<double>() : fallback;
  };

  for (const auto& in : head.value("INPUTS", json::array())) {
    const std::string name = in.value("NAME", ""), type = in.value("TYPE", "");
    const std::string label = in.value("LABEL", name);
    if (type == "image") {
      if (name == "inputImage") {
        t.source = false;
        defines += "#define inputImage uTex0\n";
      } else {
        throw std::runtime_error("entrada de imagen no admitida: " + name);
      }
    } else if (type == "float" || type == "long") {
      const size_t at = reserve(1);
      const double def = number(in, "DEFAULT", 0);
      t.params.push_back({name, label, "float", number(in, "MIN", 0), number(in, "MAX", 1), def, ""});
      defines += "#define " + name + (type == "long" ? " int(" + u(at) + ")\n" : " " + u(at) + "\n");
    } else if (type == "bool" || type == "event") {
      const size_t at = reserve(1);
      const auto def = in.find("DEFAULT");
      const double value = def == in.end() ? 0.0 : def->is_boolean() ? (def->get<bool>() ? 1.0 : 0.0) : def->is_number() ? def->get<double>() : 0.0;
      t.params.push_back({name, label, "bool", 0, 1, value, ""});
      defines += "#define " + name + " (" + u(at) + " > 0.5)\n";
    } else if (type == "color") {
      const size_t at = reserve(4);
      const json def = in.value("DEFAULT", json::array({0, 0, 0, 1}));
      static const char* suffix[] = {" R", " G", " B", " A"};
      for (size_t c = 0; c < 4; ++c)
        t.params.push_back({name + "_" + std::to_string(c), label + suffix[c], "float", 0, 1, def.size() > c ? def[c].get<double>() : 0.0, ""});
      defines += "#define " + name + " vec4(" + u(at) + ", " + u(at + 1) + ", " + u(at + 2) + ", " + u(at + 3) + ")\n";
    } else if (type == "point2D") {
      const size_t at = reserve(2);
      const json def = in.value("DEFAULT", json::array({0, 0}));
      t.params.push_back({name + "_x", label + " X", "float", number(in, "MIN", 0), 2000, def.size() > 0 ? def[0].get<double>() : 0.0, ""});
      t.params.push_back({name + "_y", label + " Y", "float", number(in, "MIN", 0), 2000, def.size() > 1 ? def[1].get<double>() : 0.0, ""});
      defines += "#define " + name + " vec2(" + u(at) + ", " + u(at + 1) + ")\n";
    } else {
      throw std::runtime_error("tipo de entrada no admitido: " + type);
    }
  }

  // Marca para Renderer: el cuerpo es un shader completo, no una función fx().
  t.glsl = "//ISF\n" + defines + text.substr(close + 2);
  return t;
}

std::string EffectChains::instancePath(const std::string& entity, int id) const {
  return entity + "/effects/e" + std::to_string(id);
}

void EffectChains::announce(const std::string& entity) { events_.publish("effects.changed", {{"entity", entity}}); }

int EffectChains::add(const std::string& entity, const std::string& type, int index, int id) {
  const schema::EffectType* t = catalog_.find(type);
  if (!t) throw std::runtime_error("efecto desconocido: " + type);
  if (id == 0) id = nextId_;
  nextId_ = std::max(nextId_, id + 1);
  const std::string prefix = instancePath(entity, id) + "/";

  const auto define = [&](const std::string& name, ParamType kind, Value def, double lo, double hi, const std::string& label,
                          std::vector<std::string> options = {}) {
    params_.define({prefix + name, kind, std::move(def), lo, hi, false, label, t->name, "", "M3", std::move(options)});
  };
  define("bypass", ParamType::Bool, Value{false}, 0, 0, "Bypass");
  define("opacity", ParamType::Float, Value{1.0}, 0, 1, "Opacidad");
  define("blend", ParamType::Enum, Value{std::string("Alpha")}, 0, 0, "Blend", blendNames());
  define("name", ParamType::String, Value{std::string{}}, 0, 0, "Nombre");
  for (const auto& p : t->params) {
    if (p.type == "string") define(p.name, ParamType::String, Value{p.defaultText}, 0, 0, p.label);
    else if (p.type == "bool") define(p.name, ParamType::Bool, Value{p.defaultValue > 0.5}, 0, 0, p.label);
    else define(p.name, ParamType::Float, Value{p.defaultValue}, p.min, p.max, p.label);
  }
  auto& chain = chains_[entity];
  const int at = index < 0 || index > static_cast<int>(chain.size()) ? static_cast<int>(chain.size()) : index;
  chain.insert(chain.begin() + at, {id, type});
  announce(entity);
  return id;
}

void EffectChains::remove(const std::string& entity, int id) {
  auto& chain = chains_[entity];
  std::erase_if(chain, [&](const Instance& i) { return i.id == id; });
  params_.removePrefix(instancePath(entity, id) + "/");
  aux_.erase(instancePath(entity, id));
  audioState_.erase(instancePath(entity, id));
  announce(entity);
}

void EffectChains::move(const std::string& entity, int id, int index) {
  auto& chain = chains_[entity];
  auto it = std::find_if(chain.begin(), chain.end(), [&](const Instance& i) { return i.id == id; });
  if (it == chain.end()) throw std::runtime_error("efecto inexistente");
  const Instance item = *it;
  chain.erase(it);
  chain.insert(chain.begin() + std::clamp(index, 0, static_cast<int>(chain.size())), item);
  announce(entity);
}

void EffectChains::clear(const std::string& entity) {
  auto it = chains_.find(entity);
  if (it == chains_.end() || it->second.empty()) return;
  for (const auto& i : it->second) {
    params_.removePrefix(instancePath(entity, i.id) + "/");
    aux_.erase(instancePath(entity, i.id));
    audioState_.erase(instancePath(entity, i.id));
  }
  it->second.clear();
  announce(entity);
}

bool EffectChains::empty(const std::string& entity) const {
  auto it = chains_.find(entity);
  return it == chains_.end() || it->second.empty();
}

std::vector<audio::EffectStep> EffectChains::audioSteps(const std::string& entity) const {
  std::vector<audio::EffectStep> out;
  auto it = chains_.find(entity);
  if (it == chains_.end()) return out;
  for (const auto& inst : it->second) {
    const schema::EffectType* t = catalog_.find(inst.type);
    if (!t || t->glsl != "//AUDIO") continue;
    const std::string prefix = instancePath(entity, inst.id);
    if (params_.getBool(prefix + "/bypass")) continue;
    auto& state = audioState_[prefix];
    if (!state) state = audio::makeAudioEffect(t->id);
    audio::EffectStep step;
    step.effect = state;
    size_t slot = 0;
    for (const auto& p : t->params)
      if (slot < step.values.size()) step.values[slot++] = static_cast<float>(params_.getFloat(prefix + "/" + p.name));
    out.push_back(std::move(step));
  }
  return out;
}

json EffectChains::save(const std::string& entity) const {
  json out = json::array();
  auto it = chains_.find(entity);
  if (it == chains_.end()) return out;
  for (const auto& inst : it->second) {
    const std::string prefix = instancePath(entity, inst.id) + "/";
    json values = json::object();
    for (const auto& d : params_.list())
      if (d.path.rfind(prefix, 0) == 0)
        if (auto v = params_.get(d.path); v && *v != d.defaultValue) values[d.path.substr(prefix.size())] = control::toJson(*v);
    out.push_back({{"id", inst.id}, {"type", inst.type}, {"values", std::move(values)}});
  }
  return out;
}

void EffectChains::load(const std::string& entity, const json& chain) {
  clear(entity);
  for (const auto& e : chain) {
    if (!catalog_.find(e.value("type", ""))) continue;  // un ISF que ya no está en la carpeta
    const int id = add(entity, e.at("type"), -1, e.value("id", 0));
    const std::string prefix = instancePath(entity, id) + "/";
    const json values = e.value("values", json::object());
    for (const auto& [k, v] : values.items())
      if (auto value = control::fromJson(v)) params_.set(prefix + k, *value);
  }
}

json EffectChains::describe(const std::string& entity) const {
  json out = json::array();
  auto it = chains_.find(entity);
  if (it == chains_.end()) return out;
  for (const auto& inst : it->second) {
    const std::string prefix = instancePath(entity, inst.id);
    const schema::EffectType* t = catalog_.find(inst.type);
    json parameters = json::array();
    for (const auto& d : params_.list())
      if (d.path.rfind(prefix + "/", 0) == 0) parameters.push_back(control::describeJson(d, params_.get(d.path)));
    out.push_back({{"id", inst.id}, {"type", inst.type}, {"title", t ? t->name : inst.type}, {"prefix", prefix}, {"parameters", std::move(parameters)}});
  }
  return out;
}

media::FramePtr EffectChains::auxFor(const schema::EffectType& t, const std::string& prefix) const {
  std::string key;
  if (t.id == "lut") key = params_.getString(prefix + "/file");
  else if (t.id == "text") key = params_.getString(prefix + "/text") + "\n" + std::to_string(params_.getFloat(prefix + "/size"));
  else return nullptr;
  Aux& a = aux_[prefix];
  if (a.key == key && a.frame) return a.frame;
  a.key = key;
  a.frame = nullptr;
  try {
    if (t.id == "lut" && !key.empty()) a.frame = media::loadCubeLut(key);
    if (t.id == "text") a.frame = media::renderText(params_.getString(prefix + "/text"), params_.getFloat(prefix + "/size"), 1920, 1080);
  } catch (const std::exception& e) {
    const_cast<EventBus&>(events_).publish("effects.error", {{"message", e.what()}});
  }
  return a.frame;
}

std::vector<PlanEffect> EffectChains::plan(const std::string& entity) const {
  std::vector<PlanEffect> out;
  auto it = chains_.find(entity);
  if (it == chains_.end()) return out;
  for (const auto& inst : it->second) {
    const std::string prefix = instancePath(entity, inst.id);
    if (params_.getBool(prefix + "/bypass")) continue;
    const schema::EffectType* t = catalog_.find(inst.type);
    if (!t || t->glsl == "//AUDIO") continue;
    PlanEffect e;
    e.type = t;
    e.opacity = static_cast<float>(params_.getFloat(prefix + "/opacity", 1));
    e.blend = blendIndex(params_.getString(prefix + "/blend", "Alpha"));
    size_t slot = 0;
    for (const auto& p : t->params) {
      if (p.type == "string" || slot >= kMaxFloats) continue;
      e.values[slot++] = p.type == "bool" ? (params_.getBool(prefix + "/" + p.name) ? 1.f : 0.f)
                                          : static_cast<float>(params_.getFloat(prefix + "/" + p.name));
    }
    e.aux = auxFor(*t, prefix);
    e.instance = prefix;
    if (inst.type.rfind("ffgl:", 0) == 0)
      for (const auto& p : t->params) {
        const float v = p.type == "bool" ? (params_.getBool(prefix + "/" + p.name) ? 1.f : 0.f) : static_cast<float>(params_.getFloat(prefix + "/" + p.name));
        e.extra.push_back({static_cast<uint32_t>(std::stoul(p.name.substr(1))), v});
      }
    out.push_back(std::move(e));
  }
  return out;
}

}  // namespace vj::compositor
