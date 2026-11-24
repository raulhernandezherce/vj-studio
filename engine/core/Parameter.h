#pragma once
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace vj {

using Value = std::variant<bool, int64_t, double, std::string>;

enum class ParamType { Bool, Int, Float, String, Trigger, Enum };

// Todo control del programa es un parámetro con ruta, tipo y rango.
struct ParamDesc {
  std::string path;
  ParamType type;
  Value defaultValue;
  double min;
  double max;
  bool readOnly;
  std::string label;
  std::string group;
  std::string unit;
  std::string milestone;
  std::vector<std::string> options;  // solo Enum; el valor es una de estas cadenas
};

}  // namespace vj
