// Vídeo, cámaras y grabación con Media Foundation. Equivale a AvfMediaSource, CameraSource y VideoRecorder.
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>

#include "media/IMediaSource.h"
#include "platform-win/WinCommon.h"

namespace vj::media {
namespace {

using win::Com;
using win::ComScope;

void startMf() {
  static const HRESULT once = MFStartup(MF_VERSION);
  (void)once;
}

constexpr size_t kCache = 16;
constexpr double kSeekGap = 1.0;

// Configura el lector para entregar BGRA y devuelve el tamaño real.
bool useRgb32(IMFSourceReader* reader, int& width, int& height) {
  reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
  reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
  Com<IMFMediaType> type;
  if (FAILED(MFCreateMediaType(type.put()))) return false;
  type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
  if (FAILED(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, type.p))) return false;
  Com<IMFMediaType> actual;
  if (FAILED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, actual.put()))) return false;
  UINT32 w = 0, h = 0;
  MFGetAttributeSize(actual.p, MF_MT_FRAME_SIZE, &w, &h);
  width = static_cast<int>(w);
  height = static_cast<int>(h);
  return width > 0 && height > 0;
}

// Copia a BGRA con la fila superior primero. RGB32 no trae alfa útil.
FramePtr toFrame(IMFSample* sample, int width, int height, double time) {
  Com<IMFMediaBuffer> buffer;
  if (FAILED(sample->ConvertToContiguousBuffer(buffer.put()))) return nullptr;
  auto f = std::make_shared<VideoFrame>();
  f->width = width;
  f->height = height;
  f->stride = width * 4;
  f->format = gfx::PixelFormat::BGRA8;
  f->time = time;
  f->pixels.resize(static_cast<size_t>(f->stride) * height);
  Com<IMF2DBuffer> flat;
  BYTE* src = nullptr;
  LONG pitch = 0;
  DWORD length = 0;
  bool locked2d = false;
  if (SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(flat.put()))) && SUCCEEDED(flat->Lock2D(&src, &pitch))) {
    locked2d = true;
  } else {
    if (FAILED(buffer->Lock(&src, nullptr, &length))) return nullptr;
    if (length < static_cast<DWORD>(f->stride * height)) {
      buffer->Unlock();
      return nullptr;
    }
    pitch = f->stride;
  }
  for (int y = 0; y < height; ++y) {
    uint8_t* d = f->pixels.data() + static_cast<size_t>(y) * f->stride;
    std::memcpy(d, src + static_cast<ptrdiff_t>(y) * pitch, static_cast<size_t>(f->stride));
    for (int x = 0; x < width; ++x) d[x * 4 + 3] = 255;
  }
  if (locked2d) flat->Unlock2D();
  else buffer->Unlock();
  return f;
}

class VideoFile final : public IMediaSource {
 public:
  VideoFile(const std::string& path, std::string& error) : path_(win::wide(path)) {
    ComScope com;
    startMf();
    Com<IMFSourceReader> reader;
    if (!open(reader) || !useRgb32(reader.p, width_, height_)) {
      error = "no se pudo abrir el vídeo";
      return;
    }
    PROPVARIANT v;
    PropVariantInit(&v);
    if (SUCCEEDED(reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &v)) && v.vt == VT_UI8) duration_ = v.uhVal.QuadPart / 1e7;
    PropVariantClear(&v);
    if (duration_ <= 0) {
      error = "el vídeo no tiene duración";
      return;
    }
    ok_ = true;
    worker_ = std::thread([this] { run(); });
  }
  ~VideoFile() override {
    stop_ = true;
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
  }
  bool ok() const { return ok_; }
  MediaInfo info() const override { return {duration_, width_, height_, false}; }

  FramePtr frameAt(double t) override {
    std::lock_guard lock(mutex_);
    wanted_ = t;
    active_ = true;
    wake_.notify_one();
    FramePtr best;
    for (const FramePtr& f : cache_)
      if (!best || std::abs(f->time - t) < std::abs(best->time - t)) best = f;
    return best;
  }
  void release() override {
    std::lock_guard lock(mutex_);
    active_ = false;
    cache_.clear();
  }

 private:
  bool open(Com<IMFSourceReader>& reader) {
    Com<IMFAttributes> attrs;
    MFCreateAttributes(attrs.put(), 1);
    attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    return SUCCEEDED(MFCreateSourceReaderFromURL(path_.c_str(), attrs.p, reader.put()));
  }
  void run() {
    ComScope com;
    Com<IMFSourceReader> reader;
    if (!open(reader) || !useRgb32(reader.p, width_, height_)) return;
    double last = -1;
    while (!stop_) {
      double target;
      {
        std::unique_lock lock(mutex_);
        const auto ahead = [&] {
          return !cache_.empty() && cache_.back()->time >= wanted_ + 0.25 && cache_.size() >= kCache && cache_.front()->time <= wanted_;
        };
        wake_.wait(lock, [&] { return stop_ || (active_ && !ahead()); });
        if (stop_) return;
        target = wanted_;
        if (last < 0 || target < last - 0.05 - (cache_.empty() ? 0 : last - cache_.front()->time) || target > last + kSeekGap) {
          cache_.clear();
          PROPVARIANT pos;
          PropVariantInit(&pos);
          pos.vt = VT_I8;
          pos.hVal.QuadPart = static_cast<LONGLONG>(target * 1e7);
          reader->SetCurrentPosition(GUID_NULL, pos);
          PropVariantClear(&pos);
          last = -1;
        }
      }
      DWORD flags = 0;
      LONGLONG stamp = 0;
      Com<IMFSample> sample;
      if (FAILED(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, &stamp, sample.put()))) continue;
      if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
        std::unique_lock lock(mutex_);
        wake_.wait_for(lock, std::chrono::milliseconds(50));
        last = -1;  // al volver a pedir, se busca de nuevo
        continue;
      }
      if (!sample) continue;
      last = stamp / 1e7;
      if (last < target - 0.1) continue;  // tras buscar: hasta llegar, nada que guardar
      FramePtr f = toFrame(sample.p, width_, height_, last);
      if (!f) continue;
      std::lock_guard lock(mutex_);
      cache_.push_back(std::move(f));
      while (cache_.size() > kCache) cache_.pop_front();
    }
  }

  std::wstring path_;
  int width_ = 0, height_ = 0;
  double duration_ = 0;
  bool ok_ = false;
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<FramePtr> cache_;
  double wanted_ = 0;
  bool active_ = false;
  std::atomic<bool> stop_{false};
  std::thread worker_;
};

class Camera final : public IMediaSource {
 public:
  Camera(IMFActivate* device, std::string& error) {
    startMf();
    Com<IMFMediaSource> source;
    if (FAILED(device->ActivateObject(IID_PPV_ARGS(source.put())))) {
      error = "no se pudo abrir la cámara: revisa Privacidad > Cámara en Configuración";
      return;
    }
    Com<IMFAttributes> attrs;
    MFCreateAttributes(attrs.put(), 1);
    attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    if (FAILED(MFCreateSourceReaderFromMediaSource(source.p, attrs.p, reader_.put())) || !useRgb32(reader_.p, width_, height_)) {
      error = "la cámara no entrega un formato utilizable";
      return;
    }
    ok_ = true;
    worker_ = std::thread([this] { run(); });
  }
  ~Camera() override {
    stop_ = true;
    if (worker_.joinable()) worker_.join();
  }
  bool ok() const { return ok_; }
  MediaInfo info() const override { return {0, width_, height_, false}; }
  FramePtr frameAt(double) override {
    std::lock_guard lock(mutex_);
    return latest_;
  }

 private:
  void run() {
    ComScope com;
    while (!stop_) {
      DWORD flags = 0;
      LONGLONG stamp = 0;
      Com<IMFSample> sample;
      if (FAILED(reader_->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, &stamp, sample.put()))) return;
      if (!sample) continue;
      if (FramePtr f = toFrame(sample.p, width_, height_, stamp / 1e7)) {
        std::lock_guard lock(mutex_);
        latest_ = std::move(f);
      }
    }
  }
  Com<IMFSourceReader> reader_;
  int width_ = 0, height_ = 0;
  bool ok_ = false;
  std::atomic<bool> stop_{false};
  std::mutex mutex_;
  FramePtr latest_;
  std::thread worker_;
};

struct Devices {
  IMFActivate** list = nullptr;
  UINT32 count = 0;
  Devices() {
    startMf();
    Com<IMFAttributes> attrs;
    MFCreateAttributes(attrs.put(), 1);
    attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    MFEnumDeviceSources(attrs.p, &list, &count);
  }
  ~Devices() {
    for (UINT32 i = 0; i < count; ++i) list[i]->Release();
    CoTaskMemFree(list);
  }
  static std::string text(IMFActivate* d, REFGUID key) {
    WCHAR* s = nullptr;
    UINT32 n = 0;
    if (FAILED(d->GetAllocatedString(key, &s, &n))) return {};
    std::string out = win::utf8(s);
    CoTaskMemFree(s);
    return out;
  }
};

constexpr size_t kQueueFrames = 24;

class MfRecorder final : public VideoRecorder {
 public:
  MfRecorder(const std::string& path, int w, int h, const std::string& codec) : width_(w), height_(h) {
    if (codec == "ProRes 422") throw std::runtime_error("ProRes no está disponible en Windows");
    ComScope com;
    startMf();
    if (FAILED(MFCreateSinkWriterFromURL(win::wide(path).c_str(), nullptr, nullptr, writer_.put()))) throw std::runtime_error("no se pudo crear el archivo");
    Com<IMFMediaType> out, in;
    MFCreateMediaType(out.put());
    out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    out->SetGUID(MF_MT_SUBTYPE, codec == "HEVC" ? MFVideoFormat_HEVC : MFVideoFormat_H264);
    out->SetUINT32(MF_MT_AVG_BITRATE, static_cast<UINT32>(std::min<int64_t>(int64_t{w} * h * 8, 200000000)));
    out->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(out.p, MF_MT_FRAME_SIZE, w, h);
    MFSetAttributeRatio(out.p, MF_MT_FRAME_RATE, 60, 1);
    MFSetAttributeRatio(out.p, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (FAILED(writer_->AddStream(out.p, &stream_))) throw std::runtime_error("códec no disponible");
    MFCreateMediaType(in.put());
    in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    in->SetUINT32(MF_MT_DEFAULT_STRIDE, static_cast<UINT32>(w * 4));
    MFSetAttributeSize(in.p, MF_MT_FRAME_SIZE, w, h);
    MFSetAttributeRatio(in.p, MF_MT_FRAME_RATE, 60, 1);
    MFSetAttributeRatio(in.p, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (FAILED(writer_->SetInputMediaType(stream_, in.p, nullptr))) throw std::runtime_error("códec no disponible");
    if (FAILED(writer_->BeginWriting())) throw std::runtime_error("no se pudo empezar a grabar");
    worker_ = std::thread([this] { run(); });
  }
  ~MfRecorder() override { (void)finish(); }

  void addFrame(std::vector<uint8_t> rgba, double time) override {
    std::lock_guard lock(mutex_);
    if (queue_.size() >= kQueueFrames || rgba.size() != static_cast<size_t>(width_) * height_ * 4) {
      ++dropped_;  // el disco no da abasto: se pierde este, la salida no se frena
      return;
    }
    queue_.push_back({std::move(rgba), time});
    wake_.notify_one();
  }
  bool finish() override {
    {
      std::lock_guard lock(mutex_);
      if (finished_) return ok_;
      finished_ = true;
      stop_ = true;
      wake_.notify_all();
    }
    worker_.join();
    ok_ = written_ > 0 && SUCCEEDED(writer_->Finalize());
    return ok_;
  }
  long framesWritten() const override { return written_; }
  long framesDropped() const override { return dropped_; }

 private:
  struct Item {
    std::vector<uint8_t> rgba;
    double time;
  };
  void write(const Item& item) {
    const DWORD bytes = static_cast<DWORD>(width_) * height_ * 4;
    Com<IMFMediaBuffer> buffer;
    Com<IMFSample> sample;
    BYTE* d = nullptr;
    if (FAILED(MFCreateMemoryBuffer(bytes, buffer.put())) || FAILED(buffer->Lock(&d, nullptr, nullptr))) {
      ++dropped_;
      return;
    }
    for (size_t i = 0; i < bytes; i += 4) {
      d[i] = item.rgba[i + 2];
      d[i + 1] = item.rgba[i + 1];
      d[i + 2] = item.rgba[i];
      d[i + 3] = 255;
    }
    buffer->Unlock();
    buffer->SetCurrentLength(bytes);
    MFCreateSample(sample.put());
    sample->AddBuffer(buffer.p);
    sample->SetSampleTime(static_cast<LONGLONG>(item.time * 1e7));
    sample->SetSampleDuration(static_cast<LONGLONG>(1e7 / 60));
    if (SUCCEEDED(writer_->WriteSample(stream_, sample.p))) ++written_;
    else ++dropped_;
  }
  void run() {
    ComScope com;
    for (;;) {
      Item item;
      {
        std::unique_lock lock(mutex_);
        wake_.wait(lock, [this] { return stop_ || !queue_.empty(); });
        if (queue_.empty()) return;
        item = std::move(queue_.front());
        queue_.pop_front();
      }
      write(item);
    }
  }
  Com<IMFSinkWriter> writer_;
  DWORD stream_ = 0;
  int width_, height_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<Item> queue_;
  std::atomic<long> written_{0}, dropped_{0};
  bool stop_ = false, finished_ = false, ok_ = false;
  std::thread worker_;
};

}  // namespace

SourcePtr openAvfMedia(const std::string& path, std::string& error) {
  auto s = std::make_shared<VideoFile>(path, error);
  return s->ok() ? s : nullptr;
}

std::vector<CaptureDevice> listCaptureDevices() {
  ComScope com;
  std::vector<CaptureDevice> out;
  Devices d;
  for (UINT32 i = 0; i < d.count; ++i)
    out.push_back({Devices::text(d.list[i], MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK), Devices::text(d.list[i], MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME)});
  return out;
}

SourcePtr openCamera(const std::string& id, std::string& error) {
  ComScope com;
  Devices d;
  for (UINT32 i = 0; i < d.count; ++i)
    if (Devices::text(d.list[i], MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK) == id) {
      auto s = std::make_shared<Camera>(d.list[i], error);
      return s->ok() ? s : nullptr;
    }
  error = "cámara no encontrada";
  return nullptr;
}

std::unique_ptr<VideoRecorder> makeRecorder(const std::string& path, int width, int height, const std::string& codec) {
  if (width <= 0 || height <= 0 || (width & 1) || (height & 1)) throw std::runtime_error("la resolución debe ser par y mayor que cero");
  return std::make_unique<MfRecorder>(path, width, height, codec);
}

}  // namespace vj::media
