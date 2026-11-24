#pragma once
#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "compositor/Composition.h"
#include "core/Module.h"
#include "core/ParameterRegistry.h"

namespace vj::animation {
class Animator;
}

namespace vj::project {

// Dueño del documento: decks, guardar y abrir, historial, portapapeles y medios.
// Toda operación corre en el hilo principal, a través de CommandBus.
class ProjectModule final : public IModule {
 public:
  explicit ProjectModule(std::string documentsDir) : documents_(std::move(documentsDir)) {}
  std::string name() const override { return "project"; }
  bool start(EngineContext& ctx) override;
  void tick(double dt) override;
  void stop() override;
  std::string recoveryFile() const;

  // Anota un cambio hecho a mano. Se llama desde el hilo de control.
  void recordSet(const std::string& path, const Value& before, const Value& after);

  nlohmann::json capture();  // para pruebas y guardado

 private:
  using json = nlohmann::json;
  struct Deck {
    int id = 1;
    std::string name;
    std::string color = "None";
    bool closed = false;
    std::map<std::string, json> cells;  // "capa/columna" -> valores que difieren del defecto
  };
  struct Entry {
    bool isSet = false;
    bool structural = false;  // cambia capas o columnas: se restaura con su forma y sus pantallas
    json document;  // estado completo a restaurar
    std::string path, label;
    Value before, after;
    int deck = 1;
    double time = 0;
  };

  void registerCommands();
  void refireClips();
  std::string key(int layer, int column) const { return std::to_string(layer) + "/" + std::to_string(column); }
  json cellValues(int layer, int column);
  void applyCell(int layer, int column, const json& values);
  void storeCurrentDeck();
  void applyCurrentDeck();
  void selectDeck(int index);
  void publishDecks();
  Deck& deck() { return decks_[current_]; }
  void restore(const json& doc, bool fresh);
  void newDocument();
  std::vector<std::string> chainEntities() const;
  void setDirty(bool dirty);
  void setPath(const std::string& path);
  void mutate(const std::string& label, const std::function<void()>& fn);
  // Reordena capas y columnas. Los mapas van de índice antiguo a nuevo, base 0; -1 descarta.
  void restructure(const std::string& label, const std::vector<int>& layers, const std::vector<int>& columns, int newLayers, int newColumns,
                   const std::function<void(json&)>& edit = nullptr);
  void fitShape(const json& doc);
  void restoreStructure(const json& doc);
  json screensDocument();
  void pushUndo(Entry entry);
  void publishHistory();
  void undo();
  void redo();
  void forEachPath(const std::function<void(std::string& path)>& fn);
  void applyDashboard(const std::string& knob, double value);
  void loadPreferences();
  void savePreferences();
  std::string folder(const std::string& name) const;
  void readCell(const json& a, int& layer, int& column, const char* lk = "layer", const char* ck = "column") const;

  EngineContext* ctx_ = nullptr;
  compositor::Composition* comp_ = nullptr;
  animation::Animator* animator_ = nullptr;
  std::string documents_;
  std::vector<Deck> decks_;
  std::map<std::string, json> persistent_;
  int current_ = 0;
  int nextDeckId_ = 2;
  std::optional<json> clipboard_;
  json effectClipboard_ = json::array();
  std::string path_;

  std::mutex historyMutex_;
  std::deque<Entry> undo_, redo_;
  double clock_ = 0;
  bool recoveryChecked_ = false;
  double sinceRecovery_ = 0;
  static constexpr double kRecoveryInterval = 5;
  std::atomic<bool> dirty_{false}, restoring_{false}, prefsDirty_{false};
  struct Refire {
    int layer, column;
    double until;
  };
  std::vector<Refire> refire_;  // clips que seguían sonando antes de reordenar
  std::vector<ParameterRegistry::ListenerId> listeners_;
};

}  // namespace vj::project
