#include "control/OscRouter.h"

#include <algorithm>
#include <cmath>
#include <regex>
#include <vector>

namespace vj::control {
namespace {

struct Alias {
  const char* external;
  const char* internal;
};
// Nombres del manual que difieren de los internos, como sufijos.
const std::vector<Alias> kAliases = {
    {"/video/mixer/blendmode", "/blend"},
    {"/video/opacity", "/opacity"},
    {"/video/effects/transform/positionx", "/transform/x"},
    {"/video/effects/transform/positiony", "/transform/y"},
    {"/video/effects/transform/scale", "/transform/scale"},
    {"/video/effects/transform/rotationz", "/transform/rotation"},
    {"/audio/volume", "/audio/volume"},
};

bool replaceSuffix(std::string& s, const std::string& from, const std::string& to) {
  if (s.size() < from.size() || s.compare(s.size() - from.size(), from.size(), from) != 0) return false;
  s.replace(s.size() - from.size(), from.size(), to);
  return true;
}

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
  for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) s.replace(at, from.size(), to);
  return s;
}

double unit(double v, const ParamDesc& d) { return d.max > d.min ? (v - d.min) / (d.max - d.min) : 0; }

}  // namespace

std::string oscToParameter(const std::string& address, const ParameterRegistry& params) {
  std::string p = address;
  // Dirección relativa: la capa o el clip seleccionados en este momento.
  p = replaceAll(p, "/composition/selectedclip/", "/composition/layers/" + std::to_string(params.getInt("/composition/activelayer", 1)) + "/clips/" +
                                                      std::to_string(params.getInt("/composition/selectedclip", 1)) + "/");
  p = replaceAll(p, "/composition/selectedlayer/", "/composition/layers/" + std::to_string(params.getInt("/composition/activelayer", 1)) + "/");
  if (params.describe(p)) return p;
  for (const auto& a : kAliases) {
    std::string candidate = p;
    if (replaceSuffix(candidate, a.external, a.internal) && params.describe(candidate)) return candidate;
  }
  return {};
}

std::string parameterToOsc(const std::string& path) {
  static const std::regex layer(R"(^/composition/layers/\d+/(opacity|blend)$)");
  std::string out = path;
  if (std::regex_match(path, layer)) {
    if (!replaceSuffix(out, "/opacity", "/video/opacity")) replaceSuffix(out, "/blend", "/video/mixer/blendmode");
  }
  return out;
}

OscArg toOscValue(const ParamDesc& d, const Value& v) {
  if (auto* b = std::get_if<bool>(&v)) return int32_t{*b};
  if (auto* i = std::get_if<int64_t>(&v)) return static_cast<int32_t>(*i);
  if (auto* f = std::get_if<double>(&v)) return static_cast<float>(unit(*f, d));
  const auto& s = std::get<std::string>(v);
  if (d.type == ParamType::Enum) {
    const auto it = std::find(d.options.begin(), d.options.end(), s);
    return static_cast<int32_t>(it - d.options.begin());
  }
  return s;
}

std::optional<OscMessage> applyOsc(const OscMessage& m, ParameterRegistry& params, std::string* error) {
  const auto fail = [&](const std::string& why) -> std::optional<OscMessage> {
    if (error) *error = why;
    return std::nullopt;
  };
  const std::string path = oscToParameter(m.address, params);
  if (path.empty()) return fail("dirección desconocida");
  const auto d = params.describe(path);

  std::string op;
  size_t first = 0;
  if (!m.args.empty())
    if (auto* s = std::get_if<std::string>(&m.args[0])) {
      if (*s == "?" || *s == "+" || *s == "-" || *s == "*" || *s == "a") {
        op = *s;
        first = 1;
      }
    }
  if (op == "?") {
    OscMessage reply;
    reply.address = m.address;
    if (auto v = params.get(path)) reply.args.push_back(toOscValue(*d, *v));
    return reply;
  }

  if (d->type == ParamType::Trigger) {
    // connect 1 pulsa, connect 0 suelta: permite el modo Piano desde OSC.
    bool down = true;
    if (m.args.size() > first) {
      if (auto* i = std::get_if<int32_t>(&m.args[first])) down = *i != 0;
      if (auto* f = std::get_if<float>(&m.args[first])) down = *f > 0.5f;
    }
    if (down) params.trigger(path);
    else if (path.ends_with("/connect")) params.trigger(path.substr(0, path.size() - 7) + "release");
    return std::nullopt;
  }
  if (m.args.size() <= first) return fail("falta el valor");
  const OscArg& arg = m.args[first];
  if (std::holds_alternative<OscColor>(arg)) return fail("este parámetro no es un color");

  if (auto* s = std::get_if<std::string>(&arg)) {
    if (params.set(path, *s) != ParamError::None) return fail("valor no válido");
    return std::nullopt;
  }
  const double raw = std::holds_alternative<int32_t>(arg) ? std::get<int32_t>(arg) : std::get<float>(arg);
  const bool isInt = std::holds_alternative<int32_t>(arg);

  switch (d->type) {
    case ParamType::Bool:
      params.set(path, isInt ? raw != 0 : raw > 0.5);
      break;
    case ParamType::Enum: {
      if (d->options.empty()) break;
      const size_t n = d->options.size();
      const size_t i = isInt ? static_cast<size_t>(std::clamp(raw, 0.0, double(n - 1))) : std::min(n - 1, static_cast<size_t>(std::clamp(raw, 0.0, 1.0) * double(n)));
      params.set(path, d->options[i]);
      break;
    }
    case ParamType::Float:
    case ParamType::Int: {
      const double now = params.getFloat(path);
      // Un float va normalizado, salvo con "a" o con un operador, que hablan en unidades reales.
      const double absolute = op.empty() && !(isInt && d->type == ParamType::Int) ? d->min + raw * (d->max - d->min) : raw;
      double next = absolute;
      if (op == "+") next = now + raw;
      if (op == "-") next = now - raw;
      if (op == "*") next = now * raw;
      params.set(path, next);
      break;
    }
    default:
      return fail("tipo no admitido");
  }
  return std::nullopt;
}

}  // namespace vj::control
