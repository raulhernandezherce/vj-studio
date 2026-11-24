// Salida y entrada con WASAPI y decodificación con Media Foundation. Equivale a CoreAudioDevice y AudioData.
#include <audioclient.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mmdeviceapi.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <thread>

#include "audio/AudioData.h"
#include "audio/AudioEngine.h"
#include "platform-win/WinCommon.h"

namespace vj::audio {
namespace {

using win::Com;
using win::ComScope;

constexpr double kMaxSeconds = 30 * 60;

bool findDevice(const std::string& id, EDataFlow flow, Com<IMMDevice>& device) {
  Com<IMMDeviceEnumerator> list;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(list.put())))) return false;
  if (id.empty()) return SUCCEEDED(list->GetDefaultAudioEndpoint(flow, eConsole, device.put()));
  return SUCCEEDED(list->GetDevice(win::wide(id).c_str(), device.put()));
}

// Formato compartido en coma flotante: el mezclador de Windows convierte a 48 kHz con AUTOCONVERTPCM.
WAVEFORMATEX floatFormat(int channels) {
  WAVEFORMATEX f{};
  f.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
  f.nChannels = static_cast<WORD>(channels);
  f.nSamplesPerSec = kSampleRate;
  f.wBitsPerSample = 32;
  f.nBlockAlign = static_cast<WORD>(4 * channels);
  f.nAvgBytesPerSec = f.nSamplesPerSec * f.nBlockAlign;
  return f;
}

int mixChannels(IAudioClient* client) {
  WAVEFORMATEX* mix = nullptr;
  if (FAILED(client->GetMixFormat(&mix))) return 2;
  const int n = mix->nChannels;
  CoTaskMemFree(mix);
  return std::max(1, n);
}

class Output final : public AudioDevice {
 public:
  Output(AudioEngine& engine, const std::string& id, int bufferFrames, int master, int preview) : engine_(engine), master_(master), preview_(preview) {
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    std::atomic<int> result{0};
    std::string failure;
    worker_ = std::thread([&, id, bufferFrames] {
      ComScope com;
      Com<IMMDevice> device;
      Com<IAudioClient> client;
      Com<IAudioRenderClient> render;
      if (!findDevice(id, eRender, device) || FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.put())))) {
        failure = "dispositivo de salida no encontrado";
        result = -1;
        return;
      }
      const int channels = std::max(2, mixChannels(client.p));
      const WAVEFORMATEX format = floatFormat(channels);
      const REFERENCE_TIME wanted = static_cast<REFERENCE_TIME>(bufferFrames * 1e7 / kSampleRate);
      const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
      UINT32 capacity = 0;
      if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, wanted, 0, &format, nullptr)) || FAILED(client->SetEventHandle(event_)) ||
          FAILED(client->GetBufferSize(&capacity)) || FAILED(client->GetService(IID_PPV_ARGS(render.put()))) || FAILED(client->Start())) {
        failure = "no se pudo iniciar la salida de audio";
        result = -1;
        return;
      }
      result = 1;
      SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
      while (!stop_) {
        if (WaitForSingleObject(event_, 200) != WAIT_OBJECT_0) continue;
        UINT32 padding = 0;
        if (FAILED(client->GetCurrentPadding(&padding))) break;
        const UINT32 frames = capacity - padding;
        BYTE* data = nullptr;
        if (frames == 0 || FAILED(render->GetBuffer(frames, &data))) continue;
        engine_.render(reinterpret_cast<float*>(data), static_cast<int>(frames), channels, master_, preview_);
        render->ReleaseBuffer(frames, 0);
      }
      client->Stop();
    });
    while (result == 0) std::this_thread::yield();
    if (result < 0) {
      worker_.join();
      CloseHandle(event_);
      throw std::runtime_error(failure);
    }
  }
  ~Output() override {
    stop_ = true;
    SetEvent(event_);
    worker_.join();
    CloseHandle(event_);
  }

 private:
  AudioEngine& engine_;
  int master_, preview_;
  HANDLE event_ = nullptr;
  std::atomic<bool> stop_{false};
  std::thread worker_;
};

class Input final : public AudioDevice {
 public:
  Input(AudioEngine& engine, const std::string& id, int channel, float gain) : engine_(engine), channel_(channel), gain_(gain) {
    std::atomic<int> result{0};
    worker_ = std::thread([&, id] {
      ComScope com;
      Com<IMMDevice> device;
      Com<IAudioClient> client;
      Com<IAudioCaptureClient> capture;
      if (!findDevice(id, eCapture, device) || FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.put())))) {
        result = -1;
        return;
      }
      const int channels = mixChannels(client.p);
      const WAVEFORMATEX format = floatFormat(channels);
      const DWORD flags = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
      if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 200000, 0, &format, nullptr)) || FAILED(client->GetService(IID_PPV_ARGS(capture.put()))) || FAILED(client->Start())) {
        result = -1;
        return;
      }
      result = 1;
      std::vector<float> mono(4096);
      const int ch = std::min(channel_, channels - 1);
      while (!stop_) {
        Sleep(5);
        UINT32 packet = 0;
        while (SUCCEEDED(capture->GetNextPacketSize(&packet)) && packet > 0) {
          BYTE* data = nullptr;
          UINT32 frames = 0;
          DWORD status = 0;
          if (FAILED(capture->GetBuffer(&data, &frames, &status, nullptr, nullptr))) break;
          const auto* in = reinterpret_cast<const float*>(data);
          const UINT32 count = std::min<UINT32>(frames, 4096);
          for (UINT32 i = 0; i < count; ++i) mono[i] = (status & AUDCLNT_BUFFERFLAGS_SILENT) ? 0.f : in[i * channels + ch] * gain_;
          engine_.pushInput(mono.data(), static_cast<int>(count));
          capture->ReleaseBuffer(frames);
        }
      }
      client->Stop();
    });
    while (result == 0) std::this_thread::yield();
    if (result < 0) {
      worker_.join();
      throw std::runtime_error("dispositivo de entrada no encontrado");
    }
  }
  ~Input() override {
    stop_ = true;
    worker_.join();
  }

 private:
  AudioEngine& engine_;
  int channel_;
  float gain_;
  std::atomic<bool> stop_{false};
  std::thread worker_;
};

}  // namespace

std::vector<DeviceInfo> listDevices() {
  ComScope com;
  std::vector<DeviceInfo> out;
  Com<IMMDeviceEnumerator> list;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(list.put())))) return out;
  for (const EDataFlow flow : {eRender, eCapture}) {
    Com<IMMDeviceCollection> devices;
    if (FAILED(list->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, devices.put()))) continue;
    UINT count = 0;
    devices->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
      Com<IMMDevice> device;
      Com<IPropertyStore> props;
      Com<IAudioClient> client;
      if (FAILED(devices->Item(i, device.put()))) continue;
      DeviceInfo d;
      LPWSTR id = nullptr;
      if (SUCCEEDED(device->GetId(&id))) {
        d.id = win::utf8(id);
        CoTaskMemFree(id);
      }
      if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, props.put()))) {
        PROPVARIANT name;
        PropVariantInit(&name);
        if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &name)) && name.vt == VT_LPWSTR) d.name = win::utf8(name.pwszVal);
        PropVariantClear(&name);
      }
      int channels = 2;
      if (SUCCEEDED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.put())))) channels = mixChannels(client.p);
      (flow == eRender ? d.outputs : d.inputs) = channels;
      out.push_back(std::move(d));
    }
  }
  return out;
}

std::unique_ptr<AudioDevice> openOutput(AudioEngine& e, const std::string& id, int buffer, int master, int preview) {
  return std::make_unique<Output>(e, id, buffer, master, preview);
}

std::unique_ptr<AudioDevice> openInput(AudioEngine& e, const std::string& id, int channel, float gain) {
  return std::make_unique<Input>(e, id, channel, gain);
}

AudioData::~AudioData() {
  stop_ = true;
  if (worker_.joinable()) worker_.join();
}

std::shared_ptr<AudioData> makeAudio(std::vector<int16_t> interleaved) {
  auto a = std::make_shared<AudioData>();
  a->frames_ = interleaved.size() / 2;
  a->samples_ = std::move(interleaved);
  a->ready_ = a->frames_;
  return a;
}

std::shared_ptr<AudioData> openAudio(const std::string& path, std::string& error) {
  ComScope com;
  static const HRESULT started = MFStartup(MF_VERSION);
  (void)started;
  const auto configure = [](Com<IMFSourceReader>& reader, const std::wstring& file) {
    Com<IMFAttributes> attrs;
    MFCreateAttributes(attrs.put(), 1);
    attrs->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
    if (FAILED(MFCreateSourceReaderFromURL(file.c_str(), attrs.p, reader.put()))) return false;
    reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    if (FAILED(reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE))) return false;
    Com<IMFMediaType> type;
    MFCreateMediaType(type.put());
    type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
    type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kSampleRate);
    type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
    type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, kSampleRate * 4);
    return SUCCEEDED(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, type.p));
  };
  const std::wstring file = win::wide(path);
  Com<IMFSourceReader> reader;
  if (!configure(reader, file)) return nullptr;  // sin pista de audio
  PROPVARIANT v;
  PropVariantInit(&v);
  double seconds = 0;
  if (SUCCEEDED(reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &v)) && v.vt == VT_UI8) seconds = v.uhVal.QuadPart / 1e7;
  PropVariantClear(&v);
  if (!(seconds > 0) || !std::isfinite(seconds)) {
    error = "duración de audio no válida";
    return nullptr;
  }
  auto a = std::make_shared<AudioData>();
  a->truncated_ = seconds > kMaxSeconds;
  seconds = std::min(seconds, kMaxSeconds);
  a->frames_ = static_cast<size_t>(seconds * kSampleRate) + 1;
  a->samples_.assign(a->frames_ * 2, 0);
  AudioData* raw = a.get();
  a->worker_ = std::thread([raw, configure, file] {
    ComScope threadCom;
    Com<IMFSourceReader> r;
    if (!configure(r, file)) return;
    size_t pos = 0;
    while (!raw->stop_ && pos < raw->frames_) {
      DWORD flags = 0;
      Com<IMFSample> sample;
      if (FAILED(r->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, nullptr, sample.put())) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) break;
      if (!sample) continue;
      Com<IMFMediaBuffer> buffer;
      BYTE* data = nullptr;
      DWORD bytes = 0;
      if (FAILED(sample->ConvertToContiguousBuffer(buffer.put())) || FAILED(buffer->Lock(&data, nullptr, &bytes))) continue;
      const size_t count = std::min<size_t>(bytes / 4, raw->frames_ - pos);
      std::memcpy(raw->samples_.data() + pos * 2, data, count * 4);
      buffer->Unlock();
      pos += count;
      raw->ready_.store(pos, std::memory_order_release);
    }
  });
  return a;
}

}  // namespace vj::audio
