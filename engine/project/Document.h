#pragma once
#include <nlohmann/json.hpp>
#include <string>

namespace vj::project {

inline constexpr int kFormatVersion = 1;
inline constexpr const char* kFormatName = "vjstudio-composition";
inline constexpr const char* kExtension = ".vjcomp";

// Lleva un documento antiguo al formato actual, versión a versión.
// Lanza std::runtime_error si no es un proyecto o viene de una versión futura.
nlohmann::json migrate(nlohmann::json doc);

nlohmann::json readDocument(const std::string& path);
// Escribe en un temporal y renombra: un fallo nunca deja un proyecto a medias.
void writeDocument(const std::string& path, const nlohmann::json& doc);

// Reenlace: dada la ruta perdida `from` y su nueva ubicación `to`, calcula la nueva ruta
// de un archivo hermano conservando su posición relativa. Vacío si no es hermano.
std::string siblingPath(const std::string& from, const std::string& to, const std::string& candidate);

}  // namespace vj::project
