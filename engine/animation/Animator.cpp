#include "animation/Animator.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "core/EventBus.h"

using nlohmann::json;
using vj::compositor::ClockConfig;
using vj::compositor::Direction;
using vj::compositor::PlayMode;

namespace vj::animation {
namespace {

ClockConfig clockFor(const json& c, double bpm) {
  ClockConfig k;
  k.length = 1;
  const std::string mode = c.value("playmode", "Loop");
  k.mode = mode == "Ping Pong" ? PlayMode::PingPong : mode == "Random" ? PlayMode::Random
         : mode == "Play Once and Hold" ? PlayMode::OnceHold : PlayMode::Loop;
  const std::string dir = c.value("direction", "Forward");
  k.direction = dir == "Backward" ? Direction::Backward : dir == "Pause" ? Direction::Pause : Direction::Forward;
  const double seconds = c.value("mode", "timeline") == "bpm" ? c.value("beats", 4.0) * 60.0 / bpm : c.value("duration", 2.0);
  k.rate = c.value("speed", 1.0) / std::max(seconds, 1e-4);
  k.randomInterval = 0.1;
  return k;
}

}  // namespace

double evalEnvelope(const json& p, double t) {
  if (!p.is_array() || p.size() < 2) return t;
  const auto x = [&](size_t i) { return p[i][0].get<double>(); };
  const auto y = [&](size_t i) { return p[i][1].get<double>(); };
  if (t <= x(0)) return y(0);
  for (size_t i = 1; i < p.size(); ++i)
    if (t <= x(i)) {
      const double span = x(i) - x(i - 1);
      return span <= 0 ? y(i) : y(i - 1) + (y(i) - y(i - 1)) * (t - x(i - 1)) / span;
    }
  return y(p.size() - 1);
}

bool Animator::start(EngineContext& ctx) {
  ctx_ = &ctx;
  handler_ = ctx.events.subscribe("", [this](const std::string& topic, const json& data) {
    if (topic == "clip.fired" || topic == "column.fired") onFired(topic, data);
  });
  return true;
}

void Animator::stop() { ctx_->events.unsubscribe(handler_); }

std::string Animator::ownerOf(const std::string& path) const {
  const auto c = path.find("/clips/");
  if (c == std::string::npos) return {};
  return path.substr(0, path.find('/', c + 7));
}

void Animator::onFired(const std::string& topic, const json& data) {
  for (auto& [path, a] : anims_) {
    const json start = a.config.value("start", json::object());
    const auto kick = [&] {
      a.running = true;
      a.clock.restart(clockFor(a.config, ctx_->params.getFloat("/composition/tempo/bpm", 120)));
    };
    if (topic == "column.fired" && start.value("column", false)) {
      kick();
    } else if (topic == "clip.fired" && start.value("clip", false)) {
      const std::string owner = ownerOf(path);
      const std::string fired = "/composition/layers/" + std::to_string(data.value("layer", 0)) + "/clips/" + std::to_string(data.value("column", 0));
      // Una animación de capa o composición responde a cualquier disparo; la de un clip, al suyo.
      if (owner.empty() || owner == fired) kick();
    }
  }
}

void Animator::set(const std::string& path, json config) {
  if (!ctx_->params.describe(path)) throw std::runtime_error("parámetro desconocido: " + path);
  Anim a;
  if (config.value("mode", "") == "expression") a.expr = std::make_shared<Expression>(config.value("expr", "0"));
  auto it = anims_.find(path);
  const json start = config.value("start", json::object());
  a.running = it != anims_.end() ? it->second.running : start.value("load", start.empty());
  a.config = std::move(config);
  a.clock.restart(clockFor(a.config, ctx_->params.getFloat("/composition/tempo/bpm", 120)));
  anims_[path] = std::move(a);
}

void Animator::remove(const std::string& path) { anims_.erase(path); }

std::optional<json> Animator::get(const std::string& path) const {
  auto it = anims_.find(path);
  if (it == anims_.end()) return std::nullopt;
  return it->second.config;
}

void Animator::startNow(const std::string& path) {
  auto it = anims_.find(path);
  if (it == anims_.end()) throw std::runtime_error("sin animación");
  it->second.running = true;
  it->second.clock.restart(clockFor(it->second.config, ctx_->params.getFloat("/composition/tempo/bpm", 120)));
}

json Animator::save(const std::string& prefix, const std::function<bool(const std::string&)>& filter) const {
  json out = json::object();
  for (const auto& [path, a] : anims_) {
    if (path.rfind(prefix, 0) != 0 || (filter && !filter(path))) continue;
    out[path.substr(prefix.size())] = a.config;
  }
  return out;
}

void Animator::clearPrefix(const std::string& prefix, const std::function<bool(const std::string&)>& filter) {
  for (auto it = anims_.begin(); it != anims_.end();)
    if (it->first.rfind(prefix, 0) == 0 && (!filter || filter(it->first))) it = anims_.erase(it);
    else ++it;
}

void Animator::load(const std::string& prefix, const json& map) {
  for (const auto& [k, v] : map.items()) {
    try {
      set(prefix + k, v);
    } catch (const std::exception&) {
      // Una animación de un efecto que ya no existe no impide abrir el proyecto.
    }
  }
}

void Animator::compositionLoaded() {
  for (auto& [_, a] : anims_) {
    const json start = a.config.value("start", json::object());
    a.running = start.value("load", start.empty());
    a.clock.restart(clockFor(a.config, ctx_->params.getFloat("/composition/tempo/bpm", 120)));
  }
}

double Animator::input(const std::string& path, Anim& a, double dt) {
  auto& p = ctx_->params;
  const json& c = a.config;
  const std::string mode = c.value("mode", "timeline");
  const double bpm = p.getFloat("/composition/tempo/bpm", 120), beat = p.getFloat("/composition/tempo/beat");
  if (mode == "bpm" && c.value("phaseLock", true)) {
    const double beats = std::max(c.value("beats", 4.0), 1e-3);
    const std::string play = c.value("playmode", "Loop");
    const double phase = std::fmod(beat, beats) / beats;
    if (play == "Ping Pong") {
      const double cycle = std::fmod(beat, beats * 2) / beats;
      return cycle < 1 ? cycle : 2 - cycle;
    }
    return c.value("direction", "Forward") == "Backward" ? 1 - phase : phase;
  }
  if (mode == "timeline" || mode == "bpm") {
    const ClockConfig k = clockFor(c, bpm);
    a.clock.advance(dt, k);
    return a.clock.position();
  }
  if (mode == "clip") {
    const std::string owner = c.value("clip", ownerOf(path));
    return p.getFloat(owner + "/transport/position");
  }
  if (mode == "crossfader") return p.getFloat("/composition/crossfader/phase");
  if (mode == "fft") {
    const json f = c.value("fft", json::object());
    const std::string band = f.value("band", "L");
    const double lo = f.value("low", band == "L" ? 20.0 : band == "M" ? 250.0 : 4000.0);
    const double hi = f.value("high", band == "L" ? 250.0 : band == "M" ? 4000.0 : 20000.0);
    double level = levels_ ? levels_->level(f.value("source", "composition"), ownerOf(path), lo, hi) : 0;
    level *= f.value("gain", 1.0) * p.getFloat("/preferences/audio/fftgain", 1);
    level = std::clamp(level, 0.0, 1.0);
    a.held = std::max(level, a.held - f.value("fall", 2.0) * dt);
    const std::string drive = f.value("drive", ">");
    if (drive == "<") return 1 - a.held;
    if (drive == "+" || drive == "-") {
      a.drift += (drive == "+" ? 1 : -1) * a.held * dt;
      return a.drift - std::floor(a.drift);
    }
    return a.held;
  }
  if (mode == "expression" && a.expr) {
    a.clock.advance(dt, clockFor(c, bpm));
    const std::map<std::string, double> vars = {
        {"t", a.clock.position()}, {"time", time_}, {"beat", beat}, {"bpm", bpm},
        {"x", p.getFloat(path)}, {"cross", p.getFloat("/composition/crossfader/phase")},
        {"low", levels_ ? levels_->level("composition", {}, 20, 250) : 0},
        {"mid", levels_ ? levels_->level("composition", {}, 250, 4000) : 0},
        {"high", levels_ ? levels_->level("composition", {}, 4000, 20000) : 0}};
    return a.expr->eval(vars);
  }
  return 0;
}

void Animator::apply(const std::string& path, const Anim& a, double x) {
  auto d = ctx_->params.describe(path);
  if (!d) return;
  auto& p = ctx_->params;
  const json& c = a.config;
  const double env = std::clamp(evalEnvelope(c.value("envelope", json()), std::clamp(x, 0.0, 1.0)), 0.0, 1.0);
  const double in = c.value("in", 0.0), out = c.value("out", 1.0);  // fracciones del rango
  const double v = in + (out - in) * env;
  switch (d->type) {
    case ParamType::Float: p.setQuiet(path, d->min + v * (d->max - d->min)); break;
    case ParamType::Int: p.setQuiet(path, d->min + v * (d->max - d->min)); break;
    case ParamType::Bool: p.setQuiet(path, v >= 0.5); break;
    case ParamType::Enum:
      if (!d->options.empty()) p.setQuiet(path, d->options[std::min(d->options.size() - 1, static_cast<size_t>(v * d->options.size()))]);
      break;
    default: break;
  }
}

void Animator::tick(double dt) {
  time_ += dt;
  std::vector<std::string> events;
  for (auto& [path, a] : anims_) {
    if (!a.running) continue;
    const double x = input(path, a, dt);
    auto d = ctx_->params.describe(path);
    if (!d) continue;
    if (d->type == ParamType::Trigger) {
      // Un evento se dispara cada vez que la curva cruza el centro hacia arriba.
      const bool high = evalEnvelope(a.config.value("envelope", json()), x) >= 0.5;
      if (high && !a.lastHigh) events.push_back(path);
      a.lastHigh = high;
    } else {
      apply(path, a, x);
    }
  }
  for (const auto& path : events) ctx_->params.trigger(path);
}

}  // namespace vj::animation
