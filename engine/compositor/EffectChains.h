#pragma once
#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "audio/AudioPlan.h"
#include "compositor/RenderPlan.h"
#include "core/EventBus.h"
#include "core/ParameterRegistry.h"
#include "core/generated/Effects.gen.h"

namespace vj::compositor {

// Tipos disponibles: el catálogo interno más los ISF de la carpeta de usuario.
class EffectCatalog {
 public:
  EffectCatalog();
  const schema::EffectType* find(const std::string& id) const;
  std::vector<schema::EffectType> list() const;
  // Lee los .fs de una carpeta. Devuelve los errores por archivo.
  nlohmann::json scanIsf(const std::string& folder);
  void addExternal(schema::EffectType type);

 private:
  std::map<std::string, schema::EffectType> types_;
  std::vector<std::string> order_;
};

// Pasa un shader ISF a un EffectType. Lanza std::runtime_error si no se admite.
schema::EffectType parseIsf(const std::string& id, const std::string& text);

// Cadenas por entidad (prefijo de clip, capa, grupo o composición). Cada instancia
// es un conjunto de parámetros propios, así que API, OSC y deshacer las ven como el resto.
class EffectChains {
 public:
  EffectChains(ParameterRegistry& params, EventBus& events) : params_(params), events_(events) {}

  EffectCatalog& catalog() { return catalog_; }

  int add(const std::string& entity, const std::string& type, int index = -1, int id = 0);
  void remove(const std::string& entity, int id);
  void move(const std::string& entity, int id, int index);
  void clear(const std::string& entity);
  bool empty(const std::string& entity) const;

  nlohmann::json save(const std::string& entity) const;
  void load(const std::string& entity, const nlohmann::json& chain);
  nlohmann::json describe(const std::string& entity) const;  // para la interfaz
  std::vector<PlanEffect> plan(const std::string& entity) const;  // efectos de vídeo
  std::vector<audio::EffectStep> audioSteps(const std::string& entity) const;
  std::string instancePath(const std::string& entity, int id) const;
  std::map<std::string, nlohmann::json> saveAll(const std::string& prefixFilter) const;

 private:
  struct Instance {
    int id = 0;
    std::string type;
  };
  media::FramePtr auxFor(const schema::EffectType& t, const std::string& path) const;
  void announce(const std::string& entity);

  ParameterRegistry& params_;
  EventBus& events_;
  EffectCatalog catalog_;
  std::map<std::string, std::vector<Instance>> chains_;
  int nextId_ = 1;
  struct Aux {
    std::string key;
    media::FramePtr frame;
  };
  mutable std::map<std::string, Aux> aux_;
  mutable std::map<std::string, std::shared_ptr<audio::AudioEffect>> audioState_;
};

}  // namespace vj::compositor
