#include "compositor/Composition.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <thread>

#include "core/generated/BlendModes.gen.h"
#include "core/generated/Parameters.gen.h"

namespace vj::compositor {
namespace {

constexpr double kPublishInterval = 0.1;
constexpr double kStillLength = 10.0;  // reloj de imágenes y generadores, para Autopilot
thread_local bool tPublishing = false;

int enumIndex(const std::vector<schema::BlendMode>& t, const std::string& name) {
  for (size_t i = 0; i < t.size(); ++i)
    if (name == t[i].name) return static_cast<int>(i);
  return 0;
}
int transitionIndex(const std::string& name) {
  const auto& t = schema::transitionModes();
  for (size_t i = 0; i < t.size(); ++i)
    if (name == t[i].name) return static_cast<int>(i);
  return 0;
}
double beatsFor(const std::string& snap) {
  if (snap == "Beat") return 1;
  if (snap == "Bar") return 4;
  if (snap == "2 Bars") return 8;
  if (snap == "4 Bars") return 16;
  return 0;
}
TransformValues readTransform(const ParameterRegistry& p, const std::string& prefix) {
  TransformValues t;
  t.x = p.getFloat(prefix + "/transform/x");
  t.y = p.getFloat(prefix + "/transform/y");
  t.scale = p.getFloat(prefix + "/transform/scale", 1);
  t.scaleW = p.getFloat(prefix + "/transform/scalewidth", 1);
  t.scaleH = p.getFloat(prefix + "/transform/scaleheight", 1);
  t.rotation = p.getFloat(prefix + "/transform/rotation");
  t.anchorX = p.getFloat(prefix + "/transform/anchorx");
  t.anchorY = p.getFloat(prefix + "/transform/anchory");
  return t;
}
std::vector<std::string> split(const std::string& path) {
  std::vector<std::string> out;
  std::stringstream ss(path);
  std::string item;
  while (std::getline(ss, item, '/'))
    if (!item.empty()) out.push_back(item);
  return out;
}

}  // namespace

std::string Composition::layerPath(int layer) const { return "/composition/layers/" + std::to_string(layer + 1); }
std::string Composition::clipPath(int layer, int column) const {
  return layerPath(layer) + "/clips/" + std::to_string(column + 1);
}

Composition::Composition(ParameterRegistry& params, EventBus& events, CompositionShape shape)
    : params_(params), events_(events), shape_(shape), effects_(params, events), layers_(shape.layers) {
  layers_.reserve(kMaxLayers);
  clips_.reserve(kMaxLayers);
  clips_.resize(shape.layers);
  for (int l = 0; l < shape.layers; ++l) {
    clips_[l].reserve(kMaxColumns);
    clips_[l] = std::vector<Clip>(shape.columns);
    clips_[l].reserve(kMaxColumns);
    params.instantiate(layerPath(l), schema::layerParameters());
    for (int c = 0; c < shape.columns; ++c) params.instantiate(clipPath(l, c), schema::clipParameters());
  }
  for (int c = 0; c < shape.columns; ++c)
    params.instantiate("/composition/columns/" + std::to_string(c + 1), schema::columnParameters());
  for (int g = 0; g < shape.groups; ++g) {
    const std::string prefix = "/composition/groups/" + std::to_string(g + 1);
    params.instantiate(prefix, schema::groupParameters());
    for (int c = 0; c < shape.columns; ++c)
      params.instantiate(prefix + "/columns/" + std::to_string(c + 1), schema::groupcolumnParameters());
  }
  params.publish("/composition/structure/layers", int64_t{shape.layers});
  params.publish("/composition/structure/columns", int64_t{shape.columns});
  params.publish("/composition/structure/groups", int64_t{shape.groups});

  // Los oyentes corren en el hilo de control: aquí solo se anota, tick() lo aplica.
  listener_ = params.addListener("", [this](const ParamDesc& d, const Value&) {
    if (tPublishing || d.path.rfind("/composition/", 0) != 0) return;
    const bool wanted = d.type == ParamType::Trigger || d.path.ends_with("/path") ||
                        d.path.ends_with("/transport/position");
    if (!wanted) return;
    std::lock_guard lock(queueMutex_);
    queue_.push_back(d.path);
  });
}

// Ganancia de un lado del crossfader. Lineal: los dos lados llenos en el centro.
double Composition::crossfaderGain(const std::string& bus) const {
  if (bus != "A" && bus != "B") return 1;
  const double phase = f("/composition/crossfader/phase"), x = bus == "A" ? 1 - phase : phase;
  const std::string curve = s("/composition/crossfader/curve");
  if (curve == "Cut") return x > 0 ? 1 : 0;
  if (curve == "Smooth") return std::sin(x * 1.5707963267948966);
  return std::min(1.0, 2 * x);
}

void Composition::addLayer() {
  if (shape_.layers >= kMaxLayers) throw std::runtime_error("máximo de capas alcanzado");
  const int l = shape_.layers;
  params_.instantiate(layerPath(l), schema::layerParameters());
  for (int c = 0; c < shape_.columns; ++c) params_.instantiate(clipPath(l, c), schema::clipParameters());
  layers_.emplace_back();
  clips_.emplace_back(static_cast<size_t>(shape_.columns));
  clips_.back().reserve(kMaxColumns);
  shape_.layers = l + 1;
  params_.publish("/composition/structure/layers", int64_t{shape_.layers});
  events_.publish("structure.changed");
}

void Composition::addColumn() {
  if (shape_.columns >= kMaxColumns) throw std::runtime_error("máximo de columnas alcanzado");
  const int c = shape_.columns;
  for (int l = 0; l < shape_.layers; ++l) {
    params_.instantiate(clipPath(l, c), schema::clipParameters());
    clips_[l].emplace_back();
  }
  params_.instantiate("/composition/columns/" + std::to_string(c + 1), schema::columnParameters());
  for (int g = 0; g < shape_.groups; ++g)
    params_.instantiate("/composition/groups/" + std::to_string(g + 1) + "/columns/" + std::to_string(c + 1), schema::groupcolumnParameters());
  shape_.columns = c + 1;
  params_.publish("/composition/structure/columns", int64_t{shape_.columns});
  events_.publish("structure.changed");
}

void Composition::removeLastLayer() {
  if (shape_.layers <= 1) throw std::runtime_error("debe quedar una capa");
  const int l = shape_.layers - 1;
  ejectLayer(l);
  waitForLoads();
  shape_.layers = l;
  params_.publish("/composition/structure/layers", int64_t{shape_.layers});
  effects_.load(layerPath(l), nlohmann::json::array());
  params_.removePrefix(layerPath(l));
  clips_.pop_back();
  layers_.pop_back();
  events_.publish("structure.changed");
}

void Composition::removeLastColumn() {
  if (shape_.columns <= 1) throw std::runtime_error("debe quedar una columna");
  const int c = shape_.columns - 1;
  waitForLoads();
  shape_.columns = c;
  params_.publish("/composition/structure/columns", int64_t{shape_.columns});
  for (int l = 0; l < shape_.layers; ++l) {
    params_.removePrefix(clipPath(l, c));
    clips_[l].pop_back();
  }
  params_.removePrefix("/composition/columns/" + std::to_string(c + 1));
  for (int g = 0; g < shape_.groups; ++g)
    params_.removePrefix("/composition/groups/" + std::to_string(g + 1) + "/columns/" + std::to_string(c + 1));
  events_.publish("structure.changed");
}

Composition::~Composition() {
  params_.removeListener(listener_);
  waitForLoads();
}

void Composition::waitForLoads() {
  for (auto& row : clips_)
    for (auto& c : row)
      if (c.opening.valid()) c.opening.wait();
}

bool Composition::cellHasClip(int layer, int column) const {
  return layer >= 0 && layer < shape_.layers && column >= 0 && column < shape_.columns &&
         clips_[layer][column].source != nullptr;
}

// --- Órdenes -------------------------------------------------------------------

void Composition::process(const std::string& path) {
  const auto p = split(path);
  if (p.size() < 2) return;
  const std::string& last = p.back();
  if (p[1] == "eject") {
    for (int l = 0; l < shape_.layers; ++l) ejectLayer(l);
    pending_.clear();
  } else if (p[1] == "tempo") {
    processTempo(last);
  } else if (p[1] == "columns" && (last == "next" || last == "previous")) {
    const int n = shape_.columns;
    column_ = (column_ + (last == "next" ? 1 : n - 1)) % n;
    triggerColumn(column_, 0);
  } else if (p[1] == "columns" && p.size() == 4) {
    triggerColumn(std::stoi(p[2]) - 1, 0);
  } else if (p[1] == "groups" && p.size() >= 4) {
    const int g = std::stoi(p[2]);
    if (p[3] == "eject") {
      for (int l = 0; l < shape_.layers; ++l)
        if (i(layerPath(l) + "/group") == g) ejectLayer(l);
    } else if (p[3] == "columns" && p.size() == 6) {
      triggerColumn(std::stoi(p[4]) - 1, g);
    }
  } else if (p[1] == "layers" && p.size() >= 4) {
    const int l = std::stoi(p[2]) - 1;
    if (l < 0 || l >= shape_.layers) return;
    if (p[3] == "eject") {
      ejectLayer(l);
    } else if (p[3] == "next" || p[3] == "previous") {
      const int step = p[3] == "next" ? 1 : -1, cur = layers_[l].current.column;
      for (int k = 1; k <= shape_.columns; ++k) {
        const int c = ((cur + step * k) % shape_.columns + shape_.columns) % shape_.columns;
        if (cellHasClip(l, c)) {
          press(l, c);
          break;
        }
      }
    } else if (p[3] == "clips" && p.size() >= 6) {
      const int c = std::stoi(p[4]) - 1;
      if (c < 0 || c >= shape_.columns) return;
      const std::string prefix = clipPath(l, c);
      if (last == "connect") press(l, c);
      else if (last == "release") release(l, c);
      else if (last == "clear") clearClip(l, c);
      else if (last == "path") loadClip(l, c, s(prefix + "/path"));
      else if (last == "position") {
        for (auto& st : layers_)
          for (Slot* slot : {&st.current, &st.previous})
            if (slot->at(l, c)) slot->clock.seek(f(path) * slot->length);
        if (preview_.at(l, c)) preview_.clock.seek(f(path) * preview_.length);
      }
    }
  }
}

void Composition::processTempo(const std::string& what) {
  auto& bpm = params_;
  const double now = f("/composition/tempo/bpm");
  if (what == "resync") {
    beat_ = 0;
  } else if (what == "half") {
    bpm.set("/composition/tempo/bpm", now / 2);
  } else if (what == "double") {
    bpm.set("/composition/tempo/bpm", now * 2);
  } else if (what == "tap") {
    // Una pausa larga empieza una medida nueva; se promedian los últimos 8 toques.
    if (!taps_.empty() && time_ - taps_.back() > 2.0) taps_.clear();
    taps_.push_back(time_);
    if (taps_.size() > 8) taps_.erase(taps_.begin());
    if (taps_.size() >= 2) bpm.set("/composition/tempo/bpm", 60.0 * static_cast<double>(taps_.size() - 1) / (taps_.back() - taps_.front()));
  }
}

void Composition::loadClip(int layer, int column, const std::string& path) {
  Clip& clip = clips_[layer][column];
  const std::string prefix = clipPath(layer, column);
  if (path.empty()) {
    clearClip(layer, column);
    return;
  }
  if (auto it = sources_.find(path); it != sources_.end()) {
    applyLoaded(layer, column, it->second, path);
    return;
  }
  params_.publish(prefix + "/status", "loading");
  // Una apertura en curso termina primero; pumpLoads vuelve a abrir si cambió la ruta.
  if (clip.opening.valid()) return;
  clip.openingPath = path;
  const bool thumbnails = b("/preferences/thumbnails");
  clip.opening = std::async(std::launch::async, [path, thumbnails] {
    std::string error;
    media::SourcePtr src;
    Loaded out;
    const auto dot = path.find_last_of('.');
    std::string ext = dot == std::string::npos ? "" : path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    const bool audioOnly = ext == "wav" || ext == "mp3" || ext == "aif" || ext == "aiff" || ext == "m4a" || ext == "flac" || ext == "aac";
    if (path.rfind("vj://", 0) != 0) {
      out.audio = audio::openAudio(path, error);
      if (out.audio) out.audioSeconds = static_cast<double>(out.audio->frames()) / audio::kSampleRate;
    }
    if (audioOnly) {
      if (!out.audio) throw std::runtime_error(error.empty() ? "archivo de audio no legible" : error);
      src = media::makeGenerator("vj://source/audio");
    } else {
      src = media::openMedia(path, error);
      if (!src) throw std::runtime_error(error);
    }
    out.source = src;
    if (!audioOnly) out.audioSeconds = 0;
    if (thumbnails) {
      const double duration = src->info().duration;
      const double at = duration > 0 ? std::min(1.0, duration / 3) : 0;
      media::FramePtr f;
      for (int i = 0; i < 150; ++i) {
        f = src->frameAt(at);
        if (f && (duration <= 0 || std::abs(f->time - at) < 0.25)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
      }
      if (f) out.thumbnail = media::thumbnailDataUrl(*f);
      src->release();
    }
    return out;
  });
}

void Composition::applyLoaded(int layer, int column, const Loaded& loaded, const std::string& path) {
  Clip& clip = clips_[layer][column];
  const std::string prefix = clipPath(layer, column);
  clip.source = loaded.source;
  clip.info = loaded.source->info();
  clip.audio = loaded.audio;
  if (loaded.audioSeconds > 0) clip.info.duration = loaded.audioSeconds;
  params_.publish(prefix + "/hasaudio", loaded.audio != nullptr);
  params_.publish(prefix + "/status", "ready");
  params_.publish(prefix + "/duration", clip.info.duration);
  params_.publish(prefix + "/mediawidth", int64_t{clip.info.width});
  params_.publish(prefix + "/mediaheight", int64_t{clip.info.height});
  if (!b(prefix + "/thumbnailcustom")) params_.publish(prefix + "/thumbnail", loaded.thumbnail);
  if (clip.info.straightAlpha) params_.set(prefix + "/video/alphatype", std::string("Straight"));
  events_.publish("clip.loaded", {{"layer", layer + 1}, {"column", column + 1}, {"path", path}});
}

void Composition::clearClip(int layer, int column) {
  for (auto& st : layers_) {
    if (st.current.at(layer, column)) stopSlot(st.current);
    if (st.previous.at(layer, column)) stopSlot(st.previous);
  }
  if (preview_.at(layer, column)) previewStop();
  for (int l = 0; l < shape_.layers; ++l) setActiveClip(l);
  Clip& clip = clips_[layer][column];
  clip.source.reset();
  clip.audio.reset();
  clip.info = {};
  params_.publish(clipPath(layer, column) + "/hasaudio", false);
  effects_.clear(clipPath(layer, column));
  const std::string prefix = clipPath(layer, column);
  tPublishing = true;
  params_.set(prefix + "/path", std::string{});
  tPublishing = false;
  params_.publish(prefix + "/status", "empty");
  params_.publish(prefix + "/thumbnail", std::string{});
  params_.publish(prefix + "/thumbnailcustom", false);
  params_.publish(prefix + "/duration", 0.0);
  params_.publish(prefix + "/mediawidth", int64_t{0});
  params_.publish(prefix + "/mediaheight", int64_t{0});
}

void Composition::pumpLoads() {
  for (int l = 0; l < shape_.layers; ++l)
    for (int c = 0; c < shape_.columns; ++c) {
      Clip& clip = clips_[l][c];
      if (!clip.opening.valid() || clip.opening.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        continue;
      const std::string prefix = clipPath(l, c);
      const std::string wanted = s(prefix + "/path");
      const std::string opened = clip.openingPath;
      try {
        Loaded loaded = clip.opening.get();
        sources_[opened] = loaded;
        if (opened == wanted) {
          applyLoaded(l, c, loaded, opened);
          applyLoadDefaults(l, c);
        }
      } catch (const std::exception& e) {
        if (opened == wanted) {
          clip.source.reset();
          params_.publish(prefix + "/status", std::string("error: ") + e.what());
          events_.publish("clip.error", {{"layer", l + 1}, {"column", c + 1}, {"message", e.what()}});
        }
      }
      if (opened != wanted && !wanted.empty()) loadClip(l, c, wanted);
    }
}

void Composition::applyLoadDefaults(int layer, int column) {
  // Solo para clips recién importados: un proyecto abierto trae sus propios ajustes.
  const std::string prefix = clipPath(layer, column);
  if (s(prefix + "/name").empty() == false) return;
  if (!s("/preferences/defaultblend").empty()) params_.set(prefix + "/video/blend", s("/preferences/defaultblend"));
  params_.set(prefix + "/transport/mode", s("/preferences/defaulttransport"));
  params_.set(prefix + "/transport/playmode", s("/preferences/defaultplaymode"));
}

Value Composition::value(const Slot& slot, const std::string& suffix) const {
  if (slot.frozen) {
    auto it = slot.frozen->find(suffix);
    if (it != slot.frozen->end()) return it->second;
    return Value{};
  }
  return params_.get(clipPath(slot.layer, slot.column) + "/" + suffix).value_or(Value{});
}
double Composition::sf(const Slot& slot, const std::string& suffix) const {
  const Value v = value(slot, suffix);
  if (auto* d = std::get_if<double>(&v)) return *d;
  if (auto* n = std::get_if<int64_t>(&v)) return static_cast<double>(*n);
  return 0;
}
int64_t Composition::si(const Slot& slot, const std::string& suffix) const {
  const Value v = value(slot, suffix);
  if (auto* n = std::get_if<int64_t>(&v)) return *n;
  return 0;
}
bool Composition::sb(const Slot& slot, const std::string& suffix) const {
  const Value v = value(slot, suffix);
  if (auto* x = std::get_if<bool>(&v)) return *x;
  return false;
}
std::string Composition::ss(const Slot& slot, const std::string& suffix) const {
  const Value v = value(slot, suffix);
  if (auto* x = std::get_if<std::string>(&v)) return *x;
  return {};
}

double Composition::snapBeats(int layer, int column) const {
  std::string snap = s(clipPath(layer, column) + "/beatsnap");
  if (snap == "Composition") snap = s("/composition/beatsnap");
  return beatsFor(snap);
}

int Composition::resolveTargetLayer(int layer, int column) const {
  std::string target = s(clipPath(layer, column) + "/target");
  if (target == "Composition") target = s("/composition/cliptarget");
  if (target == "Active Layer") return std::clamp<int>(static_cast<int>(i("/composition/activelayer")) - 1, 0, shape_.layers - 1);
  if (target == "Free Layer") {
    for (int l = 0; l < shape_.layers; ++l)
      if (!layers_[l].current.active()) return l;
  }
  return layer;
}

void Composition::press(int layer, int column) {
  if (!cellHasClip(layer, column)) return;
  std::string style = s(clipPath(layer, column) + "/triggerstyle");
  if (style == "Composition") style = s("/composition/triggerstyle");
  if (style == "Toggle") {
    for (auto& st : layers_)
      if (st.current.at(layer, column)) {
        stopSlot(st.current);
        for (int l = 0; l < shape_.layers; ++l) setActiveClip(l);
        return;
      }
  }
  const double snap = snapBeats(layer, column);
  if (snap > 0) {
    pending_.push_back({layer, column, std::floor(beat_ / snap + 1e-9) * snap + snap});
  } else {
    fire(layer, column, 0);
  }
}

void Composition::release(int layer, int column) {
  if (!cellHasClip(layer, column)) return;
  std::string style = s(clipPath(layer, column) + "/triggerstyle");
  if (style == "Composition") style = s("/composition/triggerstyle");
  if (style != "Piano") return;
  std::erase_if(pending_, [&](const Pending& p) { return p.layer == layer && p.column == column; });
  for (auto& st : layers_)
    if (st.current.at(layer, column)) stopSlot(st.current);
  for (int l = 0; l < shape_.layers; ++l) setActiveClip(l);
}

void Composition::fire(int layer, int column, double lateSeconds) {
  if (!cellHasClip(layer, column)) return;
  const int target = resolveTargetLayer(layer, column);
  LayerState& st = layers_[target];
  if (b(layerPath(target) + "/lock")) return;
  const int group = static_cast<int>(i(layerPath(target) + "/group"));
  if (group > 0 && b("/composition/groups/" + std::to_string(group) + "/lock")) return;

  const std::string prefix = clipPath(layer, column);
  const bool ownTransition = s(prefix + "/transition/mode") == "Clip";
  const std::string from = ownTransition ? prefix : layerPath(target);
  const double duration = f(from + "/transition/duration");
  const bool retrigger = st.current.at(layer, column);

  if (!retrigger && duration > 0 && st.current.active()) {
    stopSlot(st.previous);
    st.previous = std::move(st.current);
    st.elapsed = 0;
    st.duration = duration;
    st.transition = transitionIndex(s(from + "/transition/blend"));
  } else if (!retrigger) {
    stopSlot(st.current);
    stopSlot(st.previous);
    st.duration = 0;
  }
  Clip& clip = clips_[layer][column];
  Slot next;
  next.layer = layer;
  next.column = column;
  next.source = clip.source;
  next.audio = clip.audio;
  next.voice = nextVoice_++;
  next.deck = deck_;
  next.length = clip.info.duration > 0 ? clip.info.duration : kStillLength;
  next.clock = ClipClock(static_cast<uint32_t>(layer * 131 + column * 17 + 1));
  next.config = clockConfig(next);
  next.clock.restart(next.config);
  if (lateSeconds > 0) next.clock.advance(lateSeconds, next.config);
  st.current = std::move(next);
  st.faderPaused = false;
  setActiveClip(target);
  params_.publish(prefix + "/connected", true);
  events_.publish("clip.fired", {{"layer", layer + 1}, {"column", column + 1}});
}

void Composition::restSource(const media::SourcePtr& source) {
  if (!source) return;
  for (auto& st : layers_)
    if (st.current.source == source || st.previous.source == source) return;
  if (preview_.source == source) return;
  source->release();
}

void Composition::stopSlot(Slot& slot) {
  if (!slot.active()) {
    slot = Slot{};
    return;
  }
  const int l = slot.layer, c = slot.column;
  const bool frozen = slot.frozen != nullptr;
  media::SourcePtr source = slot.source;
  slot = Slot{};
  restSource(source);
  if (frozen) return;
  bool stillPlaying = false;
  for (auto& st : layers_)
    if (st.current.at(l, c) || st.previous.at(l, c)) stillPlaying = true;
  if (!stillPlaying) params_.publish(clipPath(l, c) + "/connected", false);
}

void Composition::ejectAll() {
  for (int l = 0; l < shape_.layers; ++l) {
    LayerState& st = layers_[l];
    stopSlot(st.current);
    stopSlot(st.previous);
    st.duration = 0;
    setActiveClip(l);
  }
  pending_.clear();
  previewStop();
}

void Composition::freezeSlots(const std::function<bool(int, int)>& keep) {
  for (int l = 0; l < shape_.layers; ++l)
    for (Slot* slot : {&layers_[l].current, &layers_[l].previous}) {
      if (!slot->active() || slot->frozen) continue;
      if (keep(slot->layer, slot->column)) continue;
      auto snapshot = std::make_shared<std::map<std::string, Value>>();
      const std::string prefix = clipPath(slot->layer, slot->column) + "/";
      for (const auto& d : schema::clipParameters())
        if (auto v = params_.get(prefix + d.path)) (*snapshot)[d.path] = *v;
      slot->frozen = std::move(snapshot);
      slot->frozenEffects = effects_.plan(clipPath(slot->layer, slot->column));
      params_.publish(prefix + "connected", false);
    }
}

void Composition::thawSlots(int deck) {
  for (auto& st : layers_)
    for (Slot* slot : {&st.current, &st.previous}) {
      if (!slot->active()) continue;
      if (slot->frozen && slot->deck == deck) {
        slot->frozen.reset();
        params_.publish(clipPath(slot->layer, slot->column) + "/connected", true);
      } else if (!slot->frozen) {
        slot->deck = deck;  // clip persistente: pertenece a todos los decks
      }
    }
}

void Composition::previewStop() {
  media::SourcePtr source = preview_.source;
  preview_ = Slot{};
  previewPath_.clear();
  restSource(source);
  params_.publish("/preview/layer", int64_t{0});
  params_.publish("/preview/column", int64_t{0});
  params_.publish("/preview/file", std::string{});
}

void Composition::previewClip(int layer, int column) {
  if (!cellHasClip(layer, column)) throw std::runtime_error("la celda está vacía");
  previewStop();
  Clip& clip = clips_[layer][column];
  preview_.layer = layer;
  preview_.column = column;
  preview_.source = clip.source;
  preview_.audio = clip.audio;
  preview_.voice = nextVoice_++;
  preview_.deck = deck_;
  preview_.length = clip.info.duration > 0 ? clip.info.duration : kStillLength;
  preview_.config = clockConfig(preview_);
  preview_.clock.restart(preview_.config);
  params_.publish("/preview/layer", int64_t{layer + 1});
  params_.publish("/preview/column", int64_t{column + 1});
}

void Composition::previewHold(double seconds) {
  previewHeld_ = true;
  preview_.clock.seek(seconds);
}

void Composition::previewFile(const std::string& path) {
  previewStop();
  previewPath_ = path;
  params_.publish("/preview/file", path);
  if (auto it = sources_.find(path); it != sources_.end()) {
    std::promise<Loaded> ready;
    ready.set_value(it->second);
    previewOpening_ = ready.get_future();
    return;
  }
  previewOpening_ = std::async(std::launch::async, [path] {
    std::string error;
    auto src = media::openMedia(path, error);
    if (!src) throw std::runtime_error(error);
    Loaded out;
    out.source = src;
    out.audio = audio::openAudio(path, error);
    return out;
  });
}

RenderPlan Composition::previewPlan(int width, int height) const {
  RenderPlan out;
  out.width = width;
  out.height = height;
  out.time = static_cast<float>(time_);
  out.beat = static_cast<float>(beat_);
  PlanLayer layer;
  if (preview_.active()) {
    PlanClip c;
    c.frame = preview_.frame;
    c.key = 1;
    if (preview_.layer >= 0) c = planClip(preview_);
    c.resize = Resize::Fit;
    c.transform = {};
    c.key = 1;
    layer.current = c;
  }
  layer.blend = 1;  // Alpha
  out.layers.push_back(layer);
  return out;
}

media::FramePtr Composition::frameOf(int layer, int column) {
  for (auto& st : layers_)
    if (st.current.at(layer, column)) return st.current.frame;
  if (!cellHasClip(layer, column)) return nullptr;
  return clips_[layer][column].source->frameAt(0);
}

std::string Composition::originalThumbnail(const std::string& path) const {
  auto it = sources_.find(path);
  return it != sources_.end() ? it->second.thumbnail : std::string{};
}

std::string Composition::makeThumbnail(int layer, int column) {
  auto f = frameOf(layer, column);
  return f ? media::thumbnailDataUrl(*f) : std::string{};
}

void Composition::ejectLayer(int layer) {
  if (b(layerPath(layer) + "/lock")) return;
  LayerState& st = layers_[layer];
  stopSlot(st.current);
  stopSlot(st.previous);
  st.duration = 0;
  std::erase_if(pending_, [&](const Pending& p) { return resolveTargetLayer(p.layer, p.column) == layer; });
  setActiveClip(layer);
}

void Composition::setActiveClip(int layer) {
  params_.publish(layerPath(layer) + "/activeclip", int64_t{layers_[layer].current.column + 1});
}

void Composition::triggerColumn(int column, int onlyGroup) {
  if (column < 0 || column >= shape_.columns) return;
  column_ = column;
  params_.publish("/composition/columns/current", int64_t{column + 1});
  events_.publish("column.fired", {{"column", column + 1}});
  for (int l = 0; l < shape_.layers; ++l) {
    const int group = static_cast<int>(i(layerPath(l) + "/group"));
    if (onlyGroup > 0 && group != onlyGroup) continue;
    if (b(layerPath(l) + "/ignorecolumn")) continue;
    if (onlyGroup == 0 && group > 0 && b("/composition/groups/" + std::to_string(group) + "/ignorecolumn")) continue;
    if (cellHasClip(l, column) && !b(clipPath(l, column) + "/ignorecolumn")) press(l, column);
  }
}

// --- Avance por fotograma ------------------------------------------------------

ClockConfig Composition::clockConfig(const Slot& slot) const {
  ClockConfig c;
  c.length = slot.length;
  if (slot.layer < 0) return c;  // archivo en preview: bucle a velocidad 1
  c.in = sf(slot, "transport/in");
  c.out = sf(slot, "transport/out");
  const std::string dir = ss(slot, "transport/direction");
  c.direction = dir == "Backward" ? Direction::Backward : dir == "Pause" ? Direction::Pause : Direction::Forward;
  const std::string mode = ss(slot, "transport/playmode");
  c.mode = mode == "Ping Pong" ? PlayMode::PingPong : mode == "Random" ? PlayMode::Random
         : mode == "Play Once and Eject" ? PlayMode::OnceEject : mode == "Play Once and Hold" ? PlayMode::OnceHold
         : PlayMode::Loop;
  c.randomInterval = sf(slot, "transport/randominterval");
  c.randomDistance = sf(slot, "transport/randomdistance");
  const double span = slot.length * std::abs(c.out - c.in);
  if (ss(slot, "transport/mode") == "BPM Sync") {
    // Depende del reloj maestro: la velocidad de composición no le afecta.
    const std::string m = ss(slot, "transport/beatspeed");
    const double mult = m == "1/8" ? 0.125 : m == "1/4" ? 0.25 : m == "1/2" ? 0.5 : std::stod(m);
    c.rate = span / (static_cast<double>(si(slot, "transport/beats")) * 60.0 / f("/composition/tempo/bpm")) * mult;
  } else {
    const double forced = sf(slot, "transport/duration");
    const double base = forced > 0 ? slot.length / forced : 1.0;
    int group = static_cast<int>(i(layerPath(slot.layer) + "/group"));
    const double groupSpeed = group > 0 ? f("/composition/groups/" + std::to_string(group) + "/speed") : 1.0;
    c.rate = base * sf(slot, "transport/speed") * f("/composition/speed") * groupSpeed;
  }
  return c;
}

void Composition::stepFader(int layer) {
  LayerState& st = layers_[layer];
  const double now = f(layerPath(layer) + "/opacity") * f(layerPath(layer) + "/master");
  const double before = st.lastOpacity;
  st.lastOpacity = now;
  if (!st.current.active()) return;
  const std::string mode = ss(st.current, "faderstart");
  if (mode == "Off") return;
  if (before <= 0 && now > 0) {
    st.faderPaused = false;
    if (mode == "On") st.current.clock.restart(st.current.config);
  } else if (before > 0 && now <= 0 && mode == "Pick-up") {
    st.faderPaused = true;
  }
}

void Composition::runAutopilot(int layer) {
  LayerState& st = layers_[layer];
  if (st.current.frozen) return;  // su deck no está a la vista: no hay celdas a las que saltar
  const std::string prefix = clipPath(st.current.layer, st.current.column);
  std::string action = s(prefix + "/autopilot/action");
  if (action == "Layer Determined") action = s(layerPath(st.current.layer) + "/autopilot");
  if (action == "None") return;
  const int home = st.current.layer, column = st.current.column, n = shape_.columns;
  std::vector<int> filled;
  for (int c = 0; c < n; ++c)
    if (cellHasClip(home, c)) filled.push_back(c);
  if (filled.empty()) return;
  int next = column;
  if (action == "Next" || action == "Previous") {
    const int step = action == "Next" ? 1 : -1;
    for (int k = 1; k <= n; ++k) {
      const int c = ((column + step * k) % n + n) % n;
      if (cellHasClip(home, c)) {
        next = c;
        break;
      }
    }
  } else if (action == "First") {
    next = filled.front();
  } else if (action == "Last") {
    next = filled.back();
  } else if (action == "Random") {
    next = filled[static_cast<size_t>(std::rand()) % filled.size()];
  } else if (action == "Clip") {
    const int c = static_cast<int>(i(prefix + "/autopilot/target")) - 1;
    if (cellHasClip(home, c)) next = c;
  }
  fire(home, next, 0);
}

void Composition::updateLayer(int index, double dt) {
  LayerState& st = layers_[index];
  stepFader(index);

  if (st.previous.active()) {
    st.elapsed += dt;
    if (st.elapsed >= st.duration) stopSlot(st.previous);
    else {
      st.previous.config = clockConfig(st.previous);
      st.previous.clock.advance(dt, st.previous.config);
      st.previous.frame = st.previous.source->frameAt(st.previous.clock.position());
    }
  }

  if (st.current.active()) {
    st.current.config = clockConfig(st.current);
    const ClockStep step = st.faderPaused ? ClockStep{} : st.current.clock.advance(dt, st.current.config);
    if (step.looped) ++st.current.loops;
    const bool autopilotDue = step.looped && !st.current.frozen && st.current.loops >= si(st.current, "autopilot/loops");
    if (autopilotDue) {
      std::string action = ss(st.current, "autopilot/action");
      if (action == "Layer Determined") action = s(layerPath(st.current.layer) + "/autopilot");
      if (action != "None") {
        runAutopilot(index);
        return;
      }
    }
    if (step.finished && st.current.config.mode == PlayMode::OnceEject) {
      stopSlot(st.current);
      setActiveClip(index);
      return;
    }
    st.current.frame = st.current.source->frameAt(st.current.clock.position());
  }
}

void Composition::tick(double dt) {
  std::vector<std::string> work;
  {
    std::lock_guard lock(queueMutex_);
    work.swap(queue_);
  }
  for (const auto& path : work) process(path);
  pumpLoads();

  const double nudge = (b("/composition/tempo/nudgeup") ? 1.0 : 0.0) - (b("/composition/tempo/nudgedown") ? 1.0 : 0.0);
  const double bpm = f("/composition/tempo/bpm") * (1 + nudge * f("/preferences/bpm/nudgerange"));
  if (externalBeat_ >= 0) beat_ = externalBeat_;
  else if (!b("/composition/tempo/pause")) beat_ += dt * bpm / 60.0;
  time_ += dt;
  // El arranque cuantizado compensa lo que el fotograma llegó tarde.
  while (!pending_.empty()) {
    auto due = std::find_if(pending_.begin(), pending_.end(), [&](const Pending& p) { return beat_ + 1e-9 >= p.beat; });
    if (due == pending_.end()) break;
    const Pending p = *due;
    pending_.erase(due);
    fire(p.layer, p.column, (beat_ - p.beat) * 60.0 / bpm);
  }

  for (int l = 0; l < shape_.layers; ++l) updateLayer(l, dt);

  if (previewOpening_.valid() && previewOpening_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
    try {
      Loaded loaded = previewOpening_.get();
      preview_.source = loaded.source;
      preview_.audio = loaded.audio;
      preview_.voice = nextVoice_++;
      const double duration = loaded.source->info().duration;
      preview_.length = duration > 0 ? duration : kStillLength;
      preview_.config = clockConfig(preview_);
      preview_.clock.restart(preview_.config);
    } catch (const std::exception& e) {
      events_.publish("preview.error", {{"message", e.what()}});
      previewStop();
    }
  }
  if (preview_.active()) {
    preview_.config = clockConfig(preview_);
    if (!previewHeld_) preview_.clock.advance(dt, preview_.config);
    preview_.frame = preview_.source->frameAt(preview_.clock.position());
  }

  sincePublish_ += dt;
  if (sincePublish_ >= kPublishInterval) {
    sincePublish_ = 0;
    params_.publish("/composition/tempo/beat", beat_);
    tPublishing = true;
    for (auto& st : layers_)
      if (st.current.active() && !st.current.frozen)
        params_.set(clipPath(st.current.layer, st.current.column) + "/transport/position",
                    st.current.clock.position() / st.current.length);
    tPublishing = false;
  }
}

// --- Audio ---------------------------------------------------------------------

namespace {

float dbToGain(double db) { return db <= -60 ? 0.f : static_cast<float>(std::pow(10.0, db / 20.0)); }

void panned(double gain, double pan, float& l, float& r) {
  l = static_cast<float>(gain * (1 - std::max(0.0, pan)));
  r = static_cast<float>(gain * (1 + std::min(0.0, pan)));
}

}  // namespace

audio::Voice Composition::makeVoice(const Slot& slot, float gainL, float gainR) const {
  audio::Voice v;
  v.id = slot.voice;
  v.data = slot.audio;
  v.position = slot.clock.position();
  const auto& k = slot.config;
  const double dir = k.direction == Direction::Pause ? 0 : k.direction == Direction::Backward ? -1 : 1;
  v.rate = dir * k.rate * slot.clock.bounce();
  v.gainL = gainL;
  v.gainR = gainR;
  return v;
}

std::unique_ptr<audio::AudioPlan> Composition::audioPlan() {
  auto plan = std::make_unique<audio::AudioPlan>();
  const auto busGain = [&](const std::string& bus) { return crossfaderGain(bus); };
  plan->buses.resize(static_cast<size_t>(shape_.layers + shape_.groups));
  for (int l = 0; l < shape_.layers; ++l) {
    const std::string p = layerPath(l);
    audio::Bus& bus = plan->buses[static_cast<size_t>(l)];
    panned(dbToGain(f(p + "/audio/volume")) * f(p + "/master") * busGain(s(p + "/crossfaderbus")), f(p + "/audio/pan"), bus.gainL, bus.gainR);
    const int group = static_cast<int>(i(p + "/group"));
    bus.parent = group > 0 && group <= shape_.groups ? shape_.layers + group - 1 : -1;
    bus.tap = layerTap(l);
    bus.effects = effects_.audioSteps(p);

    const LayerState& st = layers_[l];
    const double mix = st.previous.active() ? std::clamp(st.elapsed / st.duration, 0.0, 1.0) : 1.0;
    const auto add = [&](const Slot& slot, double fade) {
      if (!slot.active() || !slot.audio || sb(slot, "audio/strip")) return;
      float gl, gr;
      panned(dbToGain(sf(slot, "audio/volume")) * fade, sf(slot, "audio/pan"), gl, gr);
      audio::Voice v = makeVoice(slot, gl, gr);
      v.bus = l;
      v.tap = clipTap(l);
      if (st.faderPaused) v.rate = 0;
      if (!slot.frozen) v.effects = effects_.audioSteps(clipPath(slot.layer, slot.column));
      plan->voices.push_back(std::move(v));
    };
    add(st.current, mix);
    add(st.previous, 1 - mix);
  }
  for (int g = 0; g < shape_.groups; ++g) {
    const std::string p = "/composition/groups/" + std::to_string(g + 1);
    audio::Bus& bus = plan->buses[static_cast<size_t>(shape_.layers + g)];
    panned(dbToGain(f(p + "/audio/volume")) * f(p + "/master") * busGain(s(p + "/crossfaderbus")), f(p + "/audio/pan"), bus.gainL, bus.gainR);
    bus.tap = groupTap(g);
    bus.effects = effects_.audioSteps(p);
  }
  panned(dbToGain(f("/composition/audio/volume")) * f("/composition/master"), f("/composition/audio/pan"), plan->masterL, plan->masterR);
  plan->masterEffects = effects_.audioSteps("/composition");
  plan->masterTap = 0;
  if (preview_.active() && preview_.audio) plan->previewVoices.push_back(makeVoice(preview_, 1, 1));
  if (b("/composition/tempo/metronome") && !b("/composition/tempo/pause")) {
    plan->metronomeBeatsPerSecond = f("/composition/tempo/bpm") / 60.0;
    plan->beat = beat_;
    plan->metronomeGain = dbToGain(f("/composition/tempo/metronomevolume"));
  }
  return plan;
}

// --- Plan de render ------------------------------------------------------------

PlanClip Composition::planClip(const Slot& slot) const {
  PlanClip c;
  c.frame = slot.frame;
  c.key = static_cast<uint64_t>(slot.layer) * 4096 + slot.column + 1;
  const std::string r = ss(slot, "resize");
  c.resize = r == "Fill" ? Resize::Fill : r == "Stretch" ? Resize::Stretch : r == "Original" ? Resize::Original : Resize::Fit;
  c.transform.x = sf(slot, "transform/x");
  c.transform.y = sf(slot, "transform/y");
  c.transform.scale = sf(slot, "transform/scale");
  c.transform.scaleW = sf(slot, "transform/scalewidth");
  c.transform.scaleH = sf(slot, "transform/scaleheight");
  c.transform.rotation = sf(slot, "transform/rotation");
  c.transform.anchorX = sf(slot, "transform/anchorx");
  c.transform.anchorY = sf(slot, "transform/anchory");
  c.opacity = static_cast<float>(sf(slot, "video/opacity"));
  c.channels = {sb(slot, "video/red"), sb(slot, "video/green"), sb(slot, "video/blue")};
  c.useAlpha = sb(slot, "video/alpha");
  c.straightAlpha = ss(slot, "video/alphatype") == "Straight";
  c.hidden = sb(slot, "video/strip");
  c.effects = slot.frozen ? slot.frozenEffects : effects_.plan(clipPath(slot.layer, slot.column));
  return c;
}

RenderPlan Composition::plan() const {
  RenderPlan out;
  out.width = static_cast<int>(i("/composition/width"));
  out.height = static_cast<int>(i("/composition/height"));
  out.master = static_cast<float>(f("/composition/master"));
  out.bypass = b("/composition/bypass");
  out.routeBus[0] = routedBus_[0];
  out.routeBus[1] = routedBus_[1];
  out.time = static_cast<float>(time_);
  out.beat = static_cast<float>(beat_);
  out.bpm = static_cast<float>(f("/composition/tempo/bpm"));
  out.effects = effects_.plan("/composition");

  const auto busGain = [&](const std::string& bus) { return crossfaderGain(bus); };

  bool anySolo = false;
  const auto groupPath = [](int g) { return "/composition/groups/" + std::to_string(g); };
  for (int g = 1; g <= shape_.groups; ++g) anySolo = anySolo || b(groupPath(g) + "/solo");
  for (int l = 0; l < shape_.layers; ++l) anySolo = anySolo || b(layerPath(l) + "/solo");

  const auto& blends = schema::blendModes();
  for (int g = 1; g <= shape_.groups; ++g) {
    const std::string p = groupPath(g);
    PlanGroup pg;
    pg.blend = enumIndex(blends, s(p + "/blend"));
    pg.opacity = static_cast<float>(f(p + "/master") * busGain(s(p + "/crossfaderbus")));
    pg.visible = !b(p + "/bypass");
    pg.transform = readTransform(params_, p);
    pg.effects = effects_.plan(p);
    if (auto r = routedGroups_.find(g); r != routedGroups_.end()) {
      pg.routed = true;
      pg.ignoreBypass = r->second;
      if (r->second) pg.visible = true;
    }
    out.groups.push_back(pg);
  }

  for (int l = 0; l < shape_.layers; ++l) {
    const LayerState& st = layers_[l];
    const std::string p = layerPath(l);
    PlanLayer pl;
    pl.group = static_cast<int>(i(p + "/group"));
    if (pl.group > shape_.groups) pl.group = 0;
    const bool groupSolo = pl.group > 0 && b(groupPath(pl.group) + "/solo");
    pl.visible = !b(p + "/bypass") && (!anySolo || b(p + "/solo") || groupSolo);
    pl.opacity = static_cast<float>(f(p + "/master") * f(p + "/opacity") * busGain(s(p + "/crossfaderbus")));
    const std::string bus = s(p + "/crossfaderbus");
    pl.bus = bus == "A" ? 1 : bus == "B" ? 2 : 0;
    pl.busOpacity = static_cast<float>(f(p + "/master") * f(p + "/opacity"));
    pl.blend = enumIndex(blends, s(p + "/blend"));
    const std::string mask = s(p + "/mask");
    pl.mask = mask == "All Below" ? MaskMode::AllBelow : mask == "One Below" ? MaskMode::OneBelow : MaskMode::Disabled;
    pl.transform = readTransform(params_, p);
    pl.effects = effects_.plan(p);
    if (auto r = routedLayers_.find(l + 1); r != routedLayers_.end()) {
      pl.routed = true;
      pl.ignoreBypass = r->second;
      if (r->second) {
        pl.visible = true;
        pl.opacity = 1;
      }
    }
    if (st.current.active()) {
      pl.current = planClip(st.current);
      const std::string clipBlend = ss(st.current, "video/blend");
      if (clipBlend != "Layer Determined") pl.blend = enumIndex(blends, clipBlend);
    }
    if (st.previous.active()) {
      pl.previous = planClip(st.previous);
      pl.mix = static_cast<float>(std::clamp(st.elapsed / st.duration, 0.0, 1.0));
      pl.transition = st.transition;
    }
    out.layers.push_back(std::move(pl));
  }
  return out;
}

}  // namespace vj::compositor
