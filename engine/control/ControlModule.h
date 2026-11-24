#pragma once
#include <memory>
#include <string>

#include "control/ControlServer.h"
#include "core/CommandBus.h"
#include "core/Module.h"

namespace vj::control {

class ControlModule final : public IModule {
 public:
  ControlModule(std::string host, int port) : host_(std::move(host)), port_(port) {}
  std::string name() const override { return "control"; }
  bool start(EngineContext& ctx) override;
  void stop() override;
  int port() const { return server_ ? server_->port() : 0; }
  void setHistoryHook(ApiDispatcher::HistoryHook hook);

 private:
  std::string host_;
  int port_;
  std::unique_ptr<ControlServer> server_;
  MonitorBus* monitors_ = nullptr;
};

}  // namespace vj::control
