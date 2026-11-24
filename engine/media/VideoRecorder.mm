#import <AVFoundation/AVFoundation.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

#include "media/IMediaSource.h"

namespace vj::media {
namespace {

constexpr size_t kQueueFrames = 24;

class AvfRecorder final : public VideoRecorder {
 public:
  AvfRecorder(const std::string& path, int w, int h, const std::string& codec) : width_(w), height_(h) {
    @try {
    @autoreleasepool {
      NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
      NSError* err = nil;
      const bool prores = codec == "ProRes 422";
      writer_ = [AVAssetWriter assetWriterWithURL:url fileType:prores ? AVFileTypeQuickTimeMovie : AVFileTypeMPEG4 error:&err];
      if (!writer_) throw std::runtime_error("no se pudo crear el archivo");
      AVVideoCodecType type = prores ? AVVideoCodecTypeAppleProRes422 : codec == "HEVC" ? AVVideoCodecTypeHEVC : AVVideoCodecTypeH264;
      input_ = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo
                                                  outputSettings:@{AVVideoCodecKey : type, AVVideoWidthKey : @(w), AVVideoHeightKey : @(h)}];
      input_.expectsMediaDataInRealTime = YES;
      adaptor_ = [AVAssetWriterInputPixelBufferAdaptor assetWriterInputPixelBufferAdaptorWithAssetWriterInput:input_
          sourcePixelBufferAttributes:@{(id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA),
                                        (id)kCVPixelBufferWidthKey : @(w), (id)kCVPixelBufferHeightKey : @(h)}];
      if (![writer_ canAddInput:input_]) throw std::runtime_error("códec no disponible");
      [writer_ addInput:input_];
      if (![writer_ startWriting]) throw std::runtime_error("no se pudo empezar a grabar");
      [writer_ startSessionAtSourceTime:kCMTimeZero];
    }
    } @catch (NSException* e) {
      throw std::runtime_error(std::string("no se pudo grabar: ") + e.reason.UTF8String);
    }
    worker_ = std::thread([this] { run(); });
  }

  ~AvfRecorder() override { finish(); }

  void addFrame(std::vector<uint8_t> rgba, double time) override {
    std::lock_guard lock(mutex_);
    // La composición puede cambiar de tamaño a mitad de grabación: esos fotogramas no caben.
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
    @autoreleasepool {
      [input_ markAsFinished];
      dispatch_semaphore_t done = dispatch_semaphore_create(0);
      [writer_ finishWritingWithCompletionHandler:^{ dispatch_semaphore_signal(done); }];
      // Un codificador colgado no debe dejar una grabación cerrándose para siempre.
      if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC)) != 0) {
        [writer_ cancelWriting];
        return ok_ = false;
      }
      ok_ = writer_.status == AVAssetWriterStatusCompleted && written_ > 0;
    }
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
    @autoreleasepool {
      for (int i = 0; i < 500 && !input_.readyForMoreMediaData; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(2));
      CVPixelBufferRef px = nullptr;
      if (!input_.readyForMoreMediaData || !adaptor_.pixelBufferPool || CVPixelBufferPoolCreatePixelBuffer(nullptr, adaptor_.pixelBufferPool, &px) != kCVReturnSuccess) {
        ++dropped_;
        return;
      }
      CVPixelBufferLockBaseAddress(px, 0);
      auto* base = static_cast<uint8_t*>(CVPixelBufferGetBaseAddress(px));
      const size_t stride = CVPixelBufferGetBytesPerRow(px);
      for (int y = 0; y < height_; ++y) {
        const uint8_t* s = item.rgba.data() + static_cast<size_t>(y) * width_ * 4;
        uint8_t* d = base + y * stride;
        for (int x = 0; x < width_; ++x) {
          // La composición es premultiplicada: sobre negro basta con opacar el alfa.
          d[x * 4] = s[x * 4 + 2];
          d[x * 4 + 1] = s[x * 4 + 1];
          d[x * 4 + 2] = s[x * 4];
          d[x * 4 + 3] = 255;
        }
      }
      CVPixelBufferUnlockBaseAddress(px, 0);
      if ([adaptor_ appendPixelBuffer:px withPresentationTime:CMTimeMakeWithSeconds(item.time, 600)]) ++written_;
      else ++dropped_;
      CVPixelBufferRelease(px);
    }
  }

  void run() {
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

  AVAssetWriter* writer_ = nil;
  AVAssetWriterInput* input_ = nil;
  AVAssetWriterInputPixelBufferAdaptor* adaptor_ = nil;
  int width_, height_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<Item> queue_;
  std::atomic<long> written_{0}, dropped_{0};
  bool stop_ = false, finished_ = false, ok_ = false;
  std::thread worker_;
};

}  // namespace

std::unique_ptr<VideoRecorder> makeRecorder(const std::string& path, int width, int height, const std::string& codec) {
  // H.264 y HEVC exigen dimensiones pares.
  if (width <= 0 || height <= 0 || (width & 1) || (height & 1)) throw std::runtime_error("la resolución debe ser par y mayor que cero");
  return std::make_unique<AvfRecorder>(path, width, height, codec);
}

}  // namespace vj::media
