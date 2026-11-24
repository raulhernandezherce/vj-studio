#pragma once
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace vj {

// Avisos que no son un cambio de parámetro (clip cargado, error de decodificación...).
class EventBus {
 public:
  using Handler = std::function<void(const std::string& topic, const nlohmann::json& data)>;
  using HandlerId = uint64_t;

  // topic vacío: recibe todos los eventos.
  HandlerId subscribe(const std::string& topic, Handler fn);
  void unsubscribe(HandlerId id);
  void publish(const std::string& topic, const nlohmann::json& data = {});

 private:
  struct Sub {
    HandlerId id;
    std::string topic;
    Handler fn;
  };
  std::mutex mutex_;
  std::vector<Sub> subs_;
  HandlerId nextId_ = 1;
};

}  // namespace vj
