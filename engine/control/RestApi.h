#pragma once
#include <string>

#include <nlohmann/json.hpp>

#include "core/CommandBus.h"
#include "core/ParameterRegistry.h"

namespace vj::control {

struct HttpResponse {
  int status = 200;
  std::string contentType = "application/json";
  std::string body;
};

// API REST sobre los mismos parámetros y comandos que el WebSocket. Sin sockets: se prueba directa.
HttpResponse handleRest(const std::string& method, const std::string& path, const std::string& body, ParameterRegistry& params, CommandBus* commands);

// Lista de rutas, también servida en GET /api/v1/routes.
nlohmann::json restRoutes();

}  // namespace vj::control
