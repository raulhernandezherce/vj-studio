#include "dmx/DmxModule.h"

#include "core/Net.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

#include "core/CommandBus.h"
#include "core/ModuleRegistry.h"
#include "output/OutputModule.h"

using nlohmann::json;
namespace fs = std::filesystem;

namespace vj::dmx {
namespace {
constexpr int kSampleW = 256, kSampleH = 144;
}

std::string DmxModule::file() const { return (fs::path(documents_) / "Presets" / "dmx.json").string(); }
std::string DmxModule::typesFolder() const { return (fs::path(documents_) / "Fixtures").string(); }

void DmxModule::loadTypes() {
  types_.clear();
  for (const FixtureType& t : builtinTypes()) types_[t.name] = t;
  std::error_code ec;
  fs::create_directories(typesFolder(), ec);
  for (const auto& entry : fs::directory_iterator(typesFolder(), ec)) {
    if (entry.path().extension() != ".json") continue;
    try {
      std::ifstream in(entry.path());
      const json j = json::parse(in);
      if (j.value("format", "") == "vjstudio-fixture") types_[j.at("name")] = {j.at("name"), j.at("channels").get<std::vector<std::string>>()};
    } catch (const std::exception&) {
      // Un archivo ajeno o roto no impide usar el resto.
    }
  }
}

void DmxModule::save() {
  std::error_code ec;
  fs::create_directories(fs::path(file()).parent_path(), ec);
  const std::string tmp = file() + ".tmp";
  {
    std::ofstream out(tmp);
    out << toJson(setup_).dump(2);
  }
  fs::rename(tmp, file(), ec);
}

bool DmxModule::start(EngineContext& ctx) {
  ctx_ = &ctx;
  loadTypes();
  try {
    std::ifstream in(file());
    if (in) setup_ = setupFromJson(json::parse(in));
  } catch (const std::exception&) {
    ctx.params.publish("/dmx/status", "configuración DMX dañada: se empieza vacía");
  }
  socket_ = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (socket_ >= 0) {
    const int yes = 1;
    net::setOption(socket_, SOL_SOCKET, SO_BROADCAST, yes);
    net::setNonBlocking(socket_);  // la salida de vídeo nunca espera a la red
  }
  if (!ctx.commands) return true;
  auto& bus = *ctx.commands;
  const auto describe = [this] {
    json out = toJson(setup_);
    out["warnings"] = overlaps(setup_, types_);
    json counts = json::object();
    for (const Fixture& f : setup_.fixtures) {
      const auto it = types_.find(f.type);
      counts[std::to_string(f.id)] = channelCount(f, it != types_.end() ? it->second : builtinTypes()[0]);
    }
    out["channels"] = counts;
    return out;
  };
  bus.add("dmx.get", [describe](const json&) { return describe(); });
  bus.add("dmx.set", [this, describe](const json& a) {
    setup_ = setupFromJson(a.at("setup"));
    if (a.value("auto", false)) autoAssign(setup_, types_);
    save();
    return describe();
  });
  bus.add("dmx.types", [this](const json&) {
    json out = json::array();
    for (const auto& [name, t] : types_) out.push_back({{"name", name}, {"channels", t.channels}});
    return out;
  });
  bus.add("dmx.type.save", [this](const json& a) {
    const std::string name = a.at("name");
    if (name.empty() || name.find_first_of("/\\") != std::string::npos || name[0] == '.') throw std::runtime_error("nombre no válido");
    const auto channels = a.at("channels").get<std::vector<std::string>>();
    if (channels.empty()) throw std::runtime_error("un fixture necesita al menos un canal");
    std::ofstream out(fs::path(typesFolder()) / (name + ".json"));
    out << json{{"format", "vjstudio-fixture"}, {"name", name}, {"channels", channels}}.dump(2);
    if (!out) throw std::runtime_error("no se pudo guardar el fixture");
    out.close();
    loadTypes();
    return json::object();
  });
  return true;
}

void DmxModule::send(const std::map<int, Universe>& universes) {
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_port = htons(static_cast<uint16_t>(ctx_->params.getInt("/dmx/port", 6454)));
  const std::string target = ctx_->params.getString("/dmx/target");
  if (inet_pton(AF_INET, target.c_str(), &to.sin_addr) != 1) {
    ctx_->params.publish("/dmx/status", "dirección no válida: " + target);
    return;
  }
  const auto out = [&](const std::vector<uint8_t>& p) { return sendto(socket_, reinterpret_cast<const char*>(p.data()), static_cast<int>(p.size()), 0, reinterpret_cast<sockaddr*>(&to), sizeof to) >= 0; };
  bool ok = true;
  for (const auto& [u, data] : universes) ok = out(artDmxPacket(u, sequence_, data)) && ok;
  if (ctx_->params.getBool("/dmx/artsync")) ok = out(artSyncPacket()) && ok;
  sequence_ = sequence_ == 255 ? 1 : sequence_ + 1;  // 0 significaría sin secuencia
  ctx_->params.publish("/dmx/status", ok ? "enviando " + std::to_string(universes.size()) + " universos" : "error de red");
}

void DmxModule::tick(double dt) {
  auto& p = ctx_->params;
  clock_ += dt;
  if (!p.getBool("/dmx/enabled") || socket_ < 0 || setup_.fixtures.empty()) {
    queue_.clear();
    return;
  }
  auto* output = ctx_->modules.get<output::OutputModule>("output");
  if (clock_ >= next_ && output) {
    const double interval = 1.0 / std::max<int64_t>(1, p.getInt("/dmx/fps", 30));
    next_ = std::max(next_ + interval, clock_ - interval);
    const auto rgba = output->smallFrame(kSampleW, kSampleH);
    if (!rgba.empty()) queue_.push_back({clock_ + p.getInt("/dmx/delay", 40) / 1000.0, render(setup_, types_, rgba, kSampleW, kSampleH)});
  }
  while (!queue_.empty() && queue_.front().due <= clock_) {
    send(queue_.front().universes);
    queue_.pop_front();
  }
}

void DmxModule::stop() {
  if (socket_ >= 0) net::closeSocket(socket_);
  socket_ = -1;
}

}  // namespace vj::dmx
