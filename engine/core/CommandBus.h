#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace vj {

// Operaciones que no son un parámetro: guardar, cambiar de deck, deshacer...
// Los manejadores corren siempre en el hilo principal, entre fotogramas.
class CommandBus {
 public:
  using Handler = std::function<nlohmann::json(const nlohmann::json& args)>;

  void add(const std::string& name, Handler fn);

  // Desde el hilo principal. Lanza std::runtime_error si el comando falla.
  nlohmann::json run(const std::string& name, const nlohmann::json& args = nlohmann::json::object());

  // Desde otro hilo: espera a pump(). Devuelve {"ok":bool, "result"|"error":...}.
  nlohmann::json call(const std::string& name, const nlohmann::json& args, int timeoutMs = 10000);

  void pump();
  // Desbloquea las llamadas en curso y rechaza las siguientes.
  void close();

 private:
  struct Job {
    std::string name;
    nlohmann::json args, reply;
    bool done = false;
  };
  std::mutex mutex_;
  std::condition_variable doneCv_;
  std::map<std::string, Handler> handlers_;
  std::deque<std::shared_ptr<Job>> queue_;
  bool closed_ = false;
};

// Imagen hacia los monitores de la interfaz. Sin receptores, la salida no paga el coste.
class MonitorBus {
 public:
  using Sink = std::function<void(int monitor, int width, int height, const std::vector<uint8_t>& rgba)>;
  using Wanted = std::function<bool()>;
  void connect(Sink sink, Wanted wanted) {
    sink_ = std::move(sink);
    wanted_ = std::move(wanted);
  }
  void disconnect() {
    sink_ = nullptr;
    wanted_ = nullptr;
  }
  bool wanted() const { return wanted_ && wanted_(); }
  void send(int monitor, int width, int height, const std::vector<uint8_t>& rgba) const {
    if (sink_) sink_(monitor, width, height, rgba);
  }

 private:
  Sink sink_;
  Wanted wanted_;
};

}  // namespace vj
