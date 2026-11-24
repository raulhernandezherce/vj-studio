#pragma once
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "control/ApiDispatcher.h"
#include "core/EventBus.h"
#include "core/ParameterRegistry.h"

namespace vj::control {

// Servidor WebSocket y HTTP mínimo en ws://host:puerto/api/v1, en un hilo propio.
class ControlServer {
 public:
  ControlServer(ParameterRegistry& params, EventBus& events, CommandBus* commands = nullptr)
      : params_(params), events_(events), commands_(commands), api_(params, commands) {}
  ApiDispatcher& api() { return api_; }

  // Monitores: clientes que han pedido imagen.
  bool monitorWanted();
  void sendMonitor(int monitor, int width, int height, const std::vector<uint8_t>& rgba);
  ~ControlServer() { stop(); }

  // port 0: lo elige el sistema. Devuelve false si no se puede escuchar.
  bool start(const std::string& host, int port);
  void stop();
  int port() const { return port_; }

  // El navegador envía Origin; solo se aceptan la propia interfaz y localhost.
  static bool originAllowed(const std::string& origin);
  // Frena el DNS rebinding: un nombre público que resuelve a 127.0.0.1 no entra.
  static bool hostAllowed(const std::string& host);

 private:
  struct Client {
    int fd = -1;
    bool upgraded = false;
    bool closing = false;
    std::string in, out;
    Subscriptions subs;
  };
  void run();
  void acceptClient();
  void readClient(Client& c);
  void handleHttp(Client& c);
  void handleFrames(Client& c);
  void send(Client& c, const std::string& bytes);
  void broadcast(const std::string& path, const std::string& frame);
  void broadcastAll(const std::string& frame);
  void wake();

  ParameterRegistry& params_;
  EventBus& events_;
  CommandBus* commands_;
  ApiDispatcher api_;
  bool localOnly_ = true;
  int listenFd_ = -1;
  int wakeFds_[2] = {-1, -1};
  int port_ = 0;
  std::atomic<bool> running_{false};
  std::thread thread_;
  std::mutex mutex_;  // protege clients_ y sus colas de salida
  std::map<int, std::unique_ptr<Client>> clients_;
  ParameterRegistry::ListenerId listener_ = 0;
  EventBus::HandlerId eventHandler_ = 0;
};

}  // namespace vj::control
