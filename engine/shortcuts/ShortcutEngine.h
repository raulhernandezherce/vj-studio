#pragma once
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/ParameterRegistry.h"

namespace vj::shortcuts {

// Un gesto de cualquier origen. key identifica el control físico:
// midi:<dispositivo>:note:<canal>:<nota>, midi:...:cc:..., key:<tecla>, dmx:<universo>:<canal>, osc:<dirección>.
struct ControlEvent {
  std::string key;
  double value = 0;     // 0 a 1
  bool button = false;  // pulsado y soltado, frente a un control continuo
  bool down = false;
  int relative = 0;     // encoder: pasos con signo
};

// Mapa de controles físicos a parámetros. Sin dispositivos ni hilos: se prueba con eventos.
class ShortcutEngine {
 public:
  explicit ShortcutEngine(ParameterRegistry& params) : params_(params) {}

  int add(nlohmann::json shortcut);
  void remove(int id);
  void update(int id, const nlohmann::json& fields);
  nlohmann::json list() const { return list_; }
  void load(const nlohmann::json& list);

  // Aprender: el próximo gesto queda asignado a target.
  void learn(const std::string& target);
  void cancelLearn() { learning_.clear(); }
  bool isLearning() const { return !learning_.empty(); }

  void handle(const ControlEvent& e);

  // Qué controles deben iluminarse al cambiar un parámetro: pares clave y valor 0 a 1.
  std::vector<std::pair<std::string, double>> feedback(const std::string& path, const Value& v) const;

 private:
  void apply(const nlohmann::json& s, const ControlEvent& e);
  void setUnit(const std::string& path, double unit);
  std::string resolve(const std::string& target) const;

  ParameterRegistry& params_;
  nlohmann::json list_ = nlohmann::json::array();
  int nextId_ = 1;
  std::string learning_;
};

}  // namespace vj::shortcuts
