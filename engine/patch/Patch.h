#pragma once
#include <array>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "compositor/EffectChains.h"
#include "core/ParameterRegistry.h"
#include "gfx/IRenderBackend.h"

namespace vj::patch {

inline constexpr int kPatchVersion = 1;

enum class PortType { Float, Int, Bool, Trigger, String, Color, Texture };
const char* typeName(PortType t);
// Vacío si se pueden conectar. Los números se convierten entre sí; a texto llega todo menos texturas.
std::string linkError(PortType from, PortType to);

struct Value {
  double n = 0;
  std::array<float, 4> c = {0, 0, 0, 1};
  std::string s;
  gfx::TextureId tex = gfx::kNoTexture;
};

struct Port {
  std::string name;
  PortType type = PortType::Float;
  double def = 0, min = 0, max = 1;
  std::string text;
};

// Lo que recibe un nodo de datos en cada evaluación.
struct EvalContext {
  const std::vector<Value>& in;
  std::vector<Value>& out;
  std::array<double, 4>& state;  // memoria del nodo entre fotogramas
  const nlohmann::json& attrs;
  double time, beat, bpm, dt;
  ParameterRegistry* params;
  std::string* error;
};

enum class Kind { Data, TextureIn, TextureOut, Effect, Blend, Mixer, Transition, Solid, Shape, Isf, Media };

struct Attr {
  std::string name, label, type;  // int, float, string, code, enum, file
  nlohmann::json def;
  std::vector<std::string> options;
};

struct NodeType {
  std::string id, name, category, description;
  Kind kind = Kind::Data;
  std::vector<Port> inputs, outputs;
  std::vector<Attr> attrs;
  std::function<void(EvalContext&)> eval;
  const schema::EffectType* effect = nullptr;
};

struct Node {
  int id = 0;
  std::string type, name;
  double x = 0, y = 0;
  nlohmann::json values = nlohmann::json::object();  // entradas sin conectar
  nlohmann::json attrs = nlohmann::json::object();   // no modulables
  std::vector<std::string> hidden;
};

struct Link {
  int from = 0, to = 0;
  std::string fromPort, toPort;
};

struct Patch {
  std::string name = "Sin título", category = "none";  // none, effect, source
  std::vector<Node> nodes;
  std::vector<Link> links;
};

// Registro de nodos. Los efectos del catálogo son nodos sin más: un efecto nuevo aparece solo.
class NodeLibrary {
 public:
  explicit NodeLibrary(const compositor::EffectCatalog& catalog);
  const NodeType* find(const std::string& id) const;
  const std::vector<NodeType>& all() const { return types_; }
  nlohmann::json describe() const;

 private:
  std::vector<NodeType> types_;
  std::map<std::string, size_t> index_;
};

nlohmann::json toJson(const Patch& p);
// Lanza std::runtime_error si no es un parche. Lo que no entienda de una versión mayor lo avisa
// en warnings y lo conserva, para no romper un parche ajeno al abrirlo y guardarlo.
Patch fromJson(const nlohmann::json& j, const NodeLibrary& lib, std::vector<std::string>* warnings = nullptr);
// Lanza std::runtime_error con la conexión que no vale.
void validate(const Patch& p, const NodeLibrary& lib);

struct Order {
  std::vector<int> nodes;           // cada nodo después de lo que necesita
  std::set<std::pair<int, int>> delayed;  // (origen, destino): llega con un fotograma de retardo
};
Order evaluationOrder(const Patch& p);

struct Exposed {
  int node = 0;
  schema::EffectParam param;
};
// Float, Integer y Trigger con deslizador en el catálogo. Sin mínimo y máximo no se animan.
std::vector<Exposed> exposedParams(const Patch& p, const NodeLibrary& lib);

}  // namespace vj::patch
