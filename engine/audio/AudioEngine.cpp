#include "audio/AudioEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vj::audio {
namespace {
constexpr double kResyncSeconds = 0.05;
constexpr float kLimiterRelease = 0.9995f;
}  // namespace

void AudioEngine::Ring::write(const float* l, const float* r, int n) {
  size_t p = pos.load(std::memory_order_relaxed);
  for (int i = 0; i < n; ++i) {
    data[p % kFftSize] = (l[i] + r[i]) * 0.5f;
    ++p;
  }
  pos.store(p, std::memory_order_release);
}

void AudioEngine::Ring::copy(float* out) const {
  const size_t p = pos.load(std::memory_order_acquire);
  for (size_t i = 0; i < kFftSize; ++i) out[i] = data[(p + i) % kFftSize];
}

AudioEngine::AudioEngine(int busCount) : busL_(static_cast<size_t>(busCount)), busR_(static_cast<size_t>(busCount)), rings_(std::make_unique<std::array<Ring, kTaps>>()) {}

AudioEngine::~AudioEngine() {
  delete pending_.load();
  for (auto& r : retired_) delete r.load();
  delete active_;
}

void AudioEngine::setPlan(std::unique_ptr<AudioPlan> plan) {
  // Un plan que el hilo de audio no llegó a recoger se sustituye y se libera aquí.
  delete pending_.exchange(plan.release(), std::memory_order_acq_rel);
  collectGarbage();
}

void AudioEngine::collectGarbage() {
  for (;;) {
    AudioPlan* p = retired_[retireRead_ % kRetired].exchange(nullptr, std::memory_order_acq_rel);
    if (!p) return;
    delete p;
    ++retireRead_;
  }
}

std::vector<float> AudioEngine::tapSpectrum(int tap) const {
  std::vector<float> s(kFftSize);
  if (tap >= 0 && tap < kTaps) (*rings_)[static_cast<size_t>(tap)].copy(s.data());
  return spectrum(s.data());
}

std::vector<float> AudioEngine::inputSpectrum() const {
  std::vector<float> s(kFftSize);
  input_.copy(s.data());
  return spectrum(s.data());
}

void AudioEngine::pushInput(const float* mono, int frames) { input_.write(mono, mono, frames); }

AudioEngine::VoiceState& AudioEngine::stateFor(uint64_t id, double start) {
  VoiceState* free = nullptr;
  for (auto& s : states_) {
    if (s.id == id) {
      s.used = true;
      return s;
    }
    if (!free && s.id == 0) free = &s;
  }
  if (!free) free = &states_[0];
  free->id = id;
  free->read = start * kSampleRate;
  free->used = true;
  return *free;
}

void AudioEngine::mixVoice(const Voice& v, float* l, float* r, int n, double planAge) {
  if (!v.data) return;
  VoiceState& st = stateFor(v.id, v.position);
  // El reloj del clip manda: si el audio se aleja, salta a su posición.
  const double expected = (v.position + planAge * v.rate) * kSampleRate;
  if (std::abs(st.read - expected) > kResyncSeconds * kSampleRate) st.read = expected;

  const size_t ready = v.data->ready();
  const int16_t* s = v.data->samples();
  const double step = v.rate;
  const size_t frames = v.data->frames();
  for (int i = 0; i < n; ++i) {
    double p = st.read;
    if (frames == 0) break;
    const double len = static_cast<double>(frames);
    while (p >= len) p -= len;
    while (p < 0) p += len;
    const size_t a = static_cast<size_t>(p);
    const size_t b = a + 1 < frames ? a + 1 : 0;
    if (a < ready && b < ready) {
      const float f = static_cast<float>(p - static_cast<double>(a));
      const float sl = (s[a * 2] + (s[b * 2] - s[a * 2]) * f) / 32768.f;
      const float sr = (s[a * 2 + 1] + (s[b * 2 + 1] - s[a * 2 + 1]) * f) / 32768.f;
      l[i] += sl * v.gainL;
      r[i] += sr * v.gainR;
    }
    st.read += step;
  }
}

void AudioEngine::metronome(const AudioPlan& p, float* l, float* r, int n) {
  if (p.metronomeBeatsPerSecond <= 0) return;
  double beat = p.beat + static_cast<double>(samplesSincePlan_) / kSampleRate * p.metronomeBeatsPerSecond;
  const double perSample = p.metronomeBeatsPerSecond / kSampleRate;
  for (int i = 0; i < n; ++i) {
    const int64_t b = static_cast<int64_t>(std::floor(beat));
    if (b != lastBeat_) {
      lastBeat_ = b;
      click_ = 1;
      clickPhase_ = 0;
    }
    if (click_ > 0.0005f) {
      const double hz = b % 4 == 0 ? 1500 : 1000;
      clickPhase_ += hz / kSampleRate;
      const float s = static_cast<float>(std::sin(6.283185307 * clickPhase_)) * click_ * p.metronomeGain;
      l[i] += s;
      r[i] += s;
      click_ *= 0.9985f;
    }
    beat += perSample;
  }
}

void AudioEngine::render(float* out, int frames, int channels, int masterChannel, int previewChannel) {
  frames = std::min(frames, kMaxFrames);
  std::memset(out, 0, sizeof(float) * static_cast<size_t>(frames) * channels);

  if (AudioPlan* next = pending_.exchange(nullptr, std::memory_order_acq_rel)) {
    AudioPlan* old = active_;
    AudioPlan* expected = nullptr;
    // Con la cola llena se conserva el plan actual; el nuevo se retira. Nunca se borra aquí.
    if (!old || retired_[retireWrite_ % kRetired].compare_exchange_strong(expected, old, std::memory_order_acq_rel)) {
      if (old) ++retireWrite_;
      active_ = next;
      samplesSincePlan_ = 0;
    } else {
      pending_.store(next, std::memory_order_release);
    }
  }
  if (!active_) return;
  const AudioPlan& p = *active_;
  const double age = static_cast<double>(samplesSincePlan_) / kSampleRate;

  for (auto& s : states_) s.used = false;
  const size_t buses = std::min(p.buses.size(), busL_.size());
  for (size_t b = 0; b < buses; ++b) {
    std::fill_n(busL_[b].begin(), frames, 0.f);
    std::fill_n(busR_[b].begin(), frames, 0.f);
  }
  std::fill_n(masterL_.begin(), frames, 0.f);
  std::fill_n(masterR_.begin(), frames, 0.f);
  std::fill_n(previewL_.begin(), frames, 0.f);
  std::fill_n(previewR_.begin(), frames, 0.f);

  for (const Voice& v : p.voices) {
    std::fill_n(tmpL_.begin(), frames, 0.f);
    std::fill_n(tmpR_.begin(), frames, 0.f);
    mixVoice(v, tmpL_.data(), tmpR_.data(), frames, age);
    for (const auto& e : v.effects) e.effect->process(tmpL_.data(), tmpR_.data(), frames, e.values);
    if (v.tap >= 0 && v.tap < kTaps) (*rings_)[static_cast<size_t>(v.tap)].write(tmpL_.data(), tmpR_.data(), frames);
    const bool toBus = v.bus >= 0 && static_cast<size_t>(v.bus) < buses;
    float* dl = toBus ? busL_[static_cast<size_t>(v.bus)].data() : masterL_.data();
    float* dr = toBus ? busR_[static_cast<size_t>(v.bus)].data() : masterR_.data();
    for (int i = 0; i < frames; ++i) {
      dl[i] += tmpL_[i];
      dr[i] += tmpR_[i];
    }
  }

  for (size_t b = 0; b < buses; ++b) {
    const Bus& bus = p.buses[b];
    float* l = busL_[b].data();
    float* r = busR_[b].data();
    for (const auto& e : bus.effects) e.effect->process(l, r, frames, e.values);
    for (int i = 0; i < frames; ++i) {
      l[i] *= bus.gainL;
      r[i] *= bus.gainR;
    }
    if (bus.tap >= 0 && bus.tap < kTaps) (*rings_)[static_cast<size_t>(bus.tap)].write(l, r, frames);
    const bool toParent = bus.parent >= 0 && static_cast<size_t>(bus.parent) < buses;
    float* dl = toParent ? busL_[static_cast<size_t>(bus.parent)].data() : masterL_.data();
    float* dr = toParent ? busR_[static_cast<size_t>(bus.parent)].data() : masterR_.data();
    for (int i = 0; i < frames; ++i) {
      dl[i] += l[i];
      dr[i] += r[i];
    }
  }

  for (const auto& e : p.masterEffects) e.effect->process(masterL_.data(), masterR_.data(), frames, e.values);
  float peakL = 0, peakR = 0;
  for (int i = 0; i < frames; ++i) {
    float l = masterL_[i] * p.masterL, r = masterR_[i] * p.masterR;
    // Un valor no finito dejaría el limitador a cero para siempre: se descarta.
    if (!std::isfinite(l)) l = 0;
    if (!std::isfinite(r)) r = 0;
    // Limitador: dos clips a ganancia unidad suman sin recortar.
    const float peak = std::max(std::abs(l), std::abs(r));
    // Primero se libera y después se comprueba: al revés, la liberación dejaría pasar el pico.
    limiter_ = std::min(1.f, 1 - (1 - limiter_) * kLimiterRelease);
    if (peak * limiter_ > 1) limiter_ = 1 / peak;
    l *= limiter_;
    r *= limiter_;
    masterL_[i] = l;
    masterR_[i] = r;
    peakL = std::max(peakL, std::abs(l));
    peakR = std::max(peakR, std::abs(r));
  }
  if (p.masterTap >= 0 && p.masterTap < kTaps) (*rings_)[static_cast<size_t>(p.masterTap)].write(masterL_.data(), masterR_.data(), frames);
  peakL_.store(std::max(peakL_.load(std::memory_order_relaxed), peakL), std::memory_order_relaxed);
  peakR_.store(std::max(peakR_.load(std::memory_order_relaxed), peakR), std::memory_order_relaxed);

  for (const Voice& v : p.previewVoices) mixVoice(v, previewL_.data(), previewR_.data(), frames, age);
  metronome(p, previewL_.data(), previewR_.data(), frames);

  for (auto& s : states_)
    if (!s.used) s.id = 0;

  const auto put = [&](int first, const float* l, const float* r) {
    if (first + 1 >= channels) return false;
    for (int i = 0; i < frames; ++i) {
      out[i * channels + first] += l[i];
      out[i * channels + first + 1] += r[i];
    }
    return true;
  };
  put(masterChannel, masterL_.data(), masterR_.data());
  put(previewChannel, previewL_.data(), previewR_.data());
  samplesSincePlan_ += static_cast<uint64_t>(frames);
}

}  // namespace vj::audio
