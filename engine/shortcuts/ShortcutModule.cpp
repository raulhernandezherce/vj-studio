#include "shortcuts/ShortcutModule.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <algorithm>

#include "compositor/CompositionModule.h"
#include "core/CommandBus.h"
#include "core/EventBus.h"
#include "core/ModuleRegistry.h"

using nlohmann::json;
namespace fs = std::filesystem;

namespace vj::shortcuts {
namespace {

constexpr size_t kMonitorLines = 200;
constexpr double kClockTimeout = 1.0;

std::string midiKey(const std::string& device, const char* kind, int channel, int number) {
  return "midi:" + device + ":" + kind + ":" + std::to_string(channel) + ":" + std::to_string(number);
}

std::string hex(int v) {
  char b[8];
  std::snprintf(b, sizeof b, "%02X", v);
  return b;
}

}  // namespace

bool ShortcutModule::start(EngineContext& ctx) {
  ctx_ = &ctx;
  auto* module = ctx.modules.get<compositor::CompositionModule>("composition");
  if (!module) return false;
  comp_ = module->composition();
  engine_ = std::make_unique<ShortcutEngine>(ctx.params);
  std::error_code ec;
  fs::create_directories(fs::path(documents_) / "Shortcuts", ec);

  std::ifstream active(fs::path(documents_) / "Shortcuts" / "active.txt");
  std::string name;
  std::getline(active, name);
  loadPreset(name.empty() ? "Por defecto" : name);

  midi_ = std::make_unique<CoreMidiPort>();
  registerCommands();

  listener_ = ctx.params.addListener("", [this](const ParamDesc& d, const Value& v) {
    if (d.path.rfind("/composition/", 0) != 0) return;
    std::lock_guard lock(mutex_);
    for (auto& f : engine_->feedback(d.path, v)) {
      if (outgoing_.size() < 512) outgoing_.push_back(std::move(f));
    }
  });
  handler_ = ctx.events.subscribe("osc.unmapped", [this](const std::string&, const json& d) {
    ControlEvent e;
    e.key = "osc:" + d.value("address", "");
    e.value = d.value("value", 1.0);
    std::lock_guard lock(mutex_);
    external_.push_back(std::move(e));
  });
  return true;
}

void ShortcutModule::stop() {
  ctx_->params.removeListener(listener_);
  ctx_->events.unsubscribe(handler_);
  clockOut_ = false;
  if (clockThread_.joinable()) clockThread_.join();
  artnet_.stop();
  midi_.reset();
}

std::string ShortcutModule::presetPath(const std::string& name) const {
  std::string clean = name;
  std::replace_if(clean.begin(), clean.end(), [](char c) { return c == '/' || c == '\\' || c == ':'; }, '-');
  if (clean.empty() || clean.front() == '.') throw std::runtime_error("nombre de preset no válido");
  return (fs::path(documents_) / "Shortcuts" / (clean + ".json")).string();
}

void ShortcutModule::savePreset() {
  // Como en el manual: cada cambio queda guardado en el preset activo.
  const json doc = {{"format", "vjstudio-shortcuts"}, {"version", 1}, {"shortcuts", engine_->list()}};
  std::ofstream(presetPath(ctx_->params.getString("/shortcuts/preset"))) << doc.dump(1);
}

void ShortcutModule::loadPreset(const std::string& name) {
  std::ifstream in(presetPath(name));
  json doc = in ? json::parse(in, nullptr, false) : json::object();
  engine_->load(doc.is_object() ? doc.value("shortcuts", json::array()) : json::array());
  ctx_->params.publish("/shortcuts/preset", name);
  std::ofstream(fs::path(documents_) / "Shortcuts" / "active.txt") << name;
}

void ShortcutModule::registerCommands() {
  auto& bus = *ctx_->commands;
  bus.add("shortcuts.list", [this](const json&) { return engine_->list(); });
  bus.add("shortcuts.add", [this](const json& a) {
    const int id = engine_->add(a);
    savePreset();
    return json{{"id", id}};
  });
  bus.add("shortcuts.remove", [this](const json& a) {
    engine_->remove(a.at("id"));
    savePreset();
    return json::object();
  });
  bus.add("shortcuts.update", [this](const json& a) {
    engine_->update(a.at("id"), a.at("fields"));
    savePreset();
    return json::object();
  });
  bus.add("shortcuts.learn", [this](const json& a) {
    engine_->learn(a.at("target"));
    ctx_->params.publish("/shortcuts/learning", true);
    return json::object();
  });
  bus.add("shortcuts.cancel", [this](const json&) {
    engine_->cancelLearn();
    ctx_->params.publish("/shortcuts/learning", false);
    return json::object();
  });
  bus.add("shortcuts.key", [this](const json& a) {
    ControlEvent e;
    e.key = "key:" + a.at("key").get<std::string>();
    e.button = true;
    e.down = a.value("down", true);
    e.value = 1;
    const bool mapped = [&] {
      for (const auto& s : engine_->list())
        if (s.value("key", "") == e.key) return true;
      return engine_->isLearning();
    }();
    engine_->handle(e);
    savePreset();
    return json{{"handled", mapped}};
  });
  bus.add("shortcuts.preset.list", [this](const json&) {
    json out = json::array();
    std::error_code ec;
    for (const auto& f : fs::directory_iterator(fs::path(documents_) / "Shortcuts", ec))
      if (f.path().extension() == ".json") out.push_back(f.path().stem().string());
    return out;
  });
  bus.add("shortcuts.preset.load", [this](const json& a) {
    savePreset();
    loadPreset(a.at("name"));
    return json::object();
  });
  bus.add("shortcuts.preset.new", [this](const json& a) {
    savePreset();
    loadPreset(a.at("name"));
    savePreset();
    return json::object();
  });
  bus.add("shortcuts.preset.import", [this](const json& a) {
    // Un preset de otro equipo: se valida antes de copiarlo.
    std::ifstream in(a.at("path").get<std::string>());
    json doc = json::parse(in, nullptr, false);
    if (!doc.is_object() || doc.value("format", "") != "vjstudio-shortcuts") throw std::runtime_error("no es un preset de atajos");
    const std::string name = fs::path(a.at("path").get<std::string>()).stem().string();
    std::ofstream(presetPath(name)) << doc.dump(1);
    loadPreset(name);
    return json{{"name", name}};
  });
  bus.add("shortcuts.preset.export", [this](const json& a) {
    savePreset();
    fs::copy_file(presetPath(ctx_->params.getString("/shortcuts/preset")), a.at("path").get<std::string>(), fs::copy_options::overwrite_existing);
    return json::object();
  });
  bus.add("midi.monitor", [this](const json&) {
    std::lock_guard lock(mutex_);
    return json(std::vector<std::string>(monitor_.begin(), monitor_.end()));
  });
  bus.add("dmx.monitor", [this](const json&) {
    json out = json::object();
    for (const auto& [u, channels] : artnet_.snapshot()) {
      json active = json::object();
      for (size_t c = 0; c < channels.size(); ++c)
        if (channels[c]) active[std::to_string(c + 1)] = channels[c];
      out[std::to_string(u)] = std::move(active);
    }
    return out;
  });
}

void ShortcutModule::handleMidi(const MidiMessage& m) {
  auto& p = ctx_->params;
  if (m.status >= 0xF8) {
    if (!p.getBool("/preferences/midi/clockin")) return;
    if (m.status == 0xF8) clock_.tick(m.time);
    else if (m.status == 0xFA) clock_.start(m.time);
    else if (m.status == 0xFB) clock_.resume();
    else if (m.status == 0xFC) clock_.stop();
    clockSeen_ = m.time;
    return;
  }
  if (!p.getBool("/preferences/midi/input")) return;
  const int kind = m.status & 0xF0, channel = (m.status & 0x0F) + 1;
  {
    std::lock_guard lock(mutex_);
    monitor_.push_back(m.device + " " + hex(m.status) + " " + hex(m.data1) + " " + hex(m.data2));
    if (monitor_.size() > kMonitorLines) monitor_.pop_front();
  }
  ControlEvent e;
  if (kind == 0x90 || kind == 0x80) {
    e.key = midiKey(m.device, "note", channel, m.data1);
    e.button = true;
    e.down = kind == 0x90 && m.data2 > 0;
    e.value = m.data2 / 127.0;
  } else if (kind == 0xB0) {
    e.key = midiKey(m.device, "cc", channel, m.data1);
    e.value = m.data2 / 127.0;
    // Encoders en complemento a dos: se reconocen por el modo del atajo.
    for (const auto& s : engine_->list())
      if (s.value("key", "") == e.key && s.value("mode", "") == "relative") e.relative = m.data2 >= 64 ? m.data2 - 128 : m.data2;
    for (const auto& s : engine_->list())
      if (s.value("key", "") == e.key && s.value("cc", "") == "button") {
        e.button = true;
        e.down = m.data2 > 0;
      }
  } else {
    return;
  }
  engine_->handle(e);
  savePreset();
}

void ShortcutModule::pollDmx() {
  auto& p = ctx_->params;
  const std::string want = p.getBool("/preferences/dmx/input") ? std::to_string(p.getInt("/preferences/dmx/port")) : "";
  if (want != dmxApplied_) {
    dmxApplied_ = want;
    artnet_.stop();
    if (want.empty()) p.publish("/preferences/dmx/status", "apagado");
    else p.publish("/preferences/dmx/status", artnet_.start(std::stoi(want)) ? "escuchando en " + want : "puerto ocupado");
  }
  if (!artnet_.active()) return;
  for (const auto& [universe, channels] : artnet_.snapshot()) {
    for (size_t c = 0; c < channels.size(); ++c) {
      const std::string key = "dmx:" + std::to_string(universe) + ":" + std::to_string(c + 1);
      double value = channels[c] / 255.0;
      for (const auto& s : engine_->list())
        if (s.value("key", "") == key && s.value("fine", false) && c + 1 < channels.size()) {
          value = (channels[c] * 256 + channels[c + 1]) / 65535.0;
        }
      auto it = lastDmx_.try_emplace(key, 0.0).first;
      if (it->second == value) continue;
      it->second = value;
      ControlEvent e;
      e.key = key;
      e.value = value;
      engine_->handle(e);
    }
  }
}

void ShortcutModule::updateClockOutput() {
  auto& p = ctx_->params;
  bpm_ = p.getFloat("/composition/tempo/bpm", 120);
  const bool want = p.getBool("/preferences/midi/clockout");
  if (want == clockOut_) return;
  clockOut_ = want;
  if (want) {
    clockThread_ = std::thread([this] {
      midi_->send({}, {0xFA});
      auto next = std::chrono::steady_clock::now();
      while (clockOut_) {
        midi_->send({}, {0xF8});
        next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(60.0 / (bpm_.load() * 24.0)));
        std::this_thread::sleep_until(next);
      }
      midi_->send({}, {0xFC});
    });
  } else if (clockThread_.joinable()) {
    clockThread_.join();
  }
}

void ShortcutModule::tick(double dt) {
  auto& p = ctx_->params;
  sinceDevices_ += dt;
  if (sinceDevices_ >= 2) {
    sinceDevices_ = 0;
    midi_->refresh();
    p.publish("/preferences/midi/devices", json(midi_->inputs()).dump());
  }
  for (const auto& m : midi_->take()) handleMidi(m);

  std::deque<ControlEvent> work;
  std::deque<std::pair<std::string, double>> out;
  {
    std::lock_guard lock(mutex_);
    work.swap(external_);
    out.swap(outgoing_);
  }
  for (const auto& e : work) engine_->handle(e);
  pollDmx();
  updateClockOutput();

  if (p.getBool("/preferences/midi/clockin") && clock_.running()) {
    const double now = hostSeconds();
    if (now - clockSeen_ > kClockTimeout) {
      clock_.stop();
      comp_->setExternalBeat(-1);  // el reloj se calló: el tempo sigue por su cuenta
    } else if (clock_.locked()) {
      p.setQuiet("/composition/tempo/bpm", clock_.bpm());
      comp_->setExternalBeat(clock_.beat(now));
    }
  } else {
    comp_->setExternalBeat(-1);
  }

  if (p.getBool("/preferences/midi/output")) {
    for (const auto& [key, value] : out) {
      // midi:<dispositivo>:<note|cc>:<canal>:<número>
      static const std::regex re(R"(^midi:(.*):(note|cc):(\d+):(\d+)$)");
      std::smatch m;
      if (!std::regex_match(key, m, re)) continue;
      const int channel = std::stoi(m[3].str()) - 1, number = std::stoi(m[4].str());
      const auto status = static_cast<uint8_t>((m[2].str() == "note" ? 0x90 : 0xB0) | channel);
      midi_->send(m[1].str(), {status, static_cast<uint8_t>(number), static_cast<uint8_t>(std::lround(value * 127))});
    }
  }
}

}  // namespace vj::shortcuts
