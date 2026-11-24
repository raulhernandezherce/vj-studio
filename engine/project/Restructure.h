#pragma once
#include <vector>

#include <nlohmann/json.hpp>

namespace vj::project {

// Cambios de estructura sobre un documento. Cada mapa va de índice antiguo a nuevo, base 0; -1 descarta.
nlohmann::json remapDocument(const nlohmann::json& doc, const std::vector<int>& layers, const std::vector<int>& columns, int newLayers, int newColumns);
void copyLayer(nlohmann::json& doc, int from, int to);
void copyColumn(nlohmann::json& doc, int from, int to);

}  // namespace vj::project
