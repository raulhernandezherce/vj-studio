#pragma once
#include <nlohmann/json.hpp>
#include <functional>
#include <optional>
#include <set>
#include <string>

#include "core/CommandBus.h"
#include "core/ParameterRegistry.h"
#include "core/ValueJson.h"

namespace vj::control {

// Suscripciones de un cliente. "*" significa todos los parámetros.
struct Subscriptions {
  std::set<std::string> paths;
  bool monitor = false;
  bool matches(const std::string& path) const { return paths.count("*") || paths.count(path); }
};

// Núcleo de la API, sin sockets. OSC, MIDI y REST (M5) reutilizan estas mismas acciones.
class ApiDispatcher {
 public:
  // Anota un cambio hecho a mano en la interfaz, para deshacerlo.
  using HistoryHook = std::function<void(const std::string& path, const Value& before, const Value& after)>;

  explicit ApiDispatcher(ParameterRegistry& params, CommandBus* commands = nullptr) : params_(params), commands_(commands) {}
  void setHistoryHook(HistoryHook hook) { history_ = std::move(hook); }
  nlohmann::json handle(const nlohmann::json& request, Subscriptions& subs);
  // Atajo para texto sin procesar; un JSON mal formado devuelve una respuesta de error.
  std::string handleText(const std::string& text, Subscriptions& subs);

 private:
  ParameterRegistry& params_;
  CommandBus* commands_;
  HistoryHook history_;
};

}  // namespace vj::control
