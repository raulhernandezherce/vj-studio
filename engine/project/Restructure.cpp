#include "project/Restructure.h"

#include <string>

namespace vj::project {
using json = nlohmann::json;

namespace {

// Lee un número de ruta (base 1) en `at`, lo traduce y lo añade a `out`.
bool remapNumber(const std::string& text, size_t& at, const std::vector<int>* map, std::string& out) {
  size_t end = at;
  while (end < text.size() && text[end] >= '0' && text[end] <= '9') ++end;
  if (end == at || end - at > 4) return false;
  const size_t index = std::stoul(text.substr(at, end - at));
  out += map && index >= 1 && index <= map->size() ? std::to_string((*map)[index - 1] + 1) : text.substr(at, end - at);
  at = end;
  return true;
}

bool remapWord(const std::string& text, size_t& at, const std::string& word, std::string& out) {
  if (text.compare(at, word.size(), word) != 0) return false;
  out += word;
  at += word.size();
  return true;
}

// Solo toca rutas de parámetros: una carpeta de medios llamada "clips" no se reescribe.
std::string remapPaths(const std::string& text, const std::vector<int>& layers, const std::vector<int>& columns) {
  static const std::string kRoot = "/composition/";
  std::string out;
  out.reserve(text.size());
  size_t at = 0;
  for (size_t hit; (hit = text.find(kRoot, at)) != std::string::npos;) {
    out.append(text, at, hit + kRoot.size() - at);
    at = hit + kRoot.size();
    if (remapWord(text, at, "layers/", out)) {
      if (remapNumber(text, at, &layers, out) && remapWord(text, at, "/clips/", out)) remapNumber(text, at, &columns, out);
    } else if (remapWord(text, at, "columns/", out)) {
      remapNumber(text, at, &columns, out);
    } else if (remapWord(text, at, "groups/", out)) {
      if (remapNumber(text, at, nullptr, out) && remapWord(text, at, "/columns/", out)) remapNumber(text, at, &columns, out);
    }
  }
  out.append(text, at, std::string::npos);
  return out;
}

bool dropped(const std::string& path) {
  for (const char* marker : {"/layers/0", "/clips/0", "/columns/0"}) {
    const size_t hit = path.find(marker);
    if (hit == std::string::npos) continue;
    const size_t end = hit + std::string(marker).size();
    if (end == path.size() || path[end] == '/') return true;
  }
  return false;
}

void dropKeys(json& object) {
  if (!object.is_object()) return;
  for (auto it = object.begin(); it != object.end();) it = dropped(it.key()) ? object.erase(it) : std::next(it);
}

json remapCells(const json& cells, const std::vector<int>& layers, const std::vector<int>& columns) {
  json out = json::object();
  for (const auto& [k, v] : cells.items()) {
    const size_t slash = k.find('/');
    if (slash == std::string::npos) continue;
    const size_t l = std::stoul(k.substr(0, slash)), c = std::stoul(k.substr(slash + 1));
    if (l >= layers.size() || c >= columns.size() || layers[l] < 0 || columns[c] < 0) continue;
    out[std::to_string(layers[l]) + "/" + std::to_string(columns[c])] = v;
  }
  return out;
}

std::vector<int> identity(size_t n) {
  std::vector<int> map(n);
  for (size_t i = 0; i < n; ++i) map[i] = static_cast<int>(i);
  return map;
}

template <class Each>
void forEachCells(json& doc, Each each) {
  for (auto& deck : doc["decks"])
    if (deck.contains("cells")) each(deck["cells"]);
  if (doc.contains("persistent")) each(doc["persistent"]);
}

// Copia las entradas cuyas claves pertenecen a `prefix`, ya renombradas con los mapas.
void copyEntries(json& object, const std::string& prefix, const std::vector<int>& layers, const std::vector<int>& columns) {
  if (!object.is_object()) return;
  json added = json::object();
  for (const auto& [k, v] : object.items()) {
    if (k != prefix && k.rfind(prefix + "/", 0) != 0) continue;
    added[remapPaths(k, layers, columns)] = json::parse(remapPaths(v.dump(), layers, columns));
  }
  object.update(added);
}

}  // namespace

json remapDocument(const json& doc, const std::vector<int>& layers, const std::vector<int>& columns, int newLayers, int newColumns) {
  json out = json::parse(remapPaths(doc.dump(), layers, columns));
  for (const char* section : {"values", "effects", "animations"})
    if (out.contains(section)) dropKeys(out[section]);
  forEachCells(out, [&](json& cells) { cells = remapCells(cells, layers, columns); });
  out["shape"] = {{"layers", newLayers}, {"columns", newColumns}};
  return out;
}

void copyLayer(json& doc, int from, int to) {
  const int count = doc["shape"].value("layers", 0);
  std::vector<int> layers = identity(count);
  layers[from] = to;
  const std::vector<int> columns = identity(doc["shape"].value("columns", 0));
  const std::string prefix = "/composition/layers/" + std::to_string(from + 1);
  for (const char* section : {"values", "effects", "animations"})
    if (doc.contains(section)) copyEntries(doc[section], prefix, layers, columns);
  forEachCells(doc, [&](json& cells) {
    json added = json::object();
    for (const auto& [k, v] : cells.items())
      if (k.rfind(std::to_string(from) + "/", 0) == 0) added[std::to_string(to) + k.substr(k.find('/'))] = v;
    cells.update(added);
  });
}

void copyColumn(json& doc, int from, int to) {
  const std::vector<int> layers = identity(doc["shape"].value("layers", 0));
  std::vector<int> columns = identity(doc["shape"].value("columns", 0));
  columns[from] = to;
  if (doc.contains("values")) {
    copyEntries(doc["values"], "/composition/columns/" + std::to_string(from + 1), layers, columns);
    json added = json::object();
    const std::string tail = "/columns/" + std::to_string(from + 1);
    for (const auto& [k, v] : doc["values"].items()) {
      if (k.rfind("/composition/groups/", 0) != 0) continue;
      const size_t hit = k.find(tail);
      if (hit == std::string::npos || (hit + tail.size() != k.size() && k[hit + tail.size()] != '/')) continue;
      added[remapPaths(k, layers, columns)] = v;
    }
    doc["values"].update(added);
  }
  forEachCells(doc, [&](json& cells) {
    json added = json::object();
    const std::string tail = "/" + std::to_string(from);
    for (const auto& [k, v] : cells.items())
      if (k.size() > tail.size() && k.compare(k.size() - tail.size(), tail.size(), tail) == 0) added[k.substr(0, k.find('/')) + "/" + std::to_string(to)] = v;
    cells.update(added);
  });
}

}  // namespace vj::project
