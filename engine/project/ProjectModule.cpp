#include "project/ProjectModule.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>

#include "compositor/CompositionModule.h"
#include "control/ApiDispatcher.h"
#include "core/CommandBus.h"
#include "core/ModuleRegistry.h"
#include "core/generated/Parameters.gen.h"
#include "media/IMediaSource.h"
#include "animation/Animator.h"
#include "project/Document.h"
#include "project/Restructure.h"

using nlohmann::json;
namespace fs = std::filesystem;

namespace vj::project {
namespace {

constexpr size_t kHistoryLimit = 256;
constexpr double kCoalesceSeconds = 0.6;

bool storable(const ParamDesc& d) { return !d.readOnly && d.type != ParamType::Trigger; }
bool isCellPath(const std::string& p) { return p.find("/clips/") != std::string::npos || p.find("/effects/e") != std::string::npos; }
// Estado de trabajo, no del documento.
bool transient(const std::string& p) { return p == "/composition/activelayer" || p.ends_with("/transport/position"); }

}  // namespace

std::string ProjectModule::folder(const std::string& name) const { return (fs::path(documents_) / name).string(); }

bool ProjectModule::start(EngineContext& ctx) {
  ctx_ = &ctx;
  auto* module = ctx.modules.get<compositor::CompositionModule>("composition");
  if (!module || !ctx.commands) return false;
  comp_ = module->composition();
  animator_ = ctx.modules.get<animation::Animator>("animator");

  std::error_code ec;
  for (const char* d : {"Compositions", "Recorded", "Presets/Interface", "Presets/Effects", "Extra Effects", "Fixture Library", "Preferences", "Shortcuts"})
    fs::create_directories(fs::path(documents_) / d, ec);
  ctx.params.publish("/project/folder", documents_);
  loadPreferences();
  comp_->effects().catalog().scanIsf(folder("Extra Effects"));
  newDocument();
  registerCommands();

  listeners_.push_back(ctx.params.addListener("", [this](const ParamDesc& d, const Value& v) {
    if (d.path.rfind("/preferences/", 0) == 0) prefsDirty_ = true;
    static const std::regex knob(R"(.*/dashboard/k[1-8])");
    if (d.type == ParamType::Float && d.path.find("/dashboard/k") != std::string::npos && std::regex_match(d.path, knob))
      applyDashboard(d.path, std::get<double>(v));
    if (restoring_ || d.readOnly || d.type == ParamType::Trigger || ParameterRegistry::isQuiet()) return;
    if (d.path.rfind("/composition/", 0) == 0 && !transient(d.path)) dirty_ = true;
  }));
  return true;
}

void ProjectModule::stop() {
  for (auto id : listeners_) ctx_->params.removeListener(id);
  listeners_.clear();
  if (prefsDirty_) savePreferences();
  // Salida limpia: no hay nada que recuperar en el próximo arranque.
  std::error_code ec;
  fs::remove(recoveryFile(), ec);
}

std::string ProjectModule::recoveryFile() const { return (fs::path(documents_) / ".recovery" / (std::string("recovery") + kExtension)).string(); }

void ProjectModule::tick(double dt) {
  clock_ += dt;
  ctx_->params.publish("/project/dirty", dirty_.load());
  refireClips();
  if (prefsDirty_.exchange(false)) savePreferences();

  // El primer tick llega con todos los módulos en marcha: ya existen efectos, parches y salidas.
  if (!recoveryChecked_) {
    recoveryChecked_ = true;
    if (fs::exists(recoveryFile())) {
      try {
        json doc = readDocument(recoveryFile());
        const std::string original = doc.value("recoveryPath", "");
        restore(doc, true);
        setPath(original);
        setDirty(true);
        ctx_->params.publish("/project/recovered", true);
      } catch (const std::exception&) {
        // Una copia dañada no debe impedir arrancar.
      }
    }
  }
  sinceRecovery_ += dt;
  if (dirty_ && sinceRecovery_ >= kRecoveryInterval) {
    sinceRecovery_ = 0;
    try {
      json doc = capture();
      doc["recoveryPath"] = path_;
      std::error_code ec;
      fs::create_directories(fs::path(recoveryFile()).parent_path(), ec);
      writeDocument(recoveryFile(), doc);
    } catch (const std::exception&) {
      // Sin espacio en disco, por ejemplo: la función sigue, solo se pierde la copia.
    }
  }
}

// --- Celdas y decks ------------------------------------------------------------

json ProjectModule::cellValues(int layer, int column) {
  const std::string prefix = comp_->clipPath(layer, column) + "/";
  json out = json::object();
  auto& p = ctx_->params;
  for (const auto& d : schema::clipParameters()) {
    if (!storable(d) || transient(prefix + d.path)) continue;
    auto v = p.get(prefix + d.path);
    if (v && *v != d.defaultValue) out[d.path] = control::toJson(*v);
  }
  if (p.getBool(prefix + "thumbnailcustom")) out["thumbnail"] = p.getString(prefix + "thumbnail");
  const std::string entity = comp_->clipPath(layer, column);
  if (!comp_->effects().empty(entity)) out["effects"] = comp_->effects().save(entity);
  if (animator_) {
    json anims = animator_->save(entity + "/");
    if (!anims.empty()) out["animations"] = std::move(anims);
  }
  return out;
}

void ProjectModule::applyCell(int layer, int column, const json& values) {
  const std::string prefix = comp_->clipPath(layer, column) + "/";
  auto& p = ctx_->params;
  for (const auto& d : schema::clipParameters()) {
    if (!storable(d) || d.path == "path") continue;
    const auto v = values.contains(d.path) ? control::fromJson(values[d.path]) : std::optional<Value>();
    p.set(prefix + d.path, v.value_or(d.defaultValue));
  }
  const bool custom = values.contains("thumbnail");
  p.publish(prefix + "thumbnailcustom", custom);
  if (custom) p.publish(prefix + "thumbnail", values["thumbnail"].get<std::string>());
  p.set(prefix + "path", values.value("path", std::string{}));
  const std::string entity = comp_->clipPath(layer, column);
  comp_->effects().load(entity, values.value("effects", json::array()));
  if (animator_) {
    animator_->clearPrefix(entity + "/");
    animator_->load(entity + "/", values.value("animations", json::object()));
  }
}

void ProjectModule::storeCurrentDeck() {
  const auto& shape = comp_->shape();
  for (int l = 0; l < shape.layers; ++l)
    for (int c = 0; c < shape.columns; ++c) {
      const std::string k = key(l, c);
      json values = cellValues(l, c);
      const bool filled = values.contains("path");
      if (values.value("persistent", false)) {
        persistent_[k] = std::move(values);
        continue;
      }
      persistent_.erase(k);
      if (filled) deck().cells[k] = std::move(values);
      else deck().cells.erase(k);
    }
}

void ProjectModule::applyCurrentDeck() {
  const auto& shape = comp_->shape();
  for (int l = 0; l < shape.layers; ++l)
    for (int c = 0; c < shape.columns; ++c) {
      const std::string k = key(l, c);
      auto own = deck().cells.find(k);
      auto pinned = persistent_.find(k);
      const json empty = json::object();
      applyCell(l, c, pinned != persistent_.end() ? pinned->second : own != deck().cells.end() ? own->second : empty);
      const bool covered = pinned != persistent_.end() && own != deck().cells.end();
      ctx_->params.publish(comp_->clipPath(l, c) + "/covered", covered ? own->second.value("path", "") : std::string{});
    }
}

void ProjectModule::selectDeck(int index) {
  if (index < 0 || index >= static_cast<int>(decks_.size())) throw std::runtime_error("deck inexistente");
  if (index == current_) return;
  const bool was = restoring_.exchange(true);
  storeCurrentDeck();
  comp_->freezeSlots([this](int l, int c) { return persistent_.count(key(l, c)) > 0; });
  current_ = index;
  comp_->setDeck(deck().id);
  applyCurrentDeck();
  comp_->thawSlots(deck().id);
  publishDecks();
  restoring_ = was;
}

void ProjectModule::publishDecks() {
  json list = json::array();
  for (size_t i = 0; i < decks_.size(); ++i) {
    const Deck& d = decks_[i];
    list.push_back({{"index", i}, {"id", d.id}, {"name", d.name}, {"color", d.color}, {"closed", d.closed}, {"clips", d.cells.size()}});
  }
  ctx_->params.publish("/project/decks", list.dump());
  ctx_->params.publish("/composition/deck", int64_t{current_ + 1});
}

// --- Documento -----------------------------------------------------------------

json ProjectModule::capture() {
  storeCurrentDeck();
  json doc = {{"format", kFormatName}, {"version", kFormatVersion}};
  json values = json::object();
  for (const auto& d : ctx_->params.list()) {
    if (!storable(d) || d.path.rfind("/composition/", 0) != 0 || isCellPath(d.path) || transient(d.path)) continue;
    auto v = ctx_->params.get(d.path);
    if (v && *v != d.defaultValue) values[d.path] = control::toJson(*v);
  }
  doc["values"] = std::move(values);
  doc["shape"] = {{"layers", comp_->shape().layers}, {"columns", comp_->shape().columns}};
  json chains = json::object();
  for (const std::string& e : chainEntities())
    if (!comp_->effects().empty(e)) chains[e] = comp_->effects().save(e);
  doc["effects"] = std::move(chains);
  if (animator_) doc["animations"] = animator_->save("", [](const std::string& p) { return p.find("/clips/") == std::string::npos; });
  json decks = json::array();
  for (const auto& d : decks_) {
    json cells = json::object();
    for (const auto& [k, v] : d.cells) cells[k] = v;
    decks.push_back({{"id", d.id}, {"name", d.name}, {"color", d.color}, {"closed", d.closed}, {"cells", std::move(cells)}});
  }
  doc["decks"] = std::move(decks);
  json pinned = json::object();
  for (const auto& [k, v] : persistent_) pinned[k] = v;
  doc["persistent"] = std::move(pinned);
  doc["currentDeck"] = current_;
  doc["nextDeckId"] = nextDeckId_;
  return doc;
}

void ProjectModule::restore(const json& doc, bool fresh) {
  const bool was = restoring_.exchange(true);
  if (fresh) comp_->ejectAll();
  // Una composición abierta nunca recorta la matriz: solo la amplía si hace falta.
  if (fresh && doc.contains("shape")) {
    const int layers = std::min(doc["shape"].value("layers", 0), compositor::kMaxLayers);
    const int columns = std::min(doc["shape"].value("columns", 0), compositor::kMaxColumns);
    while (comp_->shape().layers < layers) comp_->addLayer();
    while (comp_->shape().columns < columns) comp_->addColumn();
  }
  const json values = doc.value("values", json::object());
  for (const auto& d : ctx_->params.list()) {
    if (!storable(d) || d.path.rfind("/composition/", 0) != 0 || isCellPath(d.path) || transient(d.path)) continue;
    const auto v = values.contains(d.path) ? control::fromJson(values[d.path]) : std::optional<Value>();
    ctx_->params.set(d.path, v.value_or(d.defaultValue));
  }
  const json chains = doc.value("effects", json::object());
  for (const std::string& e : chainEntities()) comp_->effects().load(e, chains.value(e, json::array()));
  if (animator_) {
    animator_->clearPrefix("", [](const std::string& p) { return p.find("/clips/") == std::string::npos; });
    animator_->load("", doc.value("animations", json::object()));
  }
  decks_.clear();
  for (const auto& d : doc.at("decks")) {
    Deck deck;
    deck.id = d.value("id", 1);
    deck.name = d.value("name", "Deck");
    deck.color = d.value("color", "None");
    deck.closed = d.value("closed", false);
    const json cells = d.value("cells", json::object());
    for (const auto& [k, v] : cells.items()) deck.cells[k] = v;
    decks_.push_back(std::move(deck));
  }
  if (decks_.empty()) throw std::runtime_error("el proyecto no tiene decks");
  persistent_.clear();
  const json pinned = doc.value("persistent", json::object());
  for (const auto& [k, v] : pinned.items()) persistent_[k] = v;
  current_ = std::clamp(doc.value("currentDeck", 0), 0, static_cast<int>(decks_.size()) - 1);
  nextDeckId_ = doc.value("nextDeckId", static_cast<int>(decks_.size()) + 1);
  comp_->setDeck(deck().id);
  applyCurrentDeck();
  comp_->thawSlots(deck().id);
  publishDecks();
  if (animator_ && fresh) animator_->compositionLoaded();
  restoring_ = was;
}

std::vector<std::string> ProjectModule::chainEntities() const {
  std::vector<std::string> out = {"/composition"};
  for (int l = 0; l < comp_->shape().layers; ++l) out.push_back(comp_->layerPath(l));
  for (int g = 1; g <= comp_->shape().groups; ++g) out.push_back("/composition/groups/" + std::to_string(g));
  return out;
}

void ProjectModule::newDocument() {
  json doc = {{"format", kFormatName}, {"version", kFormatVersion}, {"values", json::object()},
              {"decks", json::array({{{"id", 1}, {"name", "Deck 1"}, {"cells", json::object()}}})},
              {"persistent", json::object()}, {"currentDeck", 0}, {"nextDeckId", 2}};
  restore(doc, true);
  {
    std::lock_guard lock(historyMutex_);
    undo_.clear();
    redo_.clear();
  }
  publishHistory();
  setPath({});
  setDirty(false);
}

void ProjectModule::setDirty(bool dirty) {
  dirty_ = dirty;
  ctx_->params.publish("/project/dirty", dirty);
}

void ProjectModule::setPath(const std::string& path) {
  path_ = path;
  ctx_->params.publish("/project/path", path);
  ctx_->params.publish("/project/name", path.empty() ? std::string("Sin título") : fs::path(path).stem().string());
}

// --- Historial -----------------------------------------------------------------

void ProjectModule::pushUndo(Entry entry) {
  {
    std::lock_guard lock(historyMutex_);
    undo_.push_back(std::move(entry));
    if (undo_.size() > kHistoryLimit) undo_.pop_front();
    redo_.clear();
  }
  publishHistory();
}

void ProjectModule::publishHistory() {
  bool canUndo, canRedo;
  {
    std::lock_guard lock(historyMutex_);
    canUndo = !undo_.empty();
    canRedo = !redo_.empty();
  }
  ctx_->params.publish("/project/canundo", canUndo);
  ctx_->params.publish("/project/canredo", canRedo);
}

void ProjectModule::recordSet(const std::string& path, const Value& before, const Value& after) {
  if (transient(path) || path.rfind("/composition/", 0) != 0) return;
  {
    // Un deslizador arrastrado es una sola operación.
    std::lock_guard lock(historyMutex_);
    if (!undo_.empty() && undo_.back().isSet && undo_.back().path == path && clock_ - undo_.back().time < kCoalesceSeconds) {
      undo_.back().after = after;
      undo_.back().time = clock_;
      redo_.clear();
      return;
    }
  }
  Entry e;
  e.isSet = true;
  e.path = path;
  e.label = path;
  e.before = before;
  e.after = after;
  e.deck = decks_[current_].id;
  e.time = clock_;
  pushUndo(std::move(e));
}

void ProjectModule::mutate(const std::string& label, const std::function<void()>& fn) {
  Entry e;
  e.document = capture();
  e.label = label;
  fn();
  pushUndo(std::move(e));
  dirty_ = true;
}

json ProjectModule::screensDocument() {
  try {
    return ctx_->commands->run("screens.get");
  } catch (const std::exception&) {
    return nullptr;  // sin módulo de salida, como en las pruebas de proyecto
  }
}

void ProjectModule::fitShape(const json& doc) {
  const int layers = doc["shape"].value("layers", comp_->shape().layers), columns = doc["shape"].value("columns", comp_->shape().columns);
  comp_->ejectAll();
  while (comp_->shape().layers < layers) comp_->addLayer();
  while (comp_->shape().columns < columns) comp_->addColumn();
  while (comp_->shape().layers > layers) comp_->removeLastLayer();
  while (comp_->shape().columns > columns) comp_->removeLastColumn();
}

void ProjectModule::restoreStructure(const json& doc) {
  fitShape(doc);
  restore(doc, true);
  if (doc.contains("screens") && doc["screens"].is_object()) ctx_->commands->run("screens.set", {{"setup", doc["screens"]}, {"history", false}});
}

void ProjectModule::restructure(const std::string& label, const std::vector<int>& layers, const std::vector<int>& columns, int newLayers, int newColumns,
                                const std::function<void(json&)>& edit) {
  Entry e;
  e.structural = true;
  e.label = label;
  e.document = capture();
  e.document["screens"] = screensDocument();
  for (int l = 0; l < comp_->shape().layers; ++l) {
    const int active = static_cast<int>(ctx_->params.getInt(comp_->layerPath(l) + "/activeclip", 0));
    if (active > 0 && layers[l] >= 0 && columns[active - 1] >= 0) refire_.push_back({layers[l], columns[active - 1], clock_ + 5});
  }
  json screens = e.document["screens"];
  if (screens.is_object())
    for (auto& screen : screens["screens"])
      for (auto& slice : screen["slices"]) {
        const int source = slice.value("sourceIndex", 0);
        if (slice.value("sourceType", "") != "layer" || source < 1 || source > static_cast<int>(layers.size())) continue;
        const int moved = layers[source - 1];
        slice["sourceType"] = moved < 0 ? "composition" : "layer";
        slice["sourceIndex"] = moved < 0 ? 0 : moved + 1;
      }
  json base = e.document;
  base.erase("screens");
  json doc = remapDocument(base, layers, columns, newLayers, newColumns);
  if (edit) edit(doc);
  doc["screens"] = std::move(screens);
  const int activeLayer = static_cast<int>(ctx_->params.getInt("/composition/activelayer", 1)) - 1;
  restoreStructure(doc);
  if (activeLayer >= 0 && activeLayer < static_cast<int>(layers.size()))
    ctx_->params.set("/composition/activelayer", int64_t{std::max(layers[activeLayer], 0) + 1});
  pushUndo(std::move(e));
  dirty_ = true;
}

// Al restaurar se expulsa todo: lo que sonaba vuelve a dispararse cuando su clip ha cargado.
void ProjectModule::refireClips() {
  for (auto it = refire_.begin(); it != refire_.end();) {
    if (comp_->hasClip(it->layer, it->column)) ctx_->params.trigger(comp_->clipPath(it->layer, it->column) + "/connect");
    else if (clock_ < it->until) {
      ++it;
      continue;
    }
    it = refire_.erase(it);
  }
}

void ProjectModule::undo() {
  Entry e;
  {
    std::lock_guard lock(historyMutex_);
    if (undo_.empty()) return;
    e = std::move(undo_.back());
    undo_.pop_back();
  }
  const bool was = restoring_.exchange(true);
  if (e.isSet) {
    // Lleva al deck donde se hizo el cambio, para que se vea qué se deshace.
    if (isCellPath(e.path))
      for (size_t i = 0; i < decks_.size(); ++i)
        if (decks_[i].id == e.deck) selectDeck(static_cast<int>(i));
    ctx_->params.set(e.path, e.before);
  } else if (e.structural) {
    json now = capture();
    now["screens"] = screensDocument();
    restoreStructure(e.document);
    e.document = std::move(now);
  } else {
    json now = capture();
    restore(e.document, false);
    e.document = std::move(now);
  }
  restoring_ = was;
  {
    std::lock_guard lock(historyMutex_);
    redo_.push_back(std::move(e));
  }
  dirty_ = true;
  publishHistory();
}

void ProjectModule::redo() {
  Entry e;
  {
    std::lock_guard lock(historyMutex_);
    if (redo_.empty()) return;
    e = std::move(redo_.back());
    redo_.pop_back();
  }
  const bool was = restoring_.exchange(true);
  if (e.isSet) {
    if (isCellPath(e.path))
      for (size_t i = 0; i < decks_.size(); ++i)
        if (decks_[i].id == e.deck) selectDeck(static_cast<int>(i));
    ctx_->params.set(e.path, e.after);
  } else if (e.structural) {
    json now = capture();
    now["screens"] = screensDocument();
    restoreStructure(e.document);
    e.document = std::move(now);
  } else {
    json now = capture();
    restore(e.document, false);
    e.document = std::move(now);
  }
  restoring_ = was;
  {
    std::lock_guard lock(historyMutex_);
    undo_.push_back(std::move(e));
  }
  dirty_ = true;
  publishHistory();
}

// --- Medios y dashboard --------------------------------------------------------

void ProjectModule::forEachPath(const std::function<void(std::string&)>& fn) {
  const auto visit = [&](json& cell) {
    if (!cell.contains("path")) return;
    std::string p = cell["path"];
    if (p.rfind("vj://", 0) == 0) return;
    fn(p);
    cell["path"] = p;
  };
  for (auto& d : decks_)
    for (auto& [_, cell] : d.cells) visit(cell);
  for (auto& [_, cell] : persistent_) visit(cell);
}

void ProjectModule::applyDashboard(const std::string& knob, double value) {
  const json links = json::parse(ctx_->params.getString(knob + "/links", "[]"), nullptr, false);
  if (!links.is_array()) return;
  const std::string entity = knob.substr(0, knob.rfind("/dashboard/"));
  for (const auto& link : links) {
    std::string target = link.value("param", "");
    if (target.empty()) continue;
    if (target.front() != '/') target = entity + "/" + target;
    const auto d = ctx_->params.describe(target);
    if (!d || d->readOnly) continue;
    const double a = link.value("in", 0.0), b = link.value("out", 1.0);
    const double x = a + (b - a) * (link.value("invert", false) ? 1 - value : value);
    switch (d->type) {
      case ParamType::Float: ctx_->params.set(target, d->min + x * (d->max - d->min)); break;
      case ParamType::Int: ctx_->params.set(target, d->min + x * (d->max - d->min)); break;
      case ParamType::Bool: ctx_->params.set(target, x >= 0.5); break;
      case ParamType::Enum: {
        const size_t i = std::min(d->options.size() - 1, static_cast<size_t>(x * d->options.size()));
        ctx_->params.set(target, d->options[i]);
        break;
      }
      default: break;
    }
  }
}

void ProjectModule::loadPreferences() {
  std::ifstream in(fs::path(folder("Preferences")) / "preferences.json");
  if (!in) return;
  json doc = json::parse(in, nullptr, false);
  if (!doc.is_object()) return;
  for (const auto& [k, v] : doc.items())
    if (k.rfind("/preferences/", 0) == 0)
      if (auto value = control::fromJson(v)) ctx_->params.set(k, *value);
  prefsDirty_ = false;
}

void ProjectModule::savePreferences() {
  json doc = json::object();
  for (const auto& d : ctx_->params.list())
    if (d.path.rfind("/preferences/", 0) == 0)
      if (auto v = ctx_->params.get(d.path)) doc[d.path] = control::toJson(*v);
  try {
    writeDocument((fs::path(folder("Preferences")) / "preferences.json").string(), doc);
  } catch (const std::exception&) {
    // Unas preferencias que no se guardan no deben interrumpir la función.
  }
}

// --- Comandos ------------------------------------------------------------------

void ProjectModule::readCell(const json& a, int& layer, int& column, const char* lk, const char* ck) const {
  layer = a.at(lk).get<int>() - 1;
  column = a.at(ck).get<int>() - 1;
  const auto& s = comp_->shape();
  if (layer < 0 || layer >= s.layers || column < 0 || column >= s.columns) throw std::runtime_error("celda inexistente");
}

void ProjectModule::registerCommands() {
  auto& bus = *ctx_->commands;

  // ¿Hay contenido en esa capa o columna en algún deck? 
  const auto used = [this](int layer, int column) {
    storeCurrentDeck();
    const auto hit = [&](const std::string& k) {
      for (int l = 0; l < comp_->shape().layers; ++l)
        for (int c = 0; c < comp_->shape().columns; ++c)
          if (key(l, c) == k && (l == layer || c == column)) return true;
      return false;
    };
    for (const auto& d : decks_)
      for (const auto& [k, v] : d.cells) if (hit(k)) return true;
    for (const auto& [k, v] : persistent_) if (hit(k)) return true;
    return false;
  };
  const auto identity = [](int n) {
    std::vector<int> map(n);
    for (int i = 0; i < n; ++i) map[i] = i;
    return map;
  };
  const auto opened = [](int n, int at) {  // deja libre el índice `at`
    std::vector<int> map(n);
    for (int i = 0; i < n; ++i) map[i] = i < at ? i : i + 1;
    return map;
  };
  const auto without = [](int n, int at) {
    std::vector<int> map(n);
    for (int i = 0; i < n; ++i) map[i] = i == at ? -1 : i < at ? i : i - 1;
    return map;
  };
  const auto moved = [identity](int n, int from, int to) {
    std::vector<int> order = identity(n), map(n);
    order.erase(order.begin() + from);
    order.insert(order.begin() + to, from);
    for (int i = 0; i < n; ++i) map[order[i]] = i;
    return map;
  };
  const auto grow = [this](const char* label, const std::function<void()>& fn) {
    Entry e;
    e.structural = true;
    e.label = label;
    e.document = capture();
    e.document["screens"] = screensDocument();
    fn();
    pushUndo(std::move(e));
    setDirty(true);
  };
  const auto index = [](const json& a, const char* name, int count, int fallback) {
    const int v = a.value(name, fallback) - 1;
    if (v < 0 || v >= count) throw std::runtime_error("índice fuera de la composición");
    return v;
  };
  const auto room = [this](bool layer) {
    if (layer ? comp_->shape().layers >= compositor::kMaxLayers : comp_->shape().columns >= compositor::kMaxColumns)
      throw std::runtime_error(layer ? "máximo de capas alcanzado" : "máximo de columnas alcanzado");
  };
  bus.add("structure.addLayer", [this, grow](const json&) {
    grow("Nueva capa", [this] { comp_->addLayer(); });
    return json::object();
  });
  bus.add("structure.addColumn", [this, grow](const json&) {
    grow("Nueva columna", [this] { comp_->addColumn(); });
    return json::object();
  });
  bus.add("structure.insertLayer", [this, opened, identity, room](const json& a) {
    room(true);
    const auto [layers, columns, groups] = comp_->shape();
    const int at = std::clamp(a.value("layer", 1) - 1, 0, layers);
    restructure("Insertar capa", opened(layers, at), identity(columns), layers + 1, columns);
    return json::object();
  });
  bus.add("structure.insertColumn", [this, opened, identity, room](const json& a) {
    room(false);
    const auto [layers, columns, groups] = comp_->shape();
    const int at = std::clamp(a.value("column", 1) - 1, 0, columns);
    restructure("Insertar columna", identity(layers), opened(columns, at), layers, columns + 1);
    return json::object();
  });
  bus.add("structure.duplicateLayer", [this, opened, identity, index, room](const json& a) {
    room(true);
    const auto [layers, columns, groups] = comp_->shape();
    const int from = index(a, "layer", layers, 1);
    restructure("Duplicar capa", opened(layers, from + 1), identity(columns), layers + 1, columns, [from](json& doc) { copyLayer(doc, from, from + 1); });
    return json::object();
  });
  bus.add("structure.duplicateColumn", [this, opened, identity, index, room](const json& a) {
    room(false);
    const auto [layers, columns, groups] = comp_->shape();
    const int from = index(a, "column", columns, 1);
    restructure("Duplicar columna", identity(layers), opened(columns, from + 1), layers, columns + 1, [from](json& doc) { copyColumn(doc, from, from + 1); });
    return json::object();
  });
  bus.add("structure.moveLayer", [this, moved, identity, index](const json& a) {
    const auto [layers, columns, groups] = comp_->shape();
    const int from = index(a, "layer", layers, 1), to = index(a, "to", layers, 1);
    if (from != to) restructure("Mover capa", moved(layers, from, to), identity(columns), layers, columns);
    return json::object();
  });
  bus.add("structure.moveColumn", [this, moved, identity, index](const json& a) {
    const auto [layers, columns, groups] = comp_->shape();
    const int from = index(a, "column", columns, 1), to = index(a, "to", columns, 1);
    if (from != to) restructure("Mover columna", identity(layers), moved(columns, from, to), layers, columns);
    return json::object();
  });
  bus.add("structure.clearLayer", [this, index](const json& a) {
    const int l = index(a, "layer", comp_->shape().layers, 1);
    mutate("Vaciar capa", [&] {
      for (int c = 0; c < comp_->shape().columns; ++c) ctx_->params.trigger(comp_->clipPath(l, c) + "/clear");
    });
    return json::object();
  });
  // Sin índice quita la última, y solo si está vacía; con índice quita esa y se puede deshacer.
  bus.add("structure.removeLayer", [this, used, without, identity, grow, index](const json& a) {
    const auto [layers, columns, groups] = comp_->shape();
    if (layers <= 1) throw std::runtime_error("debe quedar una capa");
    if (a.contains("layer")) {
      restructure("Quitar capa", without(layers, index(a, "layer", layers, 1)), identity(columns), layers - 1, columns);
      return json::object();
    }
    if (used(layers - 1, -1)) throw std::runtime_error("la capa superior tiene clips: vacíala antes en todos los decks");
    grow("Quitar capa", [this] { comp_->removeLastLayer(); });
    return json::object();
  });
  bus.add("structure.removeColumn", [this, used, without, identity, grow, index](const json& a) {
    const auto [layers, columns, groups] = comp_->shape();
    if (columns <= 1) throw std::runtime_error("debe quedar una columna");
    if (a.contains("column")) {
      restructure("Quitar columna", identity(layers), without(columns, index(a, "column", columns, 1)), layers, columns - 1);
      return json::object();
    }
    if (used(-1, columns - 1)) throw std::runtime_error("la última columna tiene clips: vacíala antes en todos los decks");
    grow("Quitar columna", [this] { comp_->removeLastColumn(); });
    return json::object();
  });

  bus.add("project.new", [this](const json&) {
    newDocument();
    return json::object();
  });
  bus.add("project.save", [this](const json& a) {
    std::string path = a.value("path", path_);
    if (path.empty()) throw std::runtime_error("falta la ruta");
    if (fs::path(path).extension() != kExtension) path += kExtension;
    writeDocument(path, capture());
    setPath(path);
    setDirty(false);
    return json{{"path", path}};
  });
  bus.add("project.open", [this](const json& a) {
    const std::string path = a.at("path");
    json doc = readDocument(path);  // si falla, el proyecto abierto no se toca
    restore(doc, true);
    {
      std::lock_guard lock(historyMutex_);
      undo_.clear();
      redo_.clear();
    }
    publishHistory();
    setPath(path);
    setDirty(false);
    return json{{"path", path}};
  });
  bus.add("history.undo", [this](const json&) {
    undo();
    return json::object();
  });
  bus.add("history.redo", [this](const json&) {
    redo();
    return json::object();
  });

  bus.add("deck.add", [this](const json& a) {
    mutate("Nuevo deck", [&] {
      Deck d;
      d.id = nextDeckId_++;
      d.name = a.value("name", "Deck " + std::to_string(d.id));
      const int at = a.value("after", static_cast<int>(decks_.size()) - 1) + 1;
      storeCurrentDeck();
      decks_.insert(decks_.begin() + std::clamp(at, 0, static_cast<int>(decks_.size())), std::move(d));
      if (at <= current_) ++current_;
      publishDecks();
    });
    return json::object();
  });
  bus.add("deck.select", [this](const json& a) {
    selectDeck(a.at("index").get<int>());
    return json::object();
  });
  bus.add("deck.rename", [this](const json& a) {
    const int i = a.at("index");
    mutate("Renombrar deck", [&] {
      decks_.at(i).name = a.at("name");
      publishDecks();
    });
    return json::object();
  });
  bus.add("deck.color", [this](const json& a) {
    const int i = a.at("index");
    mutate("Color de deck", [&] {
      decks_.at(i).color = a.at("color");
      publishDecks();
    });
    return json::object();
  });
  bus.add("deck.duplicate", [this](const json& a) {
    const int i = a.at("index");
    mutate("Duplicar deck", [&] {
      storeCurrentDeck();
      Deck d = decks_.at(i);
      d.id = nextDeckId_++;
      d.name += " copia";
      decks_.insert(decks_.begin() + i + 1, std::move(d));
      if (i < current_) ++current_;
      publishDecks();
    });
    return json::object();
  });
  bus.add("deck.clear", [this](const json& a) {
    const int i = a.at("index");
    mutate("Vaciar deck", [&] {
      storeCurrentDeck();
      decks_.at(i).cells.clear();
      if (i == current_) applyCurrentDeck();
      publishDecks();
    });
    return json::object();
  });
  bus.add("deck.close", [this](const json& a) {
    const int i = a.at("index");
    mutate("Cerrar deck", [&] {
      decks_.at(i).closed = a.value("closed", true);
      publishDecks();
    });
    return json::object();
  });
  bus.add("deck.remove", [this](const json& a) {
    const int i = a.at("index");
    if (decks_.size() < 2) throw std::runtime_error("la composición necesita al menos un deck");
    mutate("Quitar deck", [&] {
      if (i == current_) selectDeck(i == 0 ? 1 : i - 1);
      decks_.erase(decks_.begin() + i);
      if (i < current_) --current_;
      publishDecks();
    });
    return json::object();
  });
  bus.add("deck.move", [this](const json& a) {
    const int from = a.at("from"), to = a.at("to");
    mutate("Mover deck", [&] {
      storeCurrentDeck();
      const int id = deck().id;
      Deck d = std::move(decks_.at(from));
      decks_.erase(decks_.begin() + from);
      decks_.insert(decks_.begin() + std::clamp(to, 0, static_cast<int>(decks_.size())), std::move(d));
      for (size_t i = 0; i < decks_.size(); ++i)
        if (decks_[i].id == id) current_ = static_cast<int>(i);
      publishDecks();
    });
    return json::object();
  });

  bus.add("clip.copy", [this](const json& a) {
    int l, c;
    readCell(a, l, c);
    clipboard_ = cellValues(l, c);
    return json::object();
  });
  bus.add("clip.cut", [this](const json& a) {
    int l, c;
    readCell(a, l, c);
    clipboard_ = cellValues(l, c);
    mutate("Cortar clip", [&] { applyCell(l, c, json::object()); });
    return json::object();
  });
  bus.add("clip.paste", [this](const json& a) {
    int l, c;
    readCell(a, l, c);
    if (!clipboard_) throw std::runtime_error("el portapapeles está vacío");
    mutate("Pegar clip", [&] { applyCell(l, c, *clipboard_); });
    return json::object();
  });
  bus.add("clip.clear", [this](const json& a) {
    int l, c;
    readCell(a, l, c);
    mutate("Vaciar clip", [&] { applyCell(l, c, json::object()); });
    return json::object();
  });
  bus.add("clip.move", [this](const json& a) {
    int l, c, tl, tc;
    readCell(a, l, c);
    readCell(a, tl, tc, "toLayer", "toColumn");
    const bool copy = a.value("copy", false);
    mutate(copy ? "Copiar clip" : "Mover clip", [&] {
      const json from = cellValues(l, c), to = cellValues(tl, tc);
      applyCell(tl, tc, from);
      if (!copy) applyCell(l, c, to);  // sobre otro clip, intercambian posición
    });
    return json::object();
  });
  bus.add("clip.releaseCovered", [this](const json& a) {
    int l, c;
    readCell(a, l, c);
    const std::string k = key(l, c);
    auto own = deck().cells.find(k);
    if (own == deck().cells.end() || !persistent_.count(k)) throw std::runtime_error("no hay clip tapado");
    int target = -1;
    if (a.contains("toColumn")) target = a["toColumn"].get<int>() - 1;
    else
      for (int t = 0; t < comp_->shape().columns && target < 0; ++t)
        if (!comp_->hasClip(l, t) && ctx_->params.getString(comp_->clipPath(l, t) + "/path").empty()) target = t;
    if (target < 0) throw std::runtime_error("no queda ninguna celda libre en la capa");
    mutate("Liberar clip tapado", [&] {
      const json values = own->second;
      deck().cells.erase(k);
      applyCell(l, target, values);
      ctx_->params.publish(comp_->clipPath(l, c) + "/covered", std::string{});
    });
    return json::object();
  });
  bus.add("clip.thumbnail", [this](const json& a) {
    int l, c;
    readCell(a, l, c);
    const std::string prefix = comp_->clipPath(l, c) + "/";
    const std::string mode = a.at("mode");
    std::string url;
    if (mode == "current") {
      url = comp_->makeThumbnail(l, c);
    } else if (mode == "file") {
      std::string error;
      auto src = media::openMedia(a.at("path"), error);
      if (!src) throw std::runtime_error(error);
      auto f = src->frameAt(0);
      if (f) url = media::thumbnailDataUrl(*f);
    } else if (mode == "revert") {
      ctx_->params.publish(prefix + "thumbnailcustom", false);
      ctx_->params.publish(prefix + "thumbnail", comp_->originalThumbnail(ctx_->params.getString(prefix + "path")));
      dirty_ = true;
      return json::object();
    }
    if (url.empty()) throw std::runtime_error("no hay imagen para la miniatura");
    ctx_->params.publish(prefix + "thumbnailcustom", true);
    ctx_->params.publish(prefix + "thumbnail", url);
    dirty_ = true;
    return json::object();
  });

  bus.add("preview.clip", [this](const json& a) {
    int l, c;
    readCell(a, l, c);
    comp_->previewClip(l, c);
    return json::object();
  });
  bus.add("preview.file", [this](const json& a) {
    comp_->previewFile(a.at("path"));
    return json::object();
  });
  bus.add("preview.stop", [this](const json&) {
    comp_->previewStop();
    return json::object();
  });

  bus.add("output.snapshot", [this](const json&) {
    const std::time_t now = std::time(nullptr);
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", std::localtime(&now));
    std::string path = (fs::path(folder("Recorded")) / (std::string("snapshot-") + stamp + ".png")).string();
    for (int n = 2; fs::exists(path); ++n)
      path = (fs::path(folder("Recorded")) / (std::string("snapshot-") + stamp + "-" + std::to_string(n) + ".png")).string();
    ctx_->commands->run("output.capture", {{"path", path}});
    // Se importa en el primer hueco libre de la capa inferior.
    int column = 0;
    for (int c = 1; c <= comp_->shape().columns && !column; ++c)
      if (ctx_->params.getString(comp_->clipPath(0, c - 1) + "/path").empty()) column = c;
    if (column) ctx_->params.set(comp_->clipPath(0, column - 1) + "/path", path);
    return json{{"path", path}, {"column", column}};
  });

  bus.add("media.list", [this](const json&) {
    storeCurrentDeck();
    std::map<std::string, int> uses;
    forEachPath([&](std::string& p) { ++uses[p]; });
    json list = json::array();
    for (const auto& [p, n] : uses) list.push_back({{"path", p}, {"exists", fs::exists(p)}, {"uses", n}});
    return list;
  });
  bus.add("media.relink", [this](const json& a) {
    const std::string from = a.at("from"), to = a.at("to");
    if (!fs::exists(to)) throw std::runtime_error("la nueva ubicación no existe");
    const bool all = a.value("all", true);
    int fixed = 0;
    mutate("Reenlazar medios", [&] {
      storeCurrentDeck();
      std::map<std::string, std::string> changes;
      forEachPath([&](std::string& p) {
        if (changes.count(p)) {
          p = changes[p];
          return;
        }
        std::string next;
        if (p == from) next = to;
        else if (all && !fs::exists(p)) {
          next = siblingPath(from, to, p);
          if (!next.empty() && !fs::exists(next)) next.clear();
        }
        if (next.empty()) return;
        changes[p] = next;
        p = next;
      });
      fixed = static_cast<int>(changes.size());
      applyCurrentDeck();
      publishDecks();
    });
    return json{{"fixed", fixed}};
  });
  bus.add("media.collect", [this](const json& a) {
    const fs::path dir = a.at("dir").get<std::string>();
    json doc = capture();
    int copied = 0;
    const auto collect = [&](json& cell, const std::string& group) {
      if (!cell.contains("path")) return;
      const std::string p = cell["path"];
      if (p.rfind("vj://", 0) == 0 || !fs::exists(p)) return;
      fs::path target = dir / "Media" / group / fs::path(p).filename();
      fs::create_directories(target.parent_path());
      for (int n = 2; fs::exists(target); ++n)
        target = dir / "Media" / group / (fs::path(p).stem().string() + "-" + std::to_string(n) + fs::path(p).extension().string());
      fs::copy_file(p, target);  // siempre copia; nunca borra el original
      cell["path"] = target.string();
      ++copied;
    };
    for (auto& d : doc["decks"]) {
      std::string name = d.value("name", "Deck");
      std::replace(name.begin(), name.end(), '/', '-');
      for (auto& [_, cell] : d["cells"].items()) collect(cell, name);
    }
    for (auto& [_, cell] : doc["persistent"].items()) collect(cell, "Persistentes");
    const std::string name = path_.empty() ? std::string("Sin título") : fs::path(path_).stem().string();
    const fs::path out = dir / (name + kExtension);
    writeDocument(out.string(), doc);
    return json{{"path", out.string()}, {"copied", copied}};
  });

  const auto entityArg = [](const json& a) {
    const std::string e = a.at("entity");
    if (e != "/composition" && e.rfind("/composition/", 0) != 0) throw std::runtime_error("entidad no válida");
    return e;
  };
  bus.add("effects.catalog", [this](const json&) {
    json out = json::array();
    for (const auto& t : comp_->effects().catalog().list())
      out.push_back({{"id", t.id}, {"name", t.name}, {"category", t.category}, {"source", t.source}});
    return out;
  });
  bus.add("effects.rescan", [this](const json&) {
    json errors = comp_->effects().catalog().scanIsf(folder("Extra Effects"));
    // FFGL es un módulo aparte: si está, añade sus propios resultados.
    try {
      for (const auto& e : ctx_->commands->run("ffgl.rescan")) errors.push_back(e);
    } catch (const std::runtime_error&) {
    }
    return errors;
  });
  bus.add("effects.list", [this, entityArg](const json& a) { return comp_->effects().describe(entityArg(a)); });
  bus.add("effect.add", [this, entityArg](const json& a) {
    int id = 0;
    mutate("Añadir efecto", [&] { id = comp_->effects().add(entityArg(a), a.at("type"), a.value("index", -1)); });
    return json{{"id", id}};
  });
  bus.add("effect.remove", [this, entityArg](const json& a) {
    mutate("Quitar efecto", [&] { comp_->effects().remove(entityArg(a), a.at("id")); });
    return json::object();
  });
  bus.add("effect.move", [this, entityArg](const json& a) {
    mutate("Mover efecto", [&] { comp_->effects().move(entityArg(a), a.at("id"), a.at("index")); });
    return json::object();
  });
  bus.add("effects.copy", [this, entityArg](const json& a) {
    effectClipboard_ = comp_->effects().save(entityArg(a));
    return json{{"count", effectClipboard_.size()}};
  });
  bus.add("effects.paste", [this, entityArg](const json& a) {
    if (effectClipboard_.empty()) throw std::runtime_error("no hay efectos copiados");
    mutate("Pegar efectos", [&] {
      const std::string e = entityArg(a);
      json chain = comp_->effects().save(e);
      for (json item : effectClipboard_) {
        item.erase("id");
        chain.push_back(item);
      }
      comp_->effects().load(e, chain);
    });
    return json::object();
  });
  const auto presetFile = [this](const std::string& kind, const std::string& name) {
    std::string clean = name;
    std::replace_if(clean.begin(), clean.end(), [](char c) { return c == '/' || c == '\\' || c == ':'; }, '-');
    if (clean.empty() || clean.front() == '.') throw std::runtime_error("nombre de preset no válido");
    const fs::path dir = fs::path(folder("Presets")) / "Effects" / kind;
    fs::create_directories(dir);
    return (dir / (clean + ".json")).string();
  };
  bus.add("preset.save", [this, entityArg, presetFile](const json& a) {
    const json chain = comp_->effects().save(entityArg(a));
    const int id = a.value("id", 0);
    json doc = json::array();
    for (const auto& e : chain)
      if (id == 0 || e.value("id", 0) == id) doc.push_back({{"type", e["type"]}, {"values", e["values"]}});
    if (doc.empty()) throw std::runtime_error("no hay efectos que guardar");
    // Un preset de un efecto va en la carpeta de su tipo; uno de cadena, en Chains.
    writeDocument(presetFile(id ? doc[0]["type"].get<std::string>() : "Chains", a.at("name")), doc);
    return json::object();
  });
  bus.add("preset.list", [this](const json& a) {
    json out = json::array();
    std::error_code ec;
    const fs::path dir = fs::path(folder("Presets")) / "Effects" / a.value("kind", std::string("Chains"));
    for (const auto& f : fs::directory_iterator(dir, ec))
      if (f.path().extension() == ".json") out.push_back(f.path().stem().string());
    return out;
  });
  bus.add("preset.apply", [this, entityArg, presetFile](const json& a) {
    const std::string e = entityArg(a), kind = a.value("kind", std::string("Chains"));
    std::ifstream in(presetFile(kind, a.at("name")));
    json doc = json::parse(in, nullptr, false);
    if (!doc.is_array()) throw std::runtime_error("preset dañado");
    const int id = a.value("id", 0);
    mutate("Aplicar preset", [&] {
      json chain = comp_->effects().save(e);
      if (id) {
        // Sobre un efecto: copia los valores una vez; el efecto no recuerda el preset.
        for (auto& item : chain)
          if (item.value("id", 0) == id) item["values"] = doc.at(0).at("values");
      } else {
        for (const auto& item : doc) chain.push_back(item);
      }
      comp_->effects().load(e, chain);
    });
    return json::object();
  });
  bus.add("source.add", [this](const json& a) {
    int l, c;
    readCell(a, l, c);
    const std::string type = a.at("type");
    const auto* t = comp_->effects().catalog().find(type);
    if (!t || !t->source) throw std::runtime_error("no es una fuente");
    mutate("Añadir fuente", [&] {
      const std::string entity = comp_->clipPath(l, c);
      ctx_->params.set(entity + "/path", "vj://source/" + type);
      ctx_->params.set(entity + "/resize", std::string("Stretch"));
      comp_->effects().clear(entity);
      comp_->effects().add(entity, type);
    });
    return json::object();
  });

  bus.add("animation.set", [this](const json& a) {
    if (!animator_) throw std::runtime_error("animación no disponible");
    mutate("Animar parámetro", [&] { animator_->set(a.at("path"), a.at("config")); });
    return json::object();
  });
  bus.add("animation.remove", [this](const json& a) {
    mutate("Quitar animación", [&] { animator_->remove(a.at("path")); });
    return json::object();
  });
  bus.add("animation.get", [this](const json& a) {
    const auto c = animator_->get(a.at("path"));
    return c ? *c : json();
  });
  bus.add("animation.list", [this](const json& a) { return animator_->save(a.value("prefix", std::string{})); });
  bus.add("animation.start", [this](const json& a) {
    animator_->startNow(a.at("path"));
    return json::object();
  });
  bus.add("animation.preset.save", [this, presetFile](const json& a) {
    const auto c = animator_->get(a.at("path"));
    if (!c) throw std::runtime_error("sin animación");
    json doc = *c;
    // Como en el manual: el preset no guarda los puntos de entrada y salida.
    doc.erase("in");
    doc.erase("out");
    writeDocument(presetFile("Animation", a.at("name")), doc);
    return json::object();
  });
  bus.add("animation.preset.apply", [this, presetFile](const json& a) {
    std::ifstream in(presetFile("Animation", a.at("name")));
    json doc = json::parse(in, nullptr, false);
    if (!doc.is_object()) throw std::runtime_error("preset dañado");
    const std::string path = a.at("path");
    if (const auto c = animator_->get(path)) {
      if (c->contains("in")) doc["in"] = (*c)["in"];
      if (c->contains("out")) doc["out"] = (*c)["out"];
    }
    mutate("Aplicar preset de animación", [&] { animator_->set(path, doc); });
    return json::object();
  });

  bus.add("dashboard.assign", [this](const json& a) {
    const std::string knob = a.at("knob");
    std::string param = a.at("param");
    if (!ctx_->params.describe(knob) || !ctx_->params.describe(param)) throw std::runtime_error("parámetro desconocido");
    const std::string entity = knob.substr(0, knob.rfind("/dashboard/")) + "/";
    if (param.rfind(entity, 0) == 0) param = param.substr(entity.size());
    json links = json::parse(ctx_->params.getString(knob + "/links", "[]"));
    for (const auto& l : links)
      if (l.value("param", "") == param) return links;
    links.push_back({{"param", param}, {"in", a.value("in", 0.0)}, {"out", a.value("out", 1.0)}, {"invert", a.value("invert", false)}});
    ctx_->params.set(knob + "/links", links.dump());
    return links;
  });
  bus.add("dashboard.unassign", [this](const json& a) {
    const std::string knob = a.at("knob"), param = a.at("param");
    json links = json::parse(ctx_->params.getString(knob + "/links", "[]"));
    links.erase(std::remove_if(links.begin(), links.end(), [&](const json& l) { return l.value("param", "") == param; }), links.end());
    ctx_->params.set(knob + "/links", links.dump());
    return links;
  });
}

}  // namespace vj::project
