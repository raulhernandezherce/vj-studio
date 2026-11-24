#include "core/CommandBus.h"

#include <chrono>
#include <stdexcept>

using nlohmann::json;

namespace vj {

void CommandBus::add(const std::string& name, Handler fn) {
  std::lock_guard lock(mutex_);
  handlers_[name] = std::move(fn);
}

json CommandBus::run(const std::string& name, const json& args) {
  Handler fn;
  {
    std::lock_guard lock(mutex_);
    auto it = handlers_.find(name);
    if (it == handlers_.end()) throw std::runtime_error("comando desconocido: " + name);
    fn = it->second;
  }
  return fn(args);
}

json CommandBus::call(const std::string& name, const json& args, int timeoutMs) {
  auto job = std::make_shared<Job>();
  job->name = name;
  job->args = args;
  std::unique_lock lock(mutex_);
  if (closed_) return {{"ok", false}, {"error", "el motor se está cerrando"}};
  queue_.push_back(job);
  const bool done = doneCv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return job->done || closed_; });
  if (!done || !job->done) return {{"ok", false}, {"error", "el comando no respondió a tiempo"}};
  return job->reply;
}

void CommandBus::pump() {
  for (;;) {
    std::shared_ptr<Job> job;
    {
      std::lock_guard lock(mutex_);
      if (queue_.empty()) return;
      job = queue_.front();
      queue_.pop_front();
    }
    json reply;
    try {
      reply = {{"ok", true}, {"result", run(job->name, job->args)}};
    } catch (const std::exception& e) {
      reply = {{"ok", false}, {"error", e.what()}};
    }
    {
      std::lock_guard lock(mutex_);
      job->reply = std::move(reply);
      job->done = true;
    }
    doneCv_.notify_all();
  }
}

void CommandBus::close() {
  std::lock_guard lock(mutex_);
  closed_ = true;
  doneCv_.notify_all();
}

}  // namespace vj
