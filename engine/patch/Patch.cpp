#include "patch/Patch.h"

#include <algorithm>
#include <stdexcept>

using nlohmann::json;

namespace vj::patch {

const char* typeName(PortType t) {
  switch (t) {
    case PortType::Float: return "float";
    case PortType::Int: return "int";
    case PortType::Bool: return "bool";
    case PortType::Trigger: return "trigger";
    case PortType::String: return "string";
    case PortType::Color: return "color";
    case PortType::Texture: return "texture";
  }
  return "";
}

std::string linkError(PortType from, PortType to) {
  const auto number = [](PortType t) { return t == PortType::Float || t == PortType::Int || t == PortType::Bool || t == PortType::Trigger; };
  if (from == to || (number(from) && number(to))) return "";
  if (to == PortType::String && from != PortType::Texture) return "";
  return std::string("no se puede conectar ") + typeName(from) + " con " + typeName(to);
}

json toJson(const Patch& p) {
  json nodes = json::array(), links = json::array();
  for (const Node& n : p.nodes)
    nodes.push_back({{"id", n.id}, {"type", n.type}, {"name", n.name}, {"x", n.x}, {"y", n.y}, {"values", n.values}, {"attrs", n.attrs}, {"hidden", n.hidden}});
  for (const Link& l : p.links) links.push_back({{"from", l.from}, {"fromPort", l.fromPort}, {"to", l.to}, {"toPort", l.toPort}});
  return {{"format", "vjstudio-patch"}, {"version", kPatchVersion}, {"name", p.name}, {"category", p.category}, {"nodes", nodes}, {"links", links}};
}

Patch fromJson(const json& j, const NodeLibrary& lib, std::vector<std::string>* warnings) {
  if (j.value("format", "") != "vjstudio-patch") throw std::runtime_error("no es un parche de VJ Studio");
  const auto warn = [&](const std::string& w) {
    if (warnings) warnings->push_back(w);
  };
  if (j.value("version", 0) > kPatchVersion) warn("el parche es de una versión más nueva: puede faltar algo");
  Patch p;
  p.name = j.value("name", p.name);
  p.category = j.value("category", p.category);
  const json nodes = j.value("nodes", json::array()), links = j.value("links", json::array());
  for (const json& n : nodes) {
    Node out;
    out.id = n.at("id").get<int>();
    out.type = n.at("type").get<std::string>();
    out.name = n.value("name", out.type);
    out.x = n.value("x", 0.0);
    out.y = n.value("y", 0.0);
    out.values = n.value("values", json::object());
    out.attrs = n.value("attrs", json::object());
    out.hidden = n.value("hidden", std::vector<std::string>{});
    if (!lib.find(out.type)) warn("nodo desconocido: " + out.type);
    p.nodes.push_back(std::move(out));
  }
  for (const json& l : links)
    p.links.push_back({l.at("from").get<int>(), l.at("to").get<int>(), l.at("fromPort").get<std::string>(), l.at("toPort").get<std::string>()});
  return p;
}

void validate(const Patch& p, const NodeLibrary& lib) {
  std::map<int, const NodeType*> types;
  for (const Node& n : p.nodes) {
    if (types.count(n.id)) throw std::runtime_error("nodo repetido");
    types[n.id] = lib.find(n.type);
  }
  std::set<std::pair<int, std::string>> used;
  for (const Link& l : p.links) {
    if (!types.count(l.from) || !types.count(l.to)) throw std::runtime_error("conexión con un nodo que no existe");
    const NodeType* a = types[l.from];
    const NodeType* b = types[l.to];
    if (!a || !b) continue;  // nodo desconocido: se conserva sin ejecutarlo
    const auto port = [](const std::vector<Port>& ports, const std::string& name) -> const Port* {
      for (const Port& q : ports)
        if (q.name == name) return &q;
      return nullptr;
    };
    const Port* out = port(a->outputs, l.fromPort);
    const Port* in = port(b->inputs, l.toPort);
    if (!out || !in) throw std::runtime_error("conexión con un puerto que no existe: " + l.fromPort + " → " + l.toPort);
    const std::string err = linkError(out->type, in->type);
    if (!err.empty()) throw std::runtime_error(err);
    if (!used.insert({l.to, l.toPort}).second) throw std::runtime_error("la entrada " + l.toPort + " ya está conectada");
  }
}

Order evaluationOrder(const Patch& p) {
  std::map<int, std::vector<int>> sources;
  for (const Link& l : p.links) sources[l.to].push_back(l.from);
  Order out;
  std::map<int, int> mark;  // 1: en curso, 2: hecho
  // La arista que cierra un ciclo es la que llega tarde: así un bucle nunca bloquea.
  const std::function<void(int)> visit = [&](int id) {
    mark[id] = 1;
    for (int from : sources[id]) {
      if (mark[from] == 1) out.delayed.insert({from, id});
      else if (mark[from] == 0) visit(from);
    }
    mark[id] = 2;
    out.nodes.push_back(id);
  };
  for (const Node& n : p.nodes)
    if (mark[n.id] == 0) visit(n.id);
  return out;
}

std::vector<Exposed> exposedParams(const Patch& p, const NodeLibrary&) {
  std::vector<Exposed> out;
  for (const Node& n : p.nodes) {
    const bool number = n.type == "data.float" || n.type == "data.int", trigger = n.type == "data.trigger";
    if (!number && !trigger) continue;
    schema::EffectParam e;
    e.name = "n" + std::to_string(n.id);
    e.label = n.name;
    e.type = trigger ? "bool" : "float";
    e.min = number ? n.attrs.value("min", 0.0) : 0;
    e.max = number ? n.attrs.value("max", 0.0) : 1;
    if (number && e.min >= e.max) continue;  // sin rango no hay deslizador
    e.defaultValue = n.values.is_object() ? n.values.value("value", 0.0) : 0.0;
    out.push_back({n.id, std::move(e)});
  }
  return out;
}

}  // namespace vj::patch
