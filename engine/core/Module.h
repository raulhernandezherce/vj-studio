#pragma once
#include <string>

namespace vj {

class ParameterRegistry;
class EventBus;
class ModuleRegistry;
class CommandBus;
class MonitorBus;

struct EngineContext {
  ParameterRegistry& params;
  EventBus& events;
  ModuleRegistry& modules;
  CommandBus* commands = nullptr;
  MonitorBus* monitors = nullptr;
};

// Cada carpeta de engine/ aporta uno o más módulos con este ciclo de vida.
class IModule {
 public:
  virtual ~IModule() = default;
  virtual std::string name() const = 0;
  virtual bool start(EngineContext& ctx) = 0;
  virtual void tick(double /*dt*/) {}
  virtual void stop() {}
};

}  // namespace vj
