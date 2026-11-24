#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>

#include "media/IMediaSource.h"

namespace vj::media {
namespace {

constexpr double kLookahead = 0.35;     // segundos decodificados por delante del reloj
constexpr double kReverseWindow = 0.6;  // segundos que se decodifican antes del reloj al ir atrás
constexpr double kReversePrefetch = 0.25;  // al ir atrás, relee antes de agotar la ventana
constexpr double kJumpLimit = 1.0;      // más allá de esto compensa reabrir el lector
constexpr size_t kCacheBytes = 256u << 20;

// Decodifica en un hilo propio alrededor del tiempo pedido y conserva una caché por
// tiempo, de modo que adelante, atrás, ping-pong y saltos usen el mismo camino.
class AvfMediaSource final : public IMediaSource {
 public:
  AvfMediaSource(AVURLAsset* asset, AVAssetTrack* track, MediaInfo info)
      : asset_(asset), track_(track), info_(info), worker_([this] { run(); }) {}

  ~AvfMediaSource() override {
    {
      std::lock_guard lock(mutex_);
      stop_ = true;
    }
    wake_.notify_all();
    worker_.join();
    // El lector debe liberarse aquí: el hilo ya no lo usa.
    reader_ = nil;
  }

  MediaInfo info() const override { return info_; }

  FramePtr frameAt(double t) override {
    std::lock_guard lock(mutex_);
    reverse_ = t < target_;
    target_ = t;
    resting_ = false;
    wake_.notify_all();
    if (cache_.empty()) return nullptr;
    auto it = cache_.upper_bound(t);
    if (it != cache_.begin()) --it;
    return it->second;
  }

 void release() override {
    std::lock_guard lock(mutex_);
    resting_ = true;
    if (cache_.size() > 1) {
      // Conserva un fotograma: basta para que el clip vuelva a mostrarse al instante.
      auto keep = cache_.begin();
      cache_.erase(std::next(keep), cache_.end());
      cacheBytes_ = keep->second->pixels.size();
    }
    reading_ = false;
  }

 private:
  bool mustReopen() const {
    if (!reading_ || target_ < windowStart_ - 1e-6 || target_ > readPos_ + kJumpLimit) return true;
    return reverse_ && windowStart_ > 0 && target_ < windowStart_ + kReversePrefetch;
  }

  bool needsWork() const { return !resting_ && (mustReopen() || (!eof_ && readPos_ < target_ + kLookahead)); }

  bool openReader(double fileTime) {
    NSError* err = nil;
    [reader_ cancelReading];
    reader_ = [[AVAssetReader alloc] initWithAsset:asset_ error:&err];
    if (!reader_) return false;
    output_ = [[AVAssetReaderTrackOutput alloc]
        initWithTrack:track_
       outputSettings:@{(id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)}];
    output_.alwaysCopiesSampleData = NO;
    [reader_ addOutput:output_];
    if (fileTime > 0)
      reader_.timeRange = CMTimeRangeMake(CMTimeMakeWithSeconds(fileTime, 600000), kCMTimePositiveInfinity);
    return [reader_ startReading];
  }

  std::shared_ptr<VideoFrame> readFrame() {
    CMSampleBufferRef sample = [output_ copyNextSampleBuffer];
    if (!sample) return nullptr;
    CVImageBufferRef image = CMSampleBufferGetImageBuffer(sample);
    if (!image) {
      CFRelease(sample);
      return nullptr;
    }
    auto f = std::make_shared<VideoFrame>();
    f->time = CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sample));
    f->format = gfx::PixelFormat::BGRA8;
    CVPixelBufferLockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
    f->width = static_cast<int>(CVPixelBufferGetWidth(image));
    f->height = static_cast<int>(CVPixelBufferGetHeight(image));
    f->stride = static_cast<int>(CVPixelBufferGetBytesPerRow(image));
    const auto* base = static_cast<const uint8_t*>(CVPixelBufferGetBaseAddress(image));
    f->pixels.assign(base, base + static_cast<size_t>(f->stride) * f->height);
    CVPixelBufferUnlockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
    CFRelease(sample);
    return f;
  }

  void evict() {
    while (cacheBytes_ > kCacheBytes && cache_.size() > 2) {
      auto first = cache_.begin();
      auto last = std::prev(cache_.end());
      auto victim = target_ - first->first > last->first - target_ ? first : last;
      const bool front = victim == first;
      cacheBytes_ -= victim->second->pixels.size();
      cache_.erase(victim);
      // Lo expulsado ya no está disponible: volver a ese tiempo debe releerlo.
      if (front) windowStart_ = cache_.begin()->first;
    }
  }

  void run() {
    for (;;) {
      bool reopen = false;
      double from = 0;
      {
        std::unique_lock lock(mutex_);
        wake_.wait(lock, [this] { return stop_ || needsWork(); });
        if (stop_) break;
        if (mustReopen()) {
          reopen = true;
          from = std::max(0.0, target_ - (reverse_ ? kReverseWindow : 0.0));
          windowStart_ = from;
          readPos_ = from;
          eof_ = false;
          reading_ = true;
        }
      }
      @autoreleasepool {
        if (reopen && !openReader(from)) break;
        std::shared_ptr<VideoFrame> frame = readFrame();
        std::lock_guard lock(mutex_);
        if (!frame) {
          eof_ = true;
        } else {
          readPos_ = frame->time;
          cacheBytes_ += frame->pixels.size();
          auto [it, fresh] = cache_.insert_or_assign(frame->time, std::move(frame));
          if (!fresh) cacheBytes_ -= it->second->pixels.size();
          evict();
        }
      }
    }
    [reader_ cancelReading];
  }

  AVURLAsset* asset_;
  AVAssetTrack* track_;
  MediaInfo info_;
  AVAssetReader* reader_ = nil;
  AVAssetReaderTrackOutput* output_ = nil;

  std::mutex mutex_;
  std::condition_variable wake_;
  std::map<double, FramePtr> cache_;
  size_t cacheBytes_ = 0;
  double target_ = 0;
  double windowStart_ = 0;
  double readPos_ = 0;
  bool reverse_ = false;
  bool reading_ = false;
  bool eof_ = false;
  bool resting_ = false;
  bool stop_ = false;
  std::thread worker_;
};

}  // namespace

SourcePtr openAvfMedia(const std::string& path, std::string& error) {
  @autoreleasepool {
    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    if (![[NSFileManager defaultManager] fileExistsAtPath:url.path]) {
      error = "el archivo no existe";
      return nullptr;
    }
    AVURLAsset* asset = [AVURLAsset URLAssetWithURL:url options:nil];
    AVAssetTrack* track = [asset tracksWithMediaType:AVMediaTypeVideo].firstObject;
    if (!track) {
      error = "el archivo no tiene pista de vídeo legible";
      return nullptr;
    }
    MediaInfo info;
    info.duration = CMTimeGetSeconds(asset.duration);
    info.width = static_cast<int>(track.naturalSize.width);
    info.height = static_cast<int>(track.naturalSize.height);
    if (!(info.duration > 0) || !std::isfinite(info.duration)) {
      error = "duración de vídeo no válida";
      return nullptr;
    }
    return std::make_shared<AvfMediaSource>(asset, track, info);
  }
}

}  // namespace vj::media
