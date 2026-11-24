#include "audio/AudioEffects.h"

#include <algorithm>
#include <cmath>

#include "audio/AudioData.h"

namespace vj::audio {
namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kRate = kSampleRate;
constexpr int kMaxBlock = 8192;

struct Biquad {
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
  float z1 = 0, z2 = 0;
  float run(float x) {
    const float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
  }
};

// Mezcla seco y mojado: los efectos procesan una copia.
class WetDry : public AudioEffect {
 public:
  void process(float* l, float* r, int n, const std::array<float, 12>& v) final {
    n = std::min(n, kMaxBlock);
    std::copy(l, l + n, dryL_.begin());
    std::copy(r, r + n, dryR_.begin());
    wet(l, r, n, v);
    const float w = v[wetIndex()];
    for (int i = 0; i < n; ++i) {
      l[i] = dryL_[i] + (l[i] - dryL_[i]) * w;
      r[i] = dryR_[i] + (r[i] - dryR_[i]) * w;
    }
  }

 protected:
  virtual int wetIndex() const = 0;
  virtual void wet(float* l, float* r, int n, const std::array<float, 12>& v) = 0;

 private:
  std::array<float, kMaxBlock> dryL_{}, dryR_{};
};

class Filter final : public WetDry {
 public:
  explicit Filter(bool high) : high_(high) {}

 protected:
  int wetIndex() const override { return 2; }
  void wet(float* l, float* r, int n, const std::array<float, 12>& v) override {
    const float w0 = 2 * kPi * std::min(v[0], kRate * 0.45f) / kRate;
    const float alpha = std::sin(w0) / (2 * std::max(v[1], 0.1f));
    const float c = std::cos(w0), a0 = 1 + alpha;
    const float b0 = (high_ ? (1 + c) : (1 - c)) / 2 / a0;
    const float b1 = (high_ ? -(1 + c) : (1 - c)) / a0;
    for (Biquad* f : {&fl_, &fr_}) {
      f->b0 = b0;
      f->b1 = b1;
      f->b2 = b0;
      f->a1 = -2 * c / a0;
      f->a2 = (1 - alpha) / a0;
    }
    for (int i = 0; i < n; ++i) {
      l[i] = fl_.run(l[i]);
      r[i] = fr_.run(r[i]);
    }
  }

 private:
  bool high_;
  Biquad fl_, fr_;
};

class Delay final : public WetDry {
 public:
  Delay() : l_(static_cast<size_t>(kRate * 2.1f)), r_(l_.size()) {}

 protected:
  int wetIndex() const override { return 2; }
  void wet(float* l, float* r, int n, const std::array<float, 12>& v) override {
    const size_t size = l_.size(), d = std::clamp<size_t>(static_cast<size_t>(v[0] * kRate), 1, size - 1);
    for (int i = 0; i < n; ++i) {
      const size_t rd = (pos_ + size - d) % size;
      const float a = l_[rd], b = r_[rd];
      const float fb = std::clamp(v[1], 0.f, 0.95f);
      l_[pos_] = l[i] + a * fb;
      r_[pos_] = r[i] + b * fb;
      l[i] = a;
      r[i] = b;
      pos_ = (pos_ + 1) % size;
    }
  }

 private:
  std::vector<float> l_, r_;
  size_t pos_ = 0;
};

class Flanger final : public WetDry {
 public:
  Flanger() : l_(4096), r_(4096) {}

 protected:
  int wetIndex() const override { return 3; }
  void wet(float* l, float* r, int n, const std::array<float, 12>& v) override {
    const size_t size = l_.size();
    for (int i = 0; i < n; ++i) {
      phase_ += v[0] / kRate;
      if (phase_ >= 1) phase_ -= 1;
      const float d = 48 + 240 * v[1] * (0.5f + 0.5f * std::sin(2 * kPi * phase_));
      const size_t rd = (pos_ + size - static_cast<size_t>(d)) % size;
      const float a = l_[rd], b = r_[rd];
      const float fb = std::clamp(v[2], 0.f, 0.9f);
      l_[pos_] = l[i] + a * fb;
      r_[pos_] = r[i] + b * fb;
      l[i] = (l[i] + a) * 0.5f;
      r[i] = (r[i] + b) * 0.5f;
      pos_ = (pos_ + 1) % size;
    }
  }

 private:
  std::vector<float> l_, r_;
  size_t pos_ = 0;
  float phase_ = 0;
};

class Bitcrusher final : public WetDry {
 protected:
  int wetIndex() const override { return 2; }
  void wet(float* l, float* r, int n, const std::array<float, 12>& v) override {
    const float steps = std::pow(2.f, std::floor(std::clamp(v[0], 1.f, 16.f)) - 1);
    const int hold = std::max(1, static_cast<int>(v[1]));
    for (int i = 0; i < n; ++i) {
      if (count_++ % hold == 0) {
        hl_ = std::round(l[i] * steps) / steps;
        hr_ = std::round(r[i] * steps) / steps;
      }
      l[i] = hl_;
      r[i] = hr_;
    }
  }

 private:
  int count_ = 0;
  float hl_ = 0, hr_ = 0;
};

class Distortion final : public WetDry {
 protected:
  int wetIndex() const override { return 1; }
  void wet(float* l, float* r, int n, const std::array<float, 12>& v) override {
    const float k = 1 / std::tanh(v[0]);
    for (int i = 0; i < n; ++i) {
      l[i] = std::tanh(l[i] * v[0]) * k;
      r[i] = std::tanh(r[i] * v[0]) * k;
    }
  }
};

// Reverb de Schroeder: filtros peine en paralelo y pasa-todos en serie.
class Reverb final : public WetDry {
 public:
  Reverb() {
    for (size_t i = 0; i < 4; ++i) comb_[i].assign(kCombs[i], 0);
    for (size_t i = 0; i < 2; ++i) all_[i].assign(kAlls[i], 0);
  }

 protected:
  int wetIndex() const override { return 2; }
  void wet(float* l, float* r, int n, const std::array<float, 12>& v) override {
    const float fb = 0.7f + 0.28f * std::clamp(v[0], 0.f, 1.f), damp = std::clamp(v[1], 0.f, 1.f) * 0.8f;
    for (int i = 0; i < n; ++i) {
      const float in = (l[i] + r[i]) * 0.25f;
      float out = 0;
      for (size_t c = 0; c < 4; ++c) {
        float& s = comb_[c][combPos_[c]];
        lp_[c] = s * (1 - damp) + lp_[c] * damp;
        out += s;
        s = in + lp_[c] * fb;
        combPos_[c] = (combPos_[c] + 1) % comb_[c].size();
      }
      for (size_t a = 0; a < 2; ++a) {
        float& s = all_[a][allPos_[a]];
        const float y = s - out * 0.5f;
        s = out + s * 0.5f;
        out = y;
        allPos_[a] = (allPos_[a] + 1) % all_[a].size();
      }
      l[i] = out;
      r[i] = out;
    }
  }

 private:
  static constexpr std::array<size_t, 4> kCombs = {1557, 1617, 1491, 1422};
  static constexpr std::array<size_t, 2> kAlls = {225, 556};
  std::array<std::vector<float>, 4> comb_;
  std::array<std::vector<float>, 2> all_;
  std::array<size_t, 4> combPos_{};
  std::array<size_t, 2> allPos_{};
  std::array<float, 4> lp_{};
};

}  // namespace

std::shared_ptr<AudioEffect> makeAudioEffect(const std::string& t) {
  if (t == "a-lowpass") return std::make_shared<Filter>(false);
  if (t == "a-highpass") return std::make_shared<Filter>(true);
  if (t == "a-delay") return std::make_shared<Delay>();
  if (t == "a-flanger") return std::make_shared<Flanger>();
  if (t == "a-bitcrusher") return std::make_shared<Bitcrusher>();
  if (t == "a-distortion") return std::make_shared<Distortion>();
  if (t == "a-reverb") return std::make_shared<Reverb>();
  return nullptr;
}

}  // namespace vj::audio
