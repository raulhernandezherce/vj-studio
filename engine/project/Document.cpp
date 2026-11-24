#include "project/Document.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

using nlohmann::json;
namespace fs = std::filesystem;

namespace vj::project {

json migrate(json doc) {
  if (!doc.is_object() || doc.value("format", "") != kFormatName) throw std::runtime_error("no es un proyecto de VJ Studio");
  int version = doc.value("version", 0);
  if (version > kFormatVersion) throw std::runtime_error("el proyecto es de una versión más nueva del programa");
  // Cada paso sube una versión. El primero: los clips aún no estaban separados por decks.
  if (version == 0) {
    doc["decks"] = json::array({{{"id", 1}, {"name", "Deck 1"}, {"cells", doc.value("cells", json::object())}}});
    doc.erase("cells");
    doc["currentDeck"] = 0;
    version = 1;
  }
  doc["version"] = version;
  return doc;
}

json readDocument(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("no se pudo abrir el archivo");
  std::stringstream text;
  text << in.rdbuf();
  json doc = json::parse(text.str(), nullptr, false);
  if (doc.is_discarded()) throw std::runtime_error("el archivo está dañado");
  return migrate(std::move(doc));
}

void writeDocument(const std::string& path, const json& doc) {
  const std::string tmp = path + ".tmp";
  {
    std::ofstream out(tmp);
    if (!out) throw std::runtime_error("no se pudo escribir en esa carpeta");
    out << doc.dump(1);
    if (!out) throw std::runtime_error("no se pudo escribir el archivo");
  }
  std::error_code ec;
  fs::rename(tmp, path, ec);
  if (ec) {
    fs::remove(tmp, ec);
    throw std::runtime_error("no se pudo guardar el archivo");
  }
}

std::string siblingPath(const std::string& from, const std::string& to, const std::string& candidate) {
  fs::path a = fs::path(from), b = fs::path(to);
  // Quita lo que coincide por el final; lo que queda son las raíces antigua y nueva.
  while (a.has_relative_path() && b.has_relative_path() && a.filename() == b.filename()) {
    a = a.parent_path();
    b = b.parent_path();
  }
  const std::string root = a.string() + "/";
  if (candidate.rfind(root, 0) != 0) return {};
  return (b / candidate.substr(root.size())).string();
}

}  // namespace vj::project
