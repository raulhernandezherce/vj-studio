#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "core/Parameter.h"

namespace vj {

enum class ParamError { None, UnknownPath, ReadOnly, TypeMismatch, NotATrigger, IsTrigger };
const char* describe(ParamError e);

// Registro único de parámetros. Seguro entre hilos: los oyentes se invocan fuera
// del bloqueo, en el hilo que hizo el cambio.
class ParameterRegistry {
 public:
  using Listener = std::function<void(const ParamDesc&, const Value&)>;
  using ListenerId = uint64_t;

  void define(ParamDesc desc);
  void defineAll(const std::vector<ParamDesc>& table);
  // Crea una copia de una plantilla del esquema bajo un prefijo.
  void instantiate(const std::string& prefix, const std::vector<ParamDesc>& tmpl);

  // Quita todos los parámetros bajo un prefijo (instancias de efecto).
  void removePrefix(const std::string& prefix);

  std::optional<ParamDesc> describe(const std::string& path) const;
  std::vector<ParamDesc> list() const;
  std::optional<Value> get(const std::string& path) const;

  // Operaciones externas: respetan solo-lectura, tipo y rango.
  ParamError set(const std::string& path, const Value& value);
  ParamError reset(const std::string& path);
  ParamError trigger(const std::string& path);

  // Para el módulo dueño del parámetro: permite escribir los de solo lectura.
  ParamError publish(const std::string& path, const Value& value);

  // Escritura automática (animaciones): los oyentes pueden saberlo con isQuiet().
  ParamError setQuiet(const std::string& path, const Value& value);
  static bool isQuiet();

  // Lecturas tipadas con valor de reserva, para el bucle de render.
  double getFloat(const std::string& path, double fallback = 0.0) const;
  int64_t getInt(const std::string& path, int64_t fallback = 0) const;
  bool getBool(const std::string& path, bool fallback = false) const;
  std::string getString(const std::string& path, const std::string& fallback = {}) const;

  // path vacío: escucha todos los parámetros.
  ListenerId addListener(const std::string& path, Listener fn);
  void removeListener(ListenerId id);

 private:
  struct Entry {
    ParamDesc desc;
    Value value;
  };
  ParamError write(const std::string& path, const Value& value, bool external);
  void notify(const ParamDesc& desc, const Value& value);

  mutable std::mutex mutex_;
  std::map<std::string, Entry> entries_;
  struct Sub {
    ListenerId id;
    std::string path;
    Listener fn;
  };
  std::vector<Sub> listeners_;
  ListenerId nextId_ = 1;
};

}  // namespace vj
