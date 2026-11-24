#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "audio/AudioPlan.h"
#include "audio/Fft.h"

namespace vj::audio {

inline constexpr int kMaxFrames = 8192;
inline constexpr int kTaps = 96;

// Mezclador en tiempo real. render() no reserva memoria ni toma bloqueos: recibe el plan por
// un intercambio atómico, y el plan retirado se libera en el hilo principal.
class AudioEngine {
 public:
  // busCount: capas + grupos. Reserva aquí todos los búferes.
  explicit AudioEngine(int busCount = 32);
  ~AudioEngine();

  // Hilo principal.
  void setPlan(std::unique_ptr<AudioPlan> plan);
  void collectGarbage();
  std::vector<float> tapSpectrum(int tap) const;
  std::vector<float> inputSpectrum() const;
  float peakLeft() { return peakL_.exchange(0); }
  float peakRight() { return peakR_.exchange(0); }

  // Hilo de audio. `out` entrelazado con `channels` canales. Los canales son base 0.
  void render(float* out, int frames, int channels, int masterChannel, int previewChannel);
  void pushInput(const float* mono, int frames);

 private:
  struct VoiceState {
    uint64_t id = 0;
    double read = 0;  // en muestras
    bool used = false;
  };
  struct Ring {
    std::array<float, kFftSize> data{};
    std::atomic<size_t> pos{0};
    void write(const float* l, const float* r, int n);
    void copy(float* out) const;
  };
  void mixVoice(const Voice& v, float* l, float* r, int n, double planAge);
  VoiceState& stateFor(uint64_t id, double start);
  void metronome(const AudioPlan& p, float* l, float* r, int n);

  std::atomic<AudioPlan*> pending_{nullptr};
  AudioPlan* active_ = nullptr;
  static constexpr size_t kRetired = 64;
  std::array<std::atomic<AudioPlan*>, kRetired> retired_{};  // cola de un productor y un consumidor
  size_t retireWrite_ = 0, retireRead_ = 0;

  std::array<VoiceState, 128> states_{};
  std::vector<std::array<float, kMaxFrames>> busL_, busR_;
  std::array<float, kMaxFrames> tmpL_{}, tmpR_{}, masterL_{}, masterR_{}, previewL_{}, previewR_{};
  uint64_t samplesSincePlan_ = 0;
  float limiter_ = 1;
  float click_ = 0;
  double clickPhase_ = 0;
  int64_t lastBeat_ = -1;
  std::atomic<float> peakL_{0}, peakR_{0};
  std::unique_ptr<std::array<Ring, kTaps>> rings_;
  Ring input_;
};

// Dispositivo real. El hilo de audio lo crea el sistema.
struct DeviceInfo {
  std::string id, name;
  int inputs = 0, outputs = 0;
};
std::vector<DeviceInfo> listDevices();

class AudioDevice {
 public:
  virtual ~AudioDevice() = default;
};
// Lanza std::runtime_error si no se puede abrir. outputId vacío: la salida por defecto.
std::unique_ptr<AudioDevice> openOutput(AudioEngine& engine, const std::string& outputId, int bufferFrames, int masterChannel, int previewChannel);
std::unique_ptr<AudioDevice> openInput(AudioEngine& engine, const std::string& inputId, int channel, float gain);

}  // namespace vj::audio
