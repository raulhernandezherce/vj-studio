#include "control/ControlModule.h"

#include <cstdio>

namespace vj::control {

bool ControlModule::start(EngineContext& ctx) {
  server_ = std::make_unique<ControlServer>(ctx.params, ctx.events, ctx.commands);
  if (!server_->start(host_, port_)) {
    std::fprintf(stderr, "vjengine: no se pudo escuchar en %s:%d\n", host_.c_str(), port_);
    return false;
  }
  monitors_ = ctx.monitors;
  if (monitors_)
    monitors_->connect(
        [this](int id, int w, int h, const std::vector<uint8_t>& px) { server_->sendMonitor(id, w, h, px); },
        [this] { return server_->monitorWanted(); });
  return true;
}

void ControlModule::setHistoryHook(ApiDispatcher::HistoryHook hook) { server_->api().setHistoryHook(std::move(hook)); }

void ControlModule::stop() {
  if (monitors_) monitors_->disconnect();
  server_.reset();
}

}  // namespace vj::control
