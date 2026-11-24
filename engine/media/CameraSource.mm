#import <AVFoundation/AVFoundation.h>

#include <atomic>
#include <mutex>

#include "media/IMediaSource.h"

@interface VjCameraSink : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
@property(nonatomic) void* owner;
@end

namespace vj::media {
namespace {

class CameraSource final : public IMediaSource {
 public:
  CameraSource(AVCaptureDevice* device, std::string& error) {
    NSError* err = nil;
    AVCaptureDeviceInput* input = [AVCaptureDeviceInput deviceInputWithDevice:device error:&err];
    if (!input) {
      error = "no se pudo abrir la cámara";
      return;
    }
    session_ = [[AVCaptureSession alloc] init];
    if ([session_ canAddInput:input]) [session_ addInput:input];
    AVCaptureVideoDataOutput* output = [[AVCaptureVideoDataOutput alloc] init];
    output.videoSettings = @{(id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)};
    output.alwaysDiscardsLateVideoFrames = YES;
    sink_ = [[VjCameraSink alloc] init];
    sink_.owner = this;
    queue_ = dispatch_queue_create("vj.camera", DISPATCH_QUEUE_SERIAL);
    [output setSampleBufferDelegate:sink_ queue:queue_];
    if ([session_ canAddOutput:output]) [session_ addOutput:output];
    [session_ startRunning];
    ok_ = true;
  }

  ~CameraSource() override {
    [session_ stopRunning];
    // Vacía la cola: después ningún fotograma puede llegar a un objeto destruido.
    if (queue_) dispatch_sync(queue_, ^{});
  }

  bool ok() const { return ok_; }
  MediaInfo info() const override {
    std::lock_guard lock(mutex_);
    return {0, latest_ ? latest_->width : 1280, latest_ ? latest_->height : 720, false};
  }
  FramePtr frameAt(double) override {
    if (!session_.running) [session_ startRunning];
    std::lock_guard lock(mutex_);
    return latest_;
  }
  void release() override { [session_ stopRunning]; }

  void receive(CMSampleBufferRef sample) {
    CVImageBufferRef image = CMSampleBufferGetImageBuffer(sample);
    if (!image) return;
    auto f = std::make_shared<VideoFrame>();
    f->format = gfx::PixelFormat::BGRA8;
    CVPixelBufferLockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
    f->width = static_cast<int>(CVPixelBufferGetWidth(image));
    f->height = static_cast<int>(CVPixelBufferGetHeight(image));
    f->stride = static_cast<int>(CVPixelBufferGetBytesPerRow(image));
    const auto* base = static_cast<const uint8_t*>(CVPixelBufferGetBaseAddress(image));
    f->pixels.assign(base, base + static_cast<size_t>(f->stride) * f->height);
    CVPixelBufferUnlockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
    std::lock_guard lock(mutex_);
    latest_ = std::move(f);
  }

 private:
  AVCaptureSession* session_ = nil;
  VjCameraSink* sink_ = nil;
  dispatch_queue_t queue_ = nil;
  mutable std::mutex mutex_;
  FramePtr latest_;
  bool ok_ = false;
};

NSArray<AVCaptureDevice*>* devices() {
  NSArray* types = @[ AVCaptureDeviceTypeBuiltInWideAngleCamera, AVCaptureDeviceTypeExternal ];
  return [AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:types mediaType:AVMediaTypeVideo position:AVCaptureDevicePositionUnspecified].devices;
}

}  // namespace

std::vector<CaptureDevice> listCaptureDevices() {
  std::vector<CaptureDevice> out;
  @autoreleasepool {
    for (AVCaptureDevice* d in devices()) out.push_back({d.uniqueID.UTF8String, d.localizedName.UTF8String});
  }
  return out;
}

SourcePtr openCamera(const std::string& id, std::string& error) {
  @autoreleasepool {
    AVCaptureDevice* device = nil;
    for (AVCaptureDevice* d in devices())
      if (id == d.uniqueID.UTF8String) device = d;
    if (!device) {
      error = "cámara no encontrada";
      return nullptr;
    }
    const AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
    if (status == AVAuthorizationStatusDenied || status == AVAuthorizationStatusRestricted) {
      error = "sin permiso para usar la cámara: actívalo en Ajustes del Sistema";
      return nullptr;
    }
    if (status == AVAuthorizationStatusNotDetermined) {
      dispatch_semaphore_t done = dispatch_semaphore_create(0);
      __block BOOL granted = NO;
      [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo completionHandler:^(BOOL ok) {
        granted = ok;
        dispatch_semaphore_signal(done);
      }];
      dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
      if (!granted) {
        error = "sin permiso para usar la cámara";
        return nullptr;
      }
    }
    auto src = std::make_shared<CameraSource>(device, error);
    if (!src->ok()) return nullptr;
    return src;
  }
}

}  // namespace vj::media

@implementation VjCameraSink
- (void)captureOutput:(AVCaptureOutput*)output didOutputSampleBuffer:(CMSampleBufferRef)sample fromConnection:(AVCaptureConnection*)connection {
  static_cast<vj::media::CameraSource*>(self.owner)->receive(sample);
}
@end
