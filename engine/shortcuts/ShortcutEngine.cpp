#include "shortcuts/ShortcutEngine.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <stdexcept>

#include "control/OscRouter.h"

using nlohmann::json;

namespace vj::shortcuts {

int ShortcutEngine::add(json s) {
  const int id = nextId_++;
  s["id"] = id;
  list_.push_back(std::move(s));
  return id;
}

void ShortcutEngine::remove(int id) {
  list_.erase(std::remove_if(list_.begin(), list_.end(), [&](const json& s) { return s.value("id", 0) == id; }), list_.end());
}

void ShortcutEngine::update(int id, const json& fields) {
  for (auto& s : list_)
    if (s.value("id", 0) == id)
      for (const auto& [k, v] : fields.items())
        if (k != "id") s[k] = v;
}

void ShortcutEngine::load(const json& list) {
  list_ = json::array();
  nextId_ = 1;
  for (json s : list) {
    s.erase("id");
    add(std::move(s));
  }
}

void ShortcutEngine::learn(const std::string& target) {
  if (!params_.describe(control::oscToParameter(target, params_))) throw std::runtime_error("parámetro desconocido: " + target);
  learning_ = target;
}

std::string ShortcutEngine::resolve(const std::string& target) const { return control::oscToParameter(target, params_); }

void ShortcutEngine::setUnit(const std::string& path, double u) {
  const auto d = params_.describe(path);
  if (!d) return;
  u = std::clamp(u, 0.0, 1.0);
  switch (d->type) {
    case ParamType::Float:
    case ParamType::Int: params_.set(path, d->min + u * (d->max - d->min)); break;
    case ParamType::Bool: params_.set(path, u >= 0.5); break;
    case ParamType::Enum:
      if (!d->options.empty()) params_.set(path, d->options[std::min(d->options.size() - 1, static_cast<size_t>(u * static_cast<double>(d->options.size())))]);
      break;
    default: break;
  }
}

void ShortcutEngine::handle(const ControlEvent& e) {
  if (!learning_.empty()) {
    if (e.button && !e.down) return;  // se aprende al pulsar; soltar no cuenta
    const auto d = params_.describe(resolve(learning_));
    json s = {{"key", e.key}, {"target", learning_}};
    if (e.relative) s["mode"] = "relative";
    else if (!e.button) s["mode"] = "absolute";
    else if (d && d->type == ParamType::Bool) s["mode"] = "toggle";
    else if (d && d->type == ParamType::Trigger) s["mode"] = "trigger";
    else if (d && d->type == ParamType::Enum) s["mode"] = "next";
    else s["mode"] = "range";
    add(std::move(s));
    learning_.clear();
    params_.publish("/shortcuts/learning", false);
    return;
  }
  for (const auto& s : list_)
    if (s.value("key", "") == e.key) apply(s, e);
}

void ShortcutEngine::apply(const json& s, const ControlEvent& e) {
  const std::string path = resolve(s.value("target", ""));
  const auto d = params_.describe(path);
  if (!d) return;
  const std::string mode = s.value("mode", "absolute");
  const double lo = s.value("min", 0.0), hi = s.value("max", 1.0);
  const double in = s.value("invert", false) ? 1 - e.value : e.value;

  if (d->type == ParamType::Trigger) {
    if (e.button || mode == "trigger") {
      const bool down = e.button ? e.down : e.value >= 0.5;
      if (down) params_.trigger(path);
      else if (path.ends_with("/connect")) params_.trigger(path.substr(0, path.size() - 7) + "release");
    } else if (e.value >= 0.5) {
      params_.trigger(path);
    }
    return;
  }
  if (mode == "relative") {
    const double steps = std::max(s.value("steps", 127.0), 1.0);
    const auto now = params_.getFloat(path);
    const double unit = d->max > d->min ? (now - d->min) / (d->max - d->min) : 0;
    double next = unit + e.relative / steps * (s.value("invert", false) ? -1 : 1);
    if (s.value("loop", false)) next -= std::floor(next);
    setUnit(path, next);
    return;
  }
  if (e.button) {
    if (mode == "toggle") {
      if (e.down) params_.set(path, !params_.getBool(path));
    } else if (mode == "piano") {
      params_.set(path, s.value("invert", false) ? !e.down : e.down);
    } else if (mode == "value") {
      if (e.down) setUnit(path, s.value("value", 1.0));
    } else if (mode == "velocity") {
      if (e.down) setUnit(path, lo + (hi - lo) * in);
    } else if (mode == "next" || mode == "previous" || mode == "random") {
      if (!e.down || d->options.empty()) return;
      const auto it = std::find(d->options.begin(), d->options.end(), params_.getString(path));
      const int n = static_cast<int>(d->options.size()), at = static_cast<int>(it - d->options.begin());
      static std::mt19937 rng{1};
      const int next = mode == "random" ? static_cast<int>(rng() % static_cast<unsigned>(n)) : ((at + (mode == "next" ? 1 : -1)) % n + n) % n;
      params_.set(path, d->options[static_cast<size_t>(next)]);
    } else {  // range: máximo al pulsar, mínimo al soltar
      setUnit(path, e.down ? hi : lo);
    }
    return;
  }
  setUnit(path, lo + (hi - lo) * in);
}

std::vector<std::pair<std::string, double>> ShortcutEngine::feedback(const std::string& path, const Value& v) const {
  std::vector<std::pair<std::string, double>> out;
  std::string lookup = path;
  // Un disparador no tiene valor: su luz sigue al estado del clip.
  if (path.ends_with("/connected")) lookup = path.substr(0, path.size() - 9) + "connect";
  double value = 0;
  if (auto* b = std::get_if<bool>(&v)) value = *b ? 1 : 0;
  else if (auto* f = std::get_if<double>(&v)) {
    const auto d = params_.describe(path);
    value = d && d->max > d->min ? (*f - d->min) / (d->max - d->min) : *f;
  } else return out;
  for (const auto& s : list_)
    if (resolve(s.value("target", "")) == lookup) out.emplace_back(s.value("key", ""), std::clamp(value, 0.0, 1.0));
  return out;
}

}  // namespace vj::shortcuts
