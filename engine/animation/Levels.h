#pragma once
#include <string>

namespace vj::animation {

// Lectura de nivel de audio: fuente (external, composition, clip, layer, group) y rango en Hz.
class ILevels {
 public:
  virtual ~ILevels() = default;
  virtual double level(const std::string& source, const std::string& owner, double lowHz, double highHz) = 0;
};

}  // namespace vj::animation
