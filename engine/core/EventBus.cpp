#include "core/EventBus.h"

namespace vj {

EventBus::HandlerId EventBus::subscribe(const std::string& topic, Handler fn) {
  std::lock_guard lock(mutex_);
  subs_.push_back({nextId_, topic, std::move(fn)});
  return nextId_++;
}

void EventBus::unsubscribe(HandlerId id) {
  std::lock_guard lock(mutex_);
  std::erase_if(subs_, [id](const Sub& s) { return s.id == id; });
}

void EventBus::publish(const std::string& topic, const nlohmann::json& data) {
  std::vector<Handler> targets;
  {
    std::lock_guard lock(mutex_);
    for (const auto& s : subs_)
      if (s.topic.empty() || s.topic == topic) targets.push_back(s.fn);
  }
  for (const auto& fn : targets) fn(topic, data);
}

}  // namespace vj
