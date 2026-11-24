#pragma once
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "control/OscCodec.h"
#include "core/CommandBus.h"
#include "core/Module.h"
#include "core/ParameterRegistry.h"

typedef struct _DNSServiceRef_t* DNSServiceRef;

namespace vj::control {

// Entrada y salida OSC por UDP. Apagadas por defecto: OSC no tiene autenticación.
class OscModule final : public IModule {
 public:
  std::string name() const override { return "osc"; }
  bool start(EngineContext& ctx) override;
  void tick(double dt) override;
  void stop() override;
  int inputPort() const { return boundPort_.load(); }
  // Direcciones que el preset "Solo asignados" envía.
  void setOutputAllowed(std::vector<std::string> paths);

 private:
  void reconfigure();
  void closeInput();
  void run(int fd);
  void log(const std::string& direction, const std::string& text);
  void flush();

  EngineContext* ctx_ = nullptr;
  std::string applied_;
  int fd_ = -1, outFd_ = -1;
  std::atomic<int> boundPort_{0};
  std::atomic<bool> running_{false};
  std::thread thread_;
  DNSServiceRef zeroconf_ = nullptr;

  std::mutex mutex_;
  std::vector<OscMessage> pending_;
  std::vector<std::string> allowed_;
  std::deque<std::string> log_;
  ParameterRegistry::ListenerId listener_ = 0;
};

}  // namespace vj::control
