#pragma once
#include <nlohmann/json.hpp>
#include <optional>

#include "core/Parameter.h"

namespace vj::control {

// Conversión de valores. Sin dependencias de red: la usan también proyecto y efectos.
nlohmann::json toJson(const Value& v);
std::optional<Value> fromJson(const nlohmann::json& j);
nlohmann::json describeJson(const ParamDesc& d, const std::optional<Value>& value);

}  // namespace vj::control
