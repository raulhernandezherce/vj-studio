#pragma once
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace vj::control {

struct OscColor {
  uint32_t rgba;
  bool operator==(const OscColor& o) const { return rgba == o.rgba; }
};
using OscArg = std::variant<int32_t, float, std::string, OscColor>;

struct OscMessage {
  std::string address;
  std::vector<OscArg> args;
};

// Lee un paquete: mensaje suelto o bundle, también anidado. Lo que no se entiende se descarta.
std::vector<OscMessage> parseOsc(const uint8_t* data, size_t size);
std::string encodeOsc(const OscMessage& m);
std::string encodeBundle(const std::vector<OscMessage>& messages);

}  // namespace vj::control
