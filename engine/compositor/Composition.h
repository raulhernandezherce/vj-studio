#pragma once
#include <deque>
#include <functional>
#include <future>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "audio/AudioPlan.h"
#include "compositor/ClipClock.h"
#include "compositor/EffectChains.h"
#include "compositor/RenderPlan.h"
#include "core/EventBus.h"
#include "core/ParameterRegistry.h"
#include "media/IMediaSource.h"

namespace vj::compositor {

// Topes de la estructura: el espacio se reserva al arrancar para que los vectores no se muevan
// mientras otros hilos los leen.
inline constexpr int kMaxLayers = 32;
inline constexpr int kMaxColumns = 64;

struct CompositionShape {
  int layers = 4;
  int columns = 8;
  int groups = 4;
};

// Estado vivo de la composición: disparos, relojes, transiciones y autopilot.
// No toca la GPU y el tiempo entra por dt, así que las pruebas son deterministas.
class Composition {
 public:
  Composition(ParameterRegistry& params, EventBus& events, CompositionShape shape);
  ~Composition();

  void tick(double dt);
  RenderPlan plan() const;
  const CompositionShape& shape() const { return shape_; }
  // Cambian la estructura en caliente. Quitar exige capa o columna vacía en todos los decks: lo comprueba ProjectModule.
  void addLayer();
  void addColumn();
  void removeLastLayer();
  void removeLastColumn();

  // Espera a que terminen las aperturas en curso (para pruebas y cierre).
  void waitForLoads();

  std::string layerPath(int layer) const;
  std::string clipPath(int layer, int column) const;

  // Decks: un clip que suena sigue sonando al cambiar de deck, con los ajustes que tenía.
  void freezeSlots(const std::function<bool(int layer, int column)>& keep);
  void thawSlots(int deck);
  void setDeck(int deck) { deck_ = deck; }
  void ejectAll();

  // Preview: reproduce un clip o archivo sin tocar la salida.
  void previewClip(int layer, int column);
  void previewFile(const std::string& path);
  void previewStop();
  // Render fuera de tiempo real: el reloj del preview lo marca quien renderiza.
  void previewHold(double seconds);
  void previewRelease() { previewHeld_ = false; }
  double previewLength() const { return preview_.length; }
  double previewFrameTime() const { return preview_.frame ? preview_.frame->time : -1; }
  bool previewHasDuration() const { return preview_.source && preview_.source->info().duration > 0; }
  bool previewActive() const { return preview_.active(); }
  RenderPlan previewPlan(int width, int height) const;

  media::FramePtr frameOf(int layer, int column);
  std::string makeThumbnail(int layer, int column);
  std::string originalThumbnail(const std::string& path) const;
  bool hasClip(int layer, int column) const { return cellHasClip(layer, column); }
  std::unique_ptr<audio::AudioPlan> audioPlan();
  // Capas y grupos enrutados a un slice: índice base 1 y si ignoran bypass y solo.
  void setRoutedBuses(bool a, bool b) {
    routedBus_[0] = a;
    routedBus_[1] = b;
  }
  void setRouted(std::map<int, bool> layers, std::map<int, bool> groups) {
    routedLayers_ = std::move(layers);
    routedGroups_ = std::move(groups);
  }
  // Reloj externo: fija el beat cada fotograma. Negativo devuelve el control al reloj interno.
  void setExternalBeat(double beat) { externalBeat_ = beat; }
  // Índices de análisis, compartidos con AudioModule.
  int layerTap(int layer) const { return 1 + layer; }
  int groupTap(int group) const { return 1 + kMaxLayers + group; }
  int clipTap(int layer) const { return 1 + kMaxLayers + shape_.groups + layer; }
  EffectChains& effects() { return effects_; }

 private:
  struct Loaded {
    media::SourcePtr source;
    std::string thumbnail;
    audio::AudioPtr audio;
    double audioSeconds = 0;  // clip solo de audio: da la duración
  };
  struct Clip {
    media::SourcePtr source;
    media::MediaInfo info;
    audio::AudioPtr audio;
    std::future<Loaded> opening;
    std::string openingPath, error;
    bool loaded = false;
  };
  struct Slot {
    int layer = -1, column = -1;  // celda de origen del clip
    media::SourcePtr source;
    double length = 0;
    ClipClock clock;
    ClockConfig config;
    int loops = 0;
    media::FramePtr frame;
    audio::AudioPtr audio;
    uint64_t voice = 0;
    int deck = 1;
    // Ajustes del clip cuando su deck ya no está a la vista.
    std::shared_ptr<std::map<std::string, Value>> frozen;
    PlanChain frozenEffects;
    bool active() const { return source != nullptr; }
    bool at(int l, int c) const { return layer == l && column == c && !frozen; }
  };
  struct LayerState {
    Slot current, previous;
    double elapsed = 0, duration = 0;
    int transition = 0;
    double lastOpacity = 1;
    bool faderPaused = false;
  };
  struct Pending {
    int layer, column;
    double beat;
  };

  void process(const std::string& path);
  void press(int layer, int column);
  void release(int layer, int column);
  void fire(int layer, int column, double lateSeconds);
  void ejectLayer(int layer);
  void stopSlot(Slot& s);
  void triggerColumn(int column, int onlyGroup);
  void loadClip(int layer, int column, const std::string& path);
  void clearClip(int layer, int column);
  void pumpLoads();
  void updateLayer(int index, double dt);
  void runAutopilot(int layer);
  void stepFader(int layer);
  bool cellHasClip(int layer, int column) const;
  int resolveTargetLayer(int layer, int column) const;
  double snapBeats(int layer, int column) const;
  Value value(const Slot& slot, const std::string& suffix) const;
  double sf(const Slot& slot, const std::string& suffix) const;
  int64_t si(const Slot& slot, const std::string& suffix) const;
  bool sb(const Slot& slot, const std::string& suffix) const;
  std::string ss(const Slot& slot, const std::string& suffix) const;
  void applyLoaded(int layer, int column, const Loaded& loaded, const std::string& path);
  void applyLoadDefaults(int layer, int column);
  audio::Voice makeVoice(const Slot& slot, float gainL, float gainR) const;
  void processTempo(const std::string& what);
  void restSource(const media::SourcePtr& source);
  ClockConfig clockConfig(const Slot& s) const;
  PlanClip planClip(const Slot& s) const;
  void setActiveClip(int layer);
  double crossfaderGain(const std::string& bus) const;

  double f(const std::string& p) const { return params_.getFloat(p); }
  bool b(const std::string& p) const { return params_.getBool(p); }
  int64_t i(const std::string& p) const { return params_.getInt(p); }
  std::string s(const std::string& p) const { return params_.getString(p); }

  ParameterRegistry& params_;
  EventBus& events_;
  CompositionShape shape_;
  EffectChains effects_;
  double time_ = 0;
  std::vector<LayerState> layers_;
  Slot preview_;
  std::future<Loaded> previewOpening_;
  std::string previewPath_;
  bool previewHeld_ = false;
  std::map<std::string, Loaded> sources_;  // medios ya abiertos, por ruta
  int deck_ = 1;
  uint64_t nextVoice_ = 1;
  std::vector<double> taps_;
  std::vector<std::vector<Clip>> clips_;
  std::deque<Pending> pending_;
  double beat_ = 0;
  double externalBeat_ = -1;
  std::map<int, bool> routedLayers_, routedGroups_;
  bool routedBus_[2] = {false, false};
  double sincePublish_ = 0;
  int column_ = 0;

  std::mutex queueMutex_;
  std::vector<std::string> queue_;
  ParameterRegistry::ListenerId listener_ = 0;
};

}  // namespace vj::compositor
