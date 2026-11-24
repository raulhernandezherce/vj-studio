#include "core/ParameterRegistry.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vj {

const char* describe(ParamError e) {
  switch (e) {
    case ParamError::None: return "ok";
    case ParamError::UnknownPath: return "parámetro desconocido";
    case ParamError::ReadOnly: return "parámetro de solo lectura";
    case ParamError::TypeMismatch: return "tipo de valor incorrecto";
    case ParamError::NotATrigger: return "el parámetro no es un disparador";
    case ParamError::IsTrigger: return "un disparador no tiene valor";
  }
  return "error";
}

namespace {

// Convierte y limita el valor al tipo y rango declarados.
std::optional<Value> coerce(const ParamDesc& d, const Value& v) {
  const auto number = [&]() -> std::optional<double> {
    if (auto* i = std::get_if<int64_t>(&v)) return static_cast<double>(*i);
    if (auto* f = std::get_if<double>(&v)) return std::isfinite(*f) ? std::optional<double>(*f) : std::nullopt;
    return std::nullopt;
  };
  switch (d.type) {
    case ParamType::Bool:
      if (std::holds_alternative<bool>(v)) return v;
      return std::nullopt;
    case ParamType::Int:
      if (auto n = number()) return Value{static_cast<int64_t>(std::clamp(std::round(*n), d.min, d.max))};
      return std::nullopt;
    case ParamType::Float:
      if (auto n = number()) return Value{std::clamp(*n, d.min, d.max)};
      return std::nullopt;
    case ParamType::String:
      if (std::holds_alternative<std::string>(v)) return v;
      return std::nullopt;
    case ParamType::Enum:
      if (auto* o = std::get_if<std::string>(&v))
        if (std::find(d.options.begin(), d.options.end(), *o) != d.options.end()) return v;
      return std::nullopt;
    case ParamType::Trigger:
      return std::nullopt;
  }
  return std::nullopt;
}

}  // namespace

void ParameterRegistry::define(ParamDesc desc) {
  std::lock_guard lock(mutex_);
  if (entries_.count(desc.path)) throw std::logic_error("parámetro duplicado: " + desc.path);
  auto path = desc.path;
  Value initial = desc.defaultValue;
  entries_.emplace(std::move(path), Entry{std::move(desc), std::move(initial)});
}

void ParameterRegistry::defineAll(const std::vector<ParamDesc>& table) {
  for (const auto& d : table) define(d);
}

void ParameterRegistry::instantiate(const std::string& prefix, const std::vector<ParamDesc>& tmpl) {
  for (ParamDesc d : tmpl) {
    d.path = prefix + "/" + d.path;
    define(std::move(d));
  }
}

void ParameterRegistry::removePrefix(const std::string& prefix) {
  std::lock_guard lock(mutex_);
  auto it = entries_.lower_bound(prefix);
  while (it != entries_.end() && it->first.rfind(prefix, 0) == 0) it = entries_.erase(it);
}

std::optional<ParamDesc> ParameterRegistry::describe(const std::string& path) const {
  std::lock_guard lock(mutex_);
  auto it = entries_.find(path);
  if (it == entries_.end()) return std::nullopt;
  return it->second.desc;
}

std::vector<ParamDesc> ParameterRegistry::list() const {
  std::lock_guard lock(mutex_);
  std::vector<ParamDesc> out;
  out.reserve(entries_.size());
  for (const auto& [_, e] : entries_) out.push_back(e.desc);
  return out;
}

std::optional<Value> ParameterRegistry::get(const std::string& path) const {
  std::lock_guard lock(mutex_);
  auto it = entries_.find(path);
  if (it == entries_.end() || it->second.desc.type == ParamType::Trigger) return std::nullopt;
  return it->second.value;
}

ParamError ParameterRegistry::write(const std::string& path, const Value& value, bool external) {
  ParamDesc desc;
  Value applied;
  {
    std::lock_guard lock(mutex_);
    auto it = entries_.find(path);
    if (it == entries_.end()) return ParamError::UnknownPath;
    Entry& e = it->second;
    if (e.desc.type == ParamType::Trigger) return ParamError::IsTrigger;
    if (external && e.desc.readOnly) return ParamError::ReadOnly;
    auto coerced = coerce(e.desc, value);
    if (!coerced) return ParamError::TypeMismatch;
    if (*coerced == e.value) return ParamError::None;
    e.value = *coerced;
    desc = e.desc;
    applied = e.value;
  }
  notify(desc, applied);
  return ParamError::None;
}

ParamError ParameterRegistry::set(const std::string& path, const Value& value) {
  return write(path, value, true);
}

ParamError ParameterRegistry::publish(const std::string& path, const Value& value) {
  return write(path, value, false);
}

namespace {
thread_local bool tQuiet = false;
}

ParamError ParameterRegistry::setQuiet(const std::string& path, const Value& value) {
  tQuiet = true;
  const ParamError e = write(path, value, true);
  tQuiet = false;
  return e;
}

bool ParameterRegistry::isQuiet() { return tQuiet; }

ParamError ParameterRegistry::reset(const std::string& path) {
  auto d = describe(path);
  if (!d) return ParamError::UnknownPath;
  if (d->type == ParamType::Trigger) return ParamError::IsTrigger;
  if (d->readOnly) return ParamError::ReadOnly;
  return write(path, d->defaultValue, true);
}

ParamError ParameterRegistry::trigger(const std::string& path) {
  auto d = describe(path);
  if (!d) return ParamError::UnknownPath;
  if (d->type != ParamType::Trigger) return ParamError::NotATrigger;
  notify(*d, Value{true});
  return ParamError::None;
}

double ParameterRegistry::getFloat(const std::string& path, double fallback) const {
  auto v = get(path);
  if (!v) return fallback;
  if (auto* f = std::get_if<double>(&*v)) return *f;
  if (auto* i = std::get_if<int64_t>(&*v)) return static_cast<double>(*i);
  return fallback;
}

int64_t ParameterRegistry::getInt(const std::string& path, int64_t fallback) const {
  auto v = get(path);
  if (auto* i = v ? std::get_if<int64_t>(&*v) : nullptr) return *i;
  return fallback;
}

bool ParameterRegistry::getBool(const std::string& path, bool fallback) const {
  auto v = get(path);
  if (auto* b = v ? std::get_if<bool>(&*v) : nullptr) return *b;
  return fallback;
}

std::string ParameterRegistry::getString(const std::string& path, const std::string& fallback) const {
  auto v = get(path);
  if (auto* s = v ? std::get_if<std::string>(&*v) : nullptr) return *s;
  return fallback;
}

ParameterRegistry::ListenerId ParameterRegistry::addListener(const std::string& path, Listener fn) {
  std::lock_guard lock(mutex_);
  listeners_.push_back({nextId_, path, std::move(fn)});
  return nextId_++;
}

void ParameterRegistry::removeListener(ListenerId id) {
  std::lock_guard lock(mutex_);
  std::erase_if(listeners_, [id](const Sub& s) { return s.id == id; });
}

void ParameterRegistry::notify(const ParamDesc& desc, const Value& value) {
  std::vector<Listener> targets;
  {
    std::lock_guard lock(mutex_);
    for (const auto& s : listeners_)
      if (s.path.empty() || s.path == desc.path) targets.push_back(s.fn);
  }
  for (const auto& fn : targets) fn(desc, value);
}

}  // namespace vj
