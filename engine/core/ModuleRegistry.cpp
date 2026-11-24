#include "core/ModuleRegistry.h"

#include <stdexcept>

namespace vj {

void ModuleRegistry::add(std::unique_ptr<IModule> module) {
  if (find(module->name())) throw std::logic_error("módulo duplicado: " + module->name());
  modules_.push_back(std::move(module));
}

IModule* ModuleRegistry::find(const std::string& name) const {
  for (const auto& m : modules_)
    if (m->name() == name) return m.get();
  return nullptr;
}

std::string ModuleRegistry::startAll(EngineContext& ctx) {
  for (const auto& m : modules_) {
    if (!m->start(ctx)) {
      stopAll();
      return m->name();
    }
    ++started_;
  }
  return {};
}

void ModuleRegistry::tickAll(double dt) {
  for (size_t i = 0; i < started_; ++i) modules_[i]->tick(dt);
}

void ModuleRegistry::stopAll() {
  while (started_ > 0) modules_[--started_]->stop();
}

std::vector<std::string> ModuleRegistry::names() const {
  std::vector<std::string> out;
  for (const auto& m : modules_) out.push_back(m->name());
  return out;
}

}  // namespace vj
