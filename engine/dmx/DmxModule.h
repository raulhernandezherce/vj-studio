#pragma once
#include <deque>
#include <map>
#include <string>

#include "core/Module.h"
#include "dmx/Dmx.h"

namespace vj::dmx {

// Lleva la composición a tiras y matrices LED por Art-Net.
class DmxModule final : public IModule {
 public:
  explicit DmxModule(std::string documents) : documents_(std::move(documents)) {}
  std::string name() const override { return "dmx"; }
  bool start(EngineContext& ctx) override;
  void tick(double dt) override;
  void stop() override;

 private:
  struct Pending {
    double due;
    std::map<int, Universe> universes;
  };
  std::string file() const;
  std::string typesFolder() const;
  void loadTypes();
  void save();
  void send(const std::map<int, Universe>& universes);

  EngineContext* ctx_ = nullptr;
  std::string documents_;
  Setup setup_;
  std::map<std::string, FixtureType> types_;
  std::deque<Pending> queue_;
  double clock_ = 0, next_ = 0;
  int socket_ = -1;
  uint8_t sequence_ = 1;
};

}  // namespace vj::dmx
