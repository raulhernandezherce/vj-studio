#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>

#include <cstring>

#include "media/IMediaSource.h"

namespace vj::media {
namespace {

class ImageSource final : public IMediaSource {
 public:
  ImageSource(FramePtr f, bool straight) : frame_(std::move(f)), straight_(straight) {}
  MediaInfo info() const override { return {0, frame_->width, frame_->height, straight_}; }
  FramePtr frameAt(double) override { return frame_; }

 private:
  FramePtr frame_;
  bool straight_;
};

}  // namespace

SourcePtr openImage(const std::string& path, std::string& error) {
  @autoreleasepool {
    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    if (![[NSFileManager defaultManager] fileExistsAtPath:url.path]) {
      error = "el archivo no existe";
      return nullptr;
    }
    CGImageSourceRef src = CGImageSourceCreateWithURL((__bridge CFURLRef)url, nullptr);
    CGImageRef img = src ? CGImageSourceCreateImageAtIndex(src, 0, nullptr) : nullptr;
    if (src) CFRelease(src);
    if (!img) {
      error = "imagen no legible";
      return nullptr;
    }
    auto f = std::make_shared<VideoFrame>();
    f->width = static_cast<int>(CGImageGetWidth(img));
    f->height = static_cast<int>(CGImageGetHeight(img));
    f->stride = f->width * 4;
    f->format = gfx::PixelFormat::RGBA8;
    f->pixels.resize(static_cast<size_t>(f->stride) * f->height);

    // ImageIO entrega el PNG sin premultiplicar: se conserva tal cual cuando el formato es
    // RGBA de 8 bits, porque Quartz solo sabría redibujarlo premultiplicado.
    const CGImageAlphaInfo alpha = CGImageGetAlphaInfo(img);
    const CGBitmapInfo order = CGImageGetBitmapInfo(img) & kCGBitmapByteOrderMask;
    const bool rawStraight = alpha == kCGImageAlphaLast && CGImageGetBitsPerPixel(img) == 32 &&
                             CGImageGetBitsPerComponent(img) == 8 &&
                             (order == kCGBitmapByteOrderDefault || order == kCGBitmapByteOrder32Big);
    if (rawStraight) {
      CFDataRef data = CGDataProviderCopyData(CGImageGetDataProvider(img));
      const auto* src8 = CFDataGetBytePtr(data);
      const size_t stride = CGImageGetBytesPerRow(img);
      for (int y = 0; y < f->height; ++y)
        std::memcpy(f->pixels.data() + static_cast<size_t>(y) * f->stride, src8 + y * stride, f->stride);
      CFRelease(data);
    } else {
      CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
      CGContextRef ctx = CGBitmapContextCreate(f->pixels.data(), f->width, f->height, 8, f->stride, cs,
                                               static_cast<uint32_t>(kCGImageAlphaPremultipliedLast) | static_cast<uint32_t>(kCGBitmapByteOrder32Big));
      CGContextDrawImage(ctx, CGRectMake(0, 0, f->width, f->height), img);
      CGContextRelease(ctx);
      CGColorSpaceRelease(cs);
    }
    CGImageRelease(img);
    return std::make_shared<ImageSource>(std::move(f), rawStraight);
  }
}

}  // namespace vj::media
