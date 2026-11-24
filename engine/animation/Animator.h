#pragma once
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "animation/Expression.h"
#include "animation/Levels.h"
#include "compositor/ClipClock.h"
#include "compositor/Composition.h"
#include "core/Module.h"
#include "core/ParameterRegistry.h"

namespace vj::animation {

// Curva a partir de puntos [t, v] ordenados; sin puntos, identidad.
double evalEnvelope(const nlohmann::json& points, double t);

// Cualquier parámetro puede tener una animación. El valor se escribe en silencio:
// ni ensucia el proyecto ni entra en el historial.
class Animator final : public IModule {
 public:
  std::string name() const override { return "animator"; }
  bool start(EngineContext& ctx) override;
  void tick(double dt) override;
  void stop() override;
  void setLevels(ILevels* levels) { levels_ = levels; }

  // Lanza si el parámetro no existe o la expresión no compila.
  void set(const std::string& path, nlohmann::json config);
  void remove(const std::string& path);
  std::optional<nlohmann::json> get(const std::string& path) const;
  void startNow(const std::string& path);

  // Guardado por prefijo: claves relativas al prefijo; prefijo vacío, rutas absolutas.
  nlohmann::json save(const std::string& prefix, const std::function<bool(const std::string&)>& filter = nullptr) const;
  void load(const std::string& prefix, const nlohmann::json& map);
  void clearPrefix(const std::string& prefix, const std::function<bool(const std::string&)>& filter = nullptr);
  void compositionLoaded();

 private:
  struct Anim {
    nlohmann::json config;
    compositor::ClipClock clock;
    std::shared_ptr<Expression> expr;
    bool running = false;
    double held = 0, drift = 0, lastPhase = 0;
    bool lastHigh = false;
  };
  double input(const std::string& path, Anim& a, double dt);
  void apply(const std::string& path, const Anim& a, double x);
  void onFired(const std::string& topic, const nlohmann::json& data);
  std::string ownerOf(const std::string& path) const;

  EngineContext* ctx_ = nullptr;
  ILevels* levels_ = nullptr;
  std::map<std::string, Anim> anims_;
  double time_ = 0;
  EventBus::HandlerId handler_ = 0;
};

}  // namespace vj::animation
