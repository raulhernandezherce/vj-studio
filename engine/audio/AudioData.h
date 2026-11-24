#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace vj::audio {

inline constexpr int kSampleRate = 48000;

// Audio de un clip, en estéreo y 16 bits a la frecuencia del motor. Se decodifica en segundo
// plano: `ready` crece y el hilo de audio solo lee muestras por debajo de ese valor.
class AudioData {
 public:
  ~AudioData();
  size_t frames() const { return frames_; }
  size_t ready() const { return ready_.load(std::memory_order_acquire); }
  const int16_t* samples() const { return samples_.data(); }
  bool truncated() const { return truncated_; }

 private:
  friend std::shared_ptr<AudioData> openAudio(const std::string&, std::string&);
  friend std::shared_ptr<AudioData> makeAudio(std::vector<int16_t>);
  std::vector<int16_t> samples_;
  size_t frames_ = 0;
  std::atomic<size_t> ready_{0};
  std::atomic<bool> stop_{false};
  bool truncated_ = false;
  std::thread worker_;
};
using AudioPtr = std::shared_ptr<const AudioData>;

// nullptr si el archivo no tiene audio legible. Reserva el buffer y decodifica de fondo.
std::shared_ptr<AudioData> openAudio(const std::string& path, std::string& error);
// Muestras ya listas, entrelazadas en estéreo (pruebas y generadores).
std::shared_ptr<AudioData> makeAudio(std::vector<int16_t> interleaved);

}  // namespace vj::audio
