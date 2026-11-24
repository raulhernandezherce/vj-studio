#pragma once
#include <optional>
#include <string>

#include "control/OscCodec.h"
#include "core/ParameterRegistry.h"

namespace vj::control {

// Traduce las direcciones públicas (las del manual) a rutas internas, y al revés.
std::string oscToParameter(const std::string& address, const ParameterRegistry& params);
std::string parameterToOsc(const std::string& path);

// Valor de un parámetro en OSC: float normalizado, entero, o texto.
OscArg toOscValue(const ParamDesc& d, const Value& v);

// Aplica un mensaje. Devuelve la respuesta si era una consulta "?".
std::optional<OscMessage> applyOsc(const OscMessage& m, ParameterRegistry& params, std::string* error = nullptr);

}  // namespace vj::control
