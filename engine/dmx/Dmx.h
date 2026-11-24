#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace vj::dmx {

inline constexpr int kChannels = 512;
using Universe = std::array<uint8_t, kChannels>;

struct FixtureType {
  std::string name;
  std::vector<std::string> channels;  // r g b w l, o un valor fijo de 0 a 255
};
const std::vector<FixtureType>& builtinTypes();

// Una tira o matriz de píxeles LED que lee una zona de la composición (de 0 a 1).
struct Fixture {
  int id = 1;
  std::string name = "Fixture", type = "RGB";
  int universe = 1, channel = 1;
  int columns = 16, rows = 1;
  double x = 0, y = 0, w = 1, h = 1;
  bool flipX = false, flipY = false;
  double brightness = 1, contrast = 1;
};

struct Setup {
  std::vector<Fixture> fixtures;
};

nlohmann::json toJson(const Setup& s);
Setup setupFromJson(const nlohmann::json& j);

// Primer canal libre tras el fixture anterior, en la numeración 1..512.
void autoAssign(Setup& s, const std::map<std::string, FixtureType>& types);
// Avisos de canales pisados entre fixtures.
std::vector<std::string> overlaps(const Setup& s, const std::map<std::string, FixtureType>& types);
// Canales ocupados por un fixture, contando el salto entre universos.
int channelCount(const Fixture& f, const FixtureType& t);

// rgba con la fila superior primero. Un píxel nunca se parte entre dos universos.
std::map<int, Universe> render(const Setup& s, const std::map<std::string, FixtureType>& types, const std::vector<uint8_t>& rgba, int width, int height);

// universe empieza en 1, como en la interfaz; en el paquete, Art-Net cuenta desde 0.
std::vector<uint8_t> artDmxPacket(int universe, uint8_t sequence, const Universe& data);
std::vector<uint8_t> artSyncPacket();

}  // namespace vj::dmx
