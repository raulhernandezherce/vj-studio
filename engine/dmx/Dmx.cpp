#include "dmx/Dmx.h"

#include <algorithm>
#include <cmath>
#include <set>

using nlohmann::json;

namespace vj::dmx {

const std::vector<FixtureType>& builtinTypes() {
  static const std::vector<FixtureType> types = {
      {"RGB", {"r", "g", "b"}}, {"GRB", {"g", "r", "b"}}, {"BGR", {"b", "g", "r"}}, {"RGBW", {"r", "g", "b", "w"}}, {"Dimmer", {"l"}},
  };
  return types;
}

json toJson(const Setup& s) {
  json list = json::array();
  for (const Fixture& f : s.fixtures)
    list.push_back({{"id", f.id}, {"name", f.name}, {"type", f.type}, {"universe", f.universe}, {"channel", f.channel}, {"columns", f.columns}, {"rows", f.rows},
                    {"x", f.x}, {"y", f.y}, {"w", f.w}, {"h", f.h}, {"flipX", f.flipX}, {"flipY", f.flipY}, {"brightness", f.brightness}, {"contrast", f.contrast}});
  return {{"format", "vjstudio-dmx"}, {"version", 1}, {"fixtures", list}};
}

Setup setupFromJson(const json& j) {
  if (j.value("format", "") != "vjstudio-dmx") throw std::runtime_error("no es una configuración DMX");
  Setup s;
  const json list = j.value("fixtures", json::array());
  for (const json& q : list) {
    Fixture f;
    f.id = q.value("id", f.id);
    f.name = q.value("name", f.name);
    f.type = q.value("type", f.type);
    f.universe = std::clamp(q.value("universe", 1), 1, 32768);
    f.channel = std::clamp(q.value("channel", 1), 1, kChannels);
    f.columns = std::clamp(q.value("columns", 16), 1, 4096);
    f.rows = std::clamp(q.value("rows", 1), 1, 4096);
    f.x = q.value("x", 0.0);
    f.y = q.value("y", 0.0);
    f.w = q.value("w", 1.0);
    f.h = q.value("h", 1.0);
    f.flipX = q.value("flipX", false);
    f.flipY = q.value("flipY", false);
    f.brightness = q.value("brightness", 1.0);
    f.contrast = q.value("contrast", 1.0);
    s.fixtures.push_back(f);
  }
  return s;
}

namespace {

// Recorre los canales de un fixture llamando a fn(universo, canal 0..511, píxel, canal del píxel).
template <class Fn>
void walk(const Fixture& f, const FixtureType& t, Fn fn) {
  int universe = f.universe, channel = f.channel - 1;
  const int width = static_cast<int>(t.channels.size());
  for (int pixel = 0; pixel < f.columns * f.rows; ++pixel) {
    if (channel + width > kChannels) {  // Auto Span con Align Output
      ++universe;
      channel = 0;
    }
    for (int c = 0; c < width; ++c) fn(universe, channel++, pixel, c);
  }
}

const FixtureType& typeOf(const Fixture& f, const std::map<std::string, FixtureType>& types) {
  const auto it = types.find(f.type);
  return it != types.end() ? it->second : builtinTypes()[0];
}

}  // namespace

int channelCount(const Fixture& f, const FixtureType& t) {
  int count = 0;
  walk(f, t, [&](int, int, int, int) { ++count; });
  return count;
}

void autoAssign(Setup& s, const std::map<std::string, FixtureType>& types) {
  int universe = 1, channel = 1;
  for (Fixture& f : s.fixtures) {
    f.universe = universe;
    f.channel = channel;
    walk(f, typeOf(f, types), [&](int u, int c, int, int) {
      universe = u;
      channel = c + 2;
    });
    if (channel > kChannels) {
      ++universe;
      channel = 1;
    }
  }
}

std::vector<std::string> overlaps(const Setup& s, const std::map<std::string, FixtureType>& types) {
  std::map<std::pair<int, int>, std::string> used;
  std::set<std::string> out;
  for (const Fixture& f : s.fixtures)
    walk(f, typeOf(f, types), [&](int u, int c, int, int) {
      auto [it, fresh] = used.try_emplace({u, c}, f.name);
      if (!fresh && it->second != f.name) out.insert(it->second + " y " + f.name + " pisan el universo " + std::to_string(u));
    });
  return {out.begin(), out.end()};
}

std::map<int, Universe> render(const Setup& s, const std::map<std::string, FixtureType>& types, const std::vector<uint8_t>& rgba, int width, int height) {
  std::map<int, Universe> out;
  for (const Fixture& f : s.fixtures) {
    const FixtureType& t = typeOf(f, types);
    std::array<double, 4> color = {};
    int lastPixel = -1;
    walk(f, t, [&](int u, int c, int pixel, int part) {
      if (pixel != lastPixel) {
        lastPixel = pixel;
        const int col = pixel % f.columns, row = pixel / f.columns;
        double px = (col + 0.5) / f.columns, py = (row + 0.5) / f.rows;
        if (f.flipX) px = 1 - px;
        if (f.flipY) py = 1 - py;
        const int sx = std::clamp(static_cast<int>((f.x + px * f.w) * width), 0, width - 1);
        const int sy = std::clamp(static_cast<int>((f.y + py * f.h) * height), 0, height - 1);
        const size_t i = (static_cast<size_t>(sy) * width + sx) * 4;
        for (int k = 0; k < 3; ++k) {
          const double v = rgba[i + k] / 255.0;
          color[k] = std::clamp(((v - 0.5) * f.contrast + 0.5) * f.brightness, 0.0, 1.0);
        }
      }
      const std::string& code = t.channels[part];
      double v = 0;
      if (code == "r") v = color[0];
      else if (code == "g") v = color[1];
      else if (code == "b") v = color[2];
      else if (code == "w") v = std::min({color[0], color[1], color[2]});
      else if (code == "l") v = 0.2126 * color[0] + 0.7152 * color[1] + 0.0722 * color[2];
      else v = std::atof(code.c_str()) / 255.0;
      auto [it, fresh] = out.try_emplace(u);
      if (fresh) it->second.fill(0);
      it->second[c] = static_cast<uint8_t>(std::lround(v * 255));
    });
  }
  return out;
}

std::vector<uint8_t> artDmxPacket(int universe, uint8_t sequence, const Universe& data) {
  const int port = universe - 1;
  std::vector<uint8_t> p = {'A', 'r', 't', '-', 'N', 'e', 't', 0, 0x00, 0x50, 0, 14, sequence, 0,
                            static_cast<uint8_t>(port & 0xFF), static_cast<uint8_t>((port >> 8) & 0x7F), kChannels >> 8, kChannels & 0xFF};
  p.insert(p.end(), data.begin(), data.end());
  return p;
}

std::vector<uint8_t> artSyncPacket() { return {'A', 'r', 't', '-', 'N', 'e', 't', 0, 0x00, 0x52, 0, 14, 0, 0}; }

}  // namespace vj::dmx
