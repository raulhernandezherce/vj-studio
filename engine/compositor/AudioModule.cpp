#include "compositor/AudioModule.h"

#include <cmath>
#include <cstdio>
#include <sstream>

#include "compositor/CompositionModule.h"
#include "core/CommandBus.h"
#include "core/ModuleRegistry.h"

using nlohmann::json;

namespace vj::compositor {

bool AudioModule::start(EngineContext& ctx) {
  ctx_ = &ctx;
  auto* module = ctx.modules.get<CompositionModule>("composition");
  if (!module) return false;
  comp_ = module->composition();
  engine_ = std::make_unique<audio::AudioEngine>(compositor::kMaxLayers + comp_->shape().groups);
  ctx.params.publish("/audio/samplerate", int64_t{audio::kSampleRate});
  if (ctx.commands)
    ctx.commands->add("audio.spectrum", [this](const json& a) {
      const std::string source = a.value("source", std::string("composition"));
      const auto s = source == "external" ? engine_->inputSpectrum() : engine_->tapSpectrum(0);
      // Se agrupa en 64 bandas logarítmicas para la pantalla.
      json out = json::array();
      for (int i = 0; i < 64; ++i) {
        const double lo = 20 * std::pow(1000.0, i / 64.0), hi = 20 * std::pow(1000.0, (i + 1) / 64.0);
        out.push_back(audio::bandLevel(s, lo, hi, audio::kSampleRate));
      }
      return out;
    });
  if (!useDevice_) ctx.params.publish("/audio/status", "sin dispositivo");
  return true;
}

void AudioModule::stop() {
  if (openingOutput_.valid()) openingOutput_.wait();
  if (openingInput_.valid()) openingInput_.wait();
  input_.reset();
  output_.reset();
  engine_.reset();
}

void AudioModule::reopen() {
  auto& p = ctx_->params;
  if (openingOutput_.valid() || openingInput_.valid()) return;
  std::stringstream key;
  key << p.getBool("/preferences/audio/enabled") << '|' << p.getString("/preferences/audio/output") << '|' << p.getInt("/preferences/audio/buffersize") << '|'
      << p.getInt("/preferences/audio/masterchannel") << '|' << p.getInt("/preferences/audio/previewchannel") << '|'
      << p.getString("/preferences/audio/input") << '|' << p.getInt("/preferences/audio/inputchannel") << '|' << p.getFloat("/preferences/audio/inputgain");
  if (key.str() == wantedKey_) return;
  wantedKey_ = key.str();

  const bool enabled = p.getBool("/preferences/audio/enabled");
  const std::string out = p.getString("/preferences/audio/output"), in = p.getString("/preferences/audio/input");
  const int buffer = static_cast<int>(p.getInt("/preferences/audio/buffersize"));
  const int master = static_cast<int>(p.getInt("/preferences/audio/masterchannel")) - 1;
  const int preview = static_cast<int>(p.getInt("/preferences/audio/previewchannel")) - 1;
  const int channel = static_cast<int>(p.getInt("/preferences/audio/inputchannel")) - 1;
  const float gain = static_cast<float>(std::pow(10.0, p.getFloat("/preferences/audio/inputgain") / 20.0));
  auto* engine = engine_.get();
  // El dispositivo anterior se cierra dentro de la tarea: pararlo también puede tardar.
  auto oldOutput = std::make_shared<std::unique_ptr<audio::AudioDevice>>(std::move(output_));
  openingOutput_ = std::async(std::launch::async, [=]() -> std::unique_ptr<audio::AudioDevice> {
    oldOutput->reset();
    if (!enabled) return nullptr;
    return audio::openOutput(*engine, out, buffer, master, preview);
  });
  // Solo se toca el micrófono si el usuario eligió una entrada: evita el aviso de permisos.
  auto oldInput = std::make_shared<std::unique_ptr<audio::AudioDevice>>(std::move(input_));
  openingInput_ = std::async(std::launch::async, [=]() -> std::unique_ptr<audio::AudioDevice> {
    oldInput->reset();
    if (!enabled || in.empty()) return nullptr;
    return audio::openInput(*engine, in, channel, gain);
  });
}

int AudioModule::tapFor(const std::string& source, const std::string& owner) const {
  if (source == "composition") return 0;
  int layer = 0;
  if (std::sscanf(owner.c_str(), "/composition/layers/%d", &layer) == 1) {
    if (source == "layer") return comp_->layerTap(layer - 1);
    if (source == "clip") return comp_->clipTap(layer - 1);
    if (source == "group") {
      const int g = static_cast<int>(ctx_->params.getInt(owner.substr(0, owner.find("/clips/")) + "/group"));
      if (g > 0) return comp_->groupTap(g - 1);
    }
  }
  return 0;
}

double AudioModule::level(const std::string& source, const std::string& owner, double lowHz, double highHz) {
  const int tap = source == "external" ? -1 : tapFor(source, owner);
  auto it = spectra_.find(tap);
  if (it == spectra_.end()) it = spectra_.emplace(tap, tap < 0 ? engine_->inputSpectrum() : engine_->tapSpectrum(tap)).first;
  return audio::bandLevel(it->second, lowHz, highHz, audio::kSampleRate);
}

void AudioModule::tick(double dt) {
  spectra_.clear();
  engine_->setPlan(comp_->audioPlan());
  auto& p = ctx_->params;

  sinceMeter_ += dt;
  if (sinceMeter_ >= 0.05) {
    sinceMeter_ = 0;
    p.publish("/audio/level/left", static_cast<double>(engine_->peakLeft()));
    p.publish("/audio/level/right", static_cast<double>(engine_->peakRight()));
  }
  if (!useDevice_) return;

  sinceDevices_ += dt;
  if (sinceDevices_ >= 3) {
    sinceDevices_ = 0;
    json list = json::array();
    for (const auto& d : audio::listDevices())
      list.push_back({{"id", d.id}, {"name", d.name}, {"inputs", d.inputs}, {"outputs", d.outputs}});
    p.publish("/audio/devices", list.dump());
  }

  const auto collect = [&](std::future<std::unique_ptr<audio::AudioDevice>>& f, std::unique_ptr<audio::AudioDevice>& slot, const char* label) {
    if (!f.valid() || f.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    try {
      slot = f.get();
      if (std::string(label) == "salida") p.publish("/audio/status", slot ? "activo" : "apagado");
    } catch (const std::exception& e) {
      slot.reset();
      p.publish("/audio/status", std::string("error de ") + label + ": " + e.what());
    }
  };
  collect(openingOutput_, output_, "salida");
  collect(openingInput_, input_, "entrada");
  reopen();
}

}  // namespace vj::compositor
