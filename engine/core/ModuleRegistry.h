#pragma once
#include <memory>
#include <string>
#include <vector>

#include "core/Module.h"

namespace vj {

// Arranca los módulos en orden de registro y los detiene en orden inverso.
class ModuleRegistry {
 public:
  void add(std::unique_ptr<IModule> module);
  IModule* find(const std::string& name) const;

  template <typename T>
  T* get(const std::string& name) const {
    return dynamic_cast<T*>(find(name));
  }

  // Devuelve el nombre del módulo que falló, o cadena vacía si todo arrancó.
  std::string startAll(EngineContext& ctx);
  void tickAll(double dt);
  void stopAll();
  std::vector<std::string> names() const;

 private:
  std::vector<std::unique_ptr<IModule>> modules_;
  size_t started_ = 0;
};

}  // namespace vj
