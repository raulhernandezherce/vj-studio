#include <gtest/gtest.h>
#include <unistd.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>

#include "animation/Animator.h"
#include "audio/AudioEngine.h"
#include "compositor/AudioModule.h"
#include "compositor/CompositionModule.h"
#include "core/CommandBus.h"
#include "core/ModuleRegistry.h"
#include "core/generated/Parameters.gen.h"

using namespace vj;
using namespace vj::audio;
namespace fs = std::filesystem;

namespace {

constexpr double kPi = 3.14159265358979;

std::string wav(const std::string& name, double seconds, const std::function<double(double)>& fn, double amplitude = 1.0) {
  const auto path = fs::temp_directory_path() / ("vj_audio_" + std::to_string(getpid()) + "_" + name + ".wav");
  const uint32_t frames = static_cast<uint32_t>(seconds * kSampleRate);
  std::vector<int16_t> pcm(frames * 2);
  for (uint32_t i = 0; i < frames; ++i) {
    const auto v = static_cast<int16_t>(std::lround(fn(i / double(kSampleRate)) * amplitude * 32767));
    pcm[i * 2] = v;
    pcm[i * 2 + 1] = v;
  }
  std::ofstream f(path, std::ios::binary);
  const uint32_t bytes = frames * 4, rate = kSampleRate, byteRate = rate * 4, size = 36 + bytes, fmtLen = 16;
  const uint16_t pcmTag = 1, channels = 2, align = 4, bits = 16;
  const auto w = [&](const void* p, size_t n) { f.write(static_cast<const char*>(p), static_cast<std::streamsize>(n)); };
  w("RIFF", 4); w(&size, 4); w("WAVEfmt ", 8); w(&fmtLen, 4); w(&pcmTag, 2); w(&channels, 2); w(&rate, 4); w(&byteRate, 4); w(&align, 2); w(&bits, 2);
  w("data", 4); w(&bytes, 4); w(pcm.data(), bytes);
  return path.string();
}

auto sine(double hz) {
  return [hz](double t) { return std::sin(2 * kPi * hz * t); };
}

double peak(const std::vector<float>& v, int channel, int channels = 2) {
  double p = 0;
  for (size_t i = static_cast<size_t>(channel); i < v.size(); i += static_cast<size_t>(channels)) p = std::max(p, double(std::abs(v[i])));
  return p;
}

// Un seno mantenido, sin archivo: para probar efectos aislados.
struct Tone {
  double phase = 0;
  std::vector<float> l, r;
  void fill(double hz, int n, double amp = 0.8) {
    l.resize(static_cast<size_t>(n));
    r.resize(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
      l[i] = r[i] = static_cast<float>(std::sin(phase) * amp);
      phase += 2 * kPi * hz / kSampleRate;
    }
  }
};

double throughEffect(const char* type, std::array<float, 12> values, double hz) {
  auto fx = makeAudioEffect(type);
  Tone t;
  double p = 0;
  for (int b = 0; b < 40; ++b) {
    t.fill(hz, 512);
    fx->process(t.l.data(), t.r.data(), 512, values);
    if (b > 20) for (float v : t.l) p = std::max(p, double(std::abs(v)));
  }
  return p;
}

}  // namespace

class AudioTest : public ::testing::Test {
 protected:
  void SetUp() override {
    params.defineAll(schema::parameters());
    modules.add(std::make_unique<compositor::CompositionModule>(compositor::CompositionShape{3, 2, 2}));
    modules.add(std::make_unique<compositor::AudioModule>(false));
    ASSERT_EQ(modules.startAll(ctx), "");
    comp = modules.get<compositor::CompositionModule>("composition")->composition();
    audio = modules.get<compositor::AudioModule>("audio");
    params.publish("/composition/width", int64_t{64});
  }
  void TearDown() override { modules.stopAll(); }
  std::string C(int l, int c = 1) { return "/composition/layers/" + std::to_string(l) + "/clips/" + std::to_string(c); }
  std::string L(int l) { return "/composition/layers/" + std::to_string(l); }
  void clip(int l, const std::string& path, int c = 1) {
    params.set(C(l, c) + "/path", path);
    for (int i = 0; i < 5; ++i) {
      modules.tickAll(0);
      comp->waitForLoads();
    }
    usleep(250000);  // decodificación de fondo de un archivo corto
    params.trigger(C(l, c) + "/connect");
    modules.tickAll(0);
  }
  // Avanza y mezcla como lo haría el dispositivo.
  std::vector<float> render(int blocks, int channels = 2, int master = 0, int preview = 2) {
    std::vector<float> all, buf(static_cast<size_t>(480 * channels));
    for (int b = 0; b < blocks; ++b) {
      modules.tickAll(480.0 / kSampleRate);
      audio->engine()->render(buf.data(), 480, channels, master, preview);
      all.insert(all.end(), buf.begin(), buf.end());
    }
    return all;
  }
  ParameterRegistry params;
  EventBus events;
  ModuleRegistry modules;
  EngineContext ctx{params, events, modules, nullptr, nullptr};
  compositor::Composition* comp = nullptr;
  compositor::AudioModule* audio = nullptr;
};

TEST_F(AudioTest, ClipPlaysDecodedFile) {
  clip(1, wav("a", 1, sine(440), 0.5));
  EXPECT_TRUE(params.getBool(C(1) + "/hasaudio"));
  EXPECT_NEAR(params.getFloat(C(1) + "/duration"), 1.0, 0.01);
  EXPECT_NEAR(peak(render(20), 0), 0.5, 0.02);
}

TEST_F(AudioTest, MinusSixDbHalvesAmplitude) {
  clip(1, wav("b", 1, sine(440), 0.8));
  const double full = peak(render(20), 0);
  params.set(C(1) + "/audio/volume", -6.0206);
  const double half = peak(render(20), 0);
  EXPECT_NEAR(half / full, 0.5, 0.01);
}

TEST_F(AudioTest, HardPanSilencesOtherChannel) {
  clip(1, wav("c", 1, sine(440), 0.5));
  params.set(C(1) + "/audio/pan", 1.0);
  auto out = render(20);
  EXPECT_LT(peak(out, 0), 1e-4);
  EXPECT_GT(peak(out, 1), 0.4);
  params.set(C(1) + "/audio/pan", -1.0);
  out = render(20);
  EXPECT_GT(peak(out, 0), 0.4);
  EXPECT_LT(peak(out, 1), 1e-4);
}

TEST_F(AudioTest, TwoFullClipsSumWithoutClipping) {
  const auto f = wav("d", 1, sine(440), 1.0);
  clip(1, f);
  clip(2, f);
  auto out = render(30);
  EXPECT_LE(peak(out, 0), 1.0);
  EXPECT_GT(peak(out, 0), 0.5);
}

TEST_F(AudioTest, GainsStackThroughLayerGroupAndComposition) {
  clip(1, wav("e", 1, sine(440), 0.8));
  const double base = peak(render(20), 0);
  params.set(L(1) + "/group", int64_t{1});
  params.set("/composition/groups/1/audio/volume", -6.0206);
  params.set(L(1) + "/audio/volume", -6.0206);
  EXPECT_NEAR(peak(render(20), 0) / base, 0.25, 0.01);
  params.set("/composition/audio/volume", -6.0206);
  EXPECT_NEAR(peak(render(20), 0) / base, 0.125, 0.01);
  params.set("/composition/master", 0.0);
  EXPECT_LT(peak(render(20), 0), 1e-4);
}

TEST_F(AudioTest, StripAudioAndAudioOnlyClip) {
  clip(1, wav("f", 1, sine(440), 0.5));
  auto plan = comp->plan();
  ASSERT_TRUE(plan.layers[0].current);
  params.set(C(1) + "/audio/strip", true);
  EXPECT_LT(peak(render(20), 0), 1e-4);
  params.set(C(1) + "/audio/strip", false);
  params.set(C(1) + "/video/strip", true);
  EXPECT_TRUE(comp->plan().layers[0].current->hidden);
  EXPECT_GT(peak(render(20), 0), 0.4);
}

TEST_F(AudioTest, AudioFollowsClipPositionAfterSeek) {
  const auto f = wav("g", 1, [](double t) { return t < 0.5 ? 0.0 : std::sin(2 * kPi * 440 * t); }, 0.5);
  clip(1, f);
  EXPECT_LT(peak(render(5), 0), 1e-3);
  params.set(C(1) + "/transport/position", 0.7);
  EXPECT_GT(peak(render(5), 0), 0.4);
}

TEST_F(AudioTest, MissingAudioFileReportsError) {
  params.set(C(1) + "/path", std::string("/no/existe.wav"));
  for (int i = 0; i < 5; ++i) {
    modules.tickAll(0);
    comp->waitForLoads();
  }
  EXPECT_EQ(params.getString(C(1) + "/status").rfind("error", 0), 0u);
}

TEST_F(AudioTest, HundredHertzActivatesLowBandOnly) {
  clip(1, wav("h", 2, sine(100), 0.8));
  render(50);
  modules.tickAll(0);
  const double low = audio->level("composition", "", 20, 250);
  const double high = audio->level("composition", "", 4000, 20000);
  EXPECT_GT(low, 0.5);
  EXPECT_LT(high, 0.01);
}

TEST_F(AudioTest, EightKiloHertzActivatesHighBand) {
  clip(1, wav("i", 2, sine(8000), 0.8));
  render(50);
  modules.tickAll(0);
  EXPECT_LT(audio->level("composition", "", 20, 250), 0.01);
  EXPECT_GT(audio->level("composition", "", 4000, 20000), 0.5);
}

TEST_F(AudioTest, FftSourcesSeparateLayers) {
  clip(1, wav("j", 2, sine(100), 0.8));
  clip(2, wav("k", 2, sine(8000), 0.8));
  render(50);
  modules.tickAll(0);
  EXPECT_GT(audio->level("layer", L(1), 20, 250), 0.5);
  EXPECT_LT(audio->level("layer", L(1), 4000, 20000), 0.01);
  EXPECT_LT(audio->level("layer", L(2), 20, 250), 0.01);
  EXPECT_GT(audio->level("clip", C(2), 4000, 20000), 0.5);
}

TEST_F(AudioTest, ExternalInputMovesAnalyzer) {
  Tone t;
  t.fill(100, 4096);
  audio->engine()->pushInput(t.l.data(), 4096);
  modules.tickAll(0);
  EXPECT_GT(audio->level("external", "", 20, 250), 0.5);
}

TEST(Fft, KnownSineAmplitude) {
  std::vector<float> s(kFftSize);
  for (size_t i = 0; i < kFftSize; ++i) s[i] = static_cast<float>(0.5 * std::sin(2 * kPi * 1000 * i / kSampleRate));
  EXPECT_NEAR(bandLevel(spectrum(s.data()), 900, 1100, kSampleRate), 0.5, 0.05);  // entre dos bins pierde un 7 %
}

TEST(AudioEffects, LowPassAtOneKiloHertzAttenuatesAbove) {
  const std::array<float, 12> v = {1000, 0.707f, 1};
  EXPECT_GT(throughEffect("a-lowpass", v, 200), 0.7);
  EXPECT_LT(throughEffect("a-lowpass", v, 8000), 0.05);
}

TEST(AudioEffects, HighPassAttenuatesBelow) {
  const std::array<float, 12> v = {1000, 0.707f, 1};
  EXPECT_LT(throughEffect("a-highpass", v, 100), 0.05);
  EXPECT_GT(throughEffect("a-highpass", v, 8000), 0.7);
}

TEST(AudioEffects, DryWetZeroIsDry) {
  const std::array<float, 12> v = {1000, 0.707f, 0};
  EXPECT_NEAR(throughEffect("a-lowpass", v, 1000), 0.8, 0.01);
}

TEST(AudioEffects, DelayRepeatsAfterItsTime) {
  auto fx = makeAudioEffect("a-delay");
  std::vector<float> l(4800, 0), r(4800, 0);
  l[0] = r[0] = 1;
  std::array<float, 12> v = {0.05f, 0, 1};
  fx->process(l.data(), r.data(), 4800, v);
  EXPECT_NEAR(l[2400], 1, 1e-5);
  EXPECT_NEAR(l[2399], 0, 1e-5);
}

TEST(AudioEffects, BitcrusherQuantizes) {
  auto fx = makeAudioEffect("a-bitcrusher");
  Tone t;
  t.fill(100, 2048);
  std::array<float, 12> v = {2, 1, 1};
  fx->process(t.l.data(), t.r.data(), 2048, v);
  for (float s : t.l) EXPECT_NEAR(std::fmod(std::abs(s) * 2, 1.f), 0, 1e-5);
}

TEST(AudioEffects, AllTypesStayFinite) {
  for (const char* type : {"a-lowpass", "a-highpass", "a-delay", "a-flanger", "a-bitcrusher", "a-distortion", "a-reverb"}) {
    auto fx = makeAudioEffect(type);
    ASSERT_TRUE(fx) << type;
    Tone t;
    std::array<float, 12> v = {500, 2, 0.8f, 1, 1, 1, 1};
    for (int b = 0; b < 200; ++b) {
      t.fill(330, 512);
      fx->process(t.l.data(), t.r.data(), 512, v);
      for (float s : t.l) ASSERT_TRUE(std::isfinite(s)) << type;
    }
  }
}

TEST_F(AudioTest, EffectInChainFiltersTheClip) {
  clip(1, wav("l", 1, sine(8000), 0.5));
  const double dry = peak(render(20), 0);
  const int e = comp->effects().add(L(1), "a-lowpass");
  params.set(comp->effects().instancePath(L(1), e) + "/cutoff", 500.0);
  render(10);
  EXPECT_LT(peak(render(20), 0), dry * 0.1);
  EXPECT_TRUE(comp->plan().layers[0].effects.empty()) << "un efecto de audio no entra al render de vídeo";
}

TEST(AudioEngineSafety, NonFiniteSamplesDoNotSilenceTheMaster) {
  class Bad final : public AudioEffect {
   public:
    void process(float* l, float* r, int n, const std::array<float, 12>&) override {
      if (armed) l[0] = r[0] = INFINITY;
      armed = false;
      (void)n;
    }
    bool armed = true;
  };
  AudioEngine engine(2);
  std::vector<int16_t> pcm(96000, 8000);
  auto plan = std::make_unique<AudioPlan>();
  plan->buses.resize(1);
  plan->masterEffects.push_back({std::make_shared<Bad>(), {}});
  Voice v;
  v.id = 1;
  v.data = makeAudio(std::move(pcm));
  v.bus = 0;
  plan->voices.push_back(v);
  engine.setPlan(std::move(plan));
  std::vector<float> buf(960);
  engine.render(buf.data(), 480, 2, 0, 2);
  for (float s : buf) ASSERT_TRUE(std::isfinite(s));
  engine.render(buf.data(), 480, 2, 0, 2);
  EXPECT_GT(peak(buf, 0), 0.2) << "el audio sigue sonando";
}

TEST_F(AudioTest, ChannelRoutingAndMetronome) {
  params.set("/composition/tempo/metronome", true);
  params.set("/composition/tempo/metronomevolume", 0.0);
  params.set("/composition/tempo/bpm", 240.0);
  auto four = render(40, 4, 0, 2);
  EXPECT_GT(peak(four, 2, 4), 0.3) << "el metrónomo va por el canal de preview";
  EXPECT_LT(peak(four, 0, 4), 1e-6) << "y no por el master";
  auto two = render(10, 2, 0, 2);  // sin canales de preview: no debe salirse del búfer
  EXPECT_LT(peak(two, 0), 1e-6);
}

TEST_F(AudioTest, PreviewVoiceStaysOffMaster) {
  clip(1, wav("m", 1, sine(440), 0.5), 2);
  params.trigger(L(1) + "/eject");
  modules.tickAll(0);
  comp->previewClip(0, 1);
  auto out = render(20, 4, 0, 2);
  EXPECT_LT(peak(out, 0, 4), 1e-6);
  EXPECT_GT(peak(out, 2, 4), 0.4);
}

TEST_F(AudioTest, PlanSwapUnderConcurrentRenderIsSafe) {
  clip(1, wav("n", 1, sine(440), 0.5));
  std::atomic<bool> stop{false};
  std::thread rt([&] {
    std::vector<float> buf(1024);
    while (!stop) audio->engine()->render(buf.data(), 512, 2, 0, 2);
  });
  for (int i = 0; i < 3000; ++i) modules.tickAll(0.001);
  stop = true;
  rt.join();
  SUCCEED();
}

TEST_F(AudioTest, EightTapsGiveOneTwenty) {
  for (int i = 0; i < 8; ++i) {
    params.trigger("/composition/tempo/tap");
    for (int f = 0; f < 30; ++f) modules.tickAll(1.0 / 60);
  }
  EXPECT_NEAR(params.getFloat("/composition/tempo/bpm"), 120, 0.5);
}

TEST_F(AudioTest, TapAfterLongPauseStartsOver) {
  params.set("/composition/tempo/bpm", 100.0);
  params.trigger("/composition/tempo/tap");
  modules.tickAll(5.0);
  params.trigger("/composition/tempo/tap");
  modules.tickAll(0);
  EXPECT_NEAR(params.getFloat("/composition/tempo/bpm"), 100, 1e-6);
}

TEST_F(AudioTest, HalfDoublePauseNudgeResync) {
  params.trigger("/composition/tempo/half");
  modules.tickAll(0);
  EXPECT_DOUBLE_EQ(params.getFloat("/composition/tempo/bpm"), 60);
  params.trigger("/composition/tempo/double");
  modules.tickAll(0);
  EXPECT_DOUBLE_EQ(params.getFloat("/composition/tempo/bpm"), 120);

  for (int i = 0; i < 60; ++i) modules.tickAll(1.0 / 60);
  const double beat = params.getFloat("/composition/tempo/beat");
  EXPECT_NEAR(beat, 2, 0.25);  // el beat se publica cada 100 ms
  params.set("/composition/tempo/pause", true);
  for (int i = 0; i < 60; ++i) modules.tickAll(1.0 / 60);
  EXPECT_NEAR(params.getFloat("/composition/tempo/beat"), beat, 0.15);
  params.set("/composition/tempo/pause", false);

  params.set("/composition/tempo/nudgeup", true);
  const double before = params.getFloat("/composition/tempo/beat");
  for (int i = 0; i < 600; ++i) modules.tickAll(1.0 / 60);
  EXPECT_NEAR(params.getFloat("/composition/tempo/beat") - before, 20 * 1.1, 0.3);
  params.set("/composition/tempo/nudgeup", false);
  EXPECT_DOUBLE_EQ(params.getFloat("/composition/tempo/bpm"), 120) << "el nudge es temporal";

  params.trigger("/composition/tempo/resync");
  for (int i = 0; i < 7; ++i) modules.tickAll(1.0 / 60);
  EXPECT_LT(params.getFloat("/composition/tempo/beat"), 0.3);
}

TEST(AudioDevices, ListsWithoutOpening) {
  const auto list = listDevices();
  for (const auto& d : list) EXPECT_FALSE(d.id.empty());
}
