#import <CoreGraphics/CoreGraphics.h>
#import <CoreText/CoreText.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <algorithm>

#include "media/IMediaSource.h"

namespace vj::media {
namespace {

CGImageRef makeImage(int width, int height, const uint8_t* rgba, CGImageAlphaInfo alpha) {
  CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CFDataRef data = CFDataCreate(nullptr, rgba, static_cast<CFIndex>(width) * height * 4);
  CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
  CGImageRef img = CGImageCreate(width, height, 8, 32, static_cast<size_t>(width) * 4, cs,
                                 static_cast<CGBitmapInfo>(alpha) | kCGBitmapByteOrder32Big, provider, nullptr, false,
                                 kCGRenderingIntentDefault);
  CGDataProviderRelease(provider);
  CFRelease(data);
  CGColorSpaceRelease(cs);
  return img;
}

}  // namespace

bool writePng(const std::string& path, int width, int height, const std::vector<uint8_t>& rgba) {
  @autoreleasepool {
    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    CGImageRef img = makeImage(width, height, rgba.data(), kCGImageAlphaNoneSkipLast);
    CGImageDestinationRef dst = CGImageDestinationCreateWithURL((__bridge CFURLRef)url, (__bridge CFStringRef)UTTypePNG.identifier, 1, nullptr);
    if (!dst) {
      CGImageRelease(img);
      return false;
    }
    CGImageDestinationAddImage(dst, img, nullptr);
    const bool ok = CGImageDestinationFinalize(dst);
    CFRelease(dst);
    CGImageRelease(img);
    return ok;
  }
}

FramePtr renderText(const std::string& text, double size, int width, int height) {
  @autoreleasepool {
    auto f = std::make_shared<VideoFrame>();
    f->width = width;
    f->height = height;
    f->stride = width * 4;
    f->format = gfx::PixelFormat::RGBA8;
    f->pixels.assign(static_cast<size_t>(f->stride) * height, 0);
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate(f->pixels.data(), width, height, 8, f->stride, cs,
                                             static_cast<uint32_t>(kCGImageAlphaPremultipliedLast) | static_cast<uint32_t>(kCGBitmapByteOrder32Big));
    CTFontRef font = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, size, nullptr);
    NSString* value = [NSString stringWithUTF8String:text.c_str()];
    if (!value) value = @"";
    NSDictionary* attrs = @{(id)kCTFontAttributeName : (__bridge id)font,
                            (id)kCTForegroundColorAttributeName : (__bridge id)CGColorGetConstantColor(kCGColorWhite)};
    NSArray<NSString*>* lines = [value componentsSeparatedByString:@"\n"];
    const double lineHeight = size * 1.2;
    double y = height / 2.0 + (lines.count - 1) * lineHeight / 2.0 - size * 0.35;
    // El contexto tiene el origen abajo y la fila 0 de datos arriba: el texto queda al derecho.
    for (NSString* lineText in lines) {
      NSAttributedString* a = [[NSAttributedString alloc] initWithString:lineText attributes:attrs];
      CTLineRef line = CTLineCreateWithAttributedString((__bridge CFAttributedStringRef)a);
      const double w = CTLineGetTypographicBounds(line, nullptr, nullptr, nullptr);
      CGContextSetTextPosition(ctx, (width - w) / 2, y);
      CTLineDraw(line, ctx);
      CFRelease(line);
      y -= lineHeight;
    }
    CFRelease(font);
    CGContextRelease(ctx);
    CGColorSpaceRelease(cs);
    return f;
  }
}

std::string thumbnailDataUrl(const VideoFrame& f, int width, int height) {
  @autoreleasepool {
    // Reducción por vecino más cercano, sobre negro: basta para reconocer el clip.
    std::vector<uint8_t> small(static_cast<size_t>(width) * height * 4);
    const bool bgra = f.format == gfx::PixelFormat::BGRA8;
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x) {
        const uint8_t* s = f.pixels.data() + static_cast<size_t>(y * f.height / height) * f.stride + static_cast<size_t>(x * f.width / width) * 4;
        uint8_t* d = small.data() + (static_cast<size_t>(y) * width + x) * 4;
        const int a = s[3];
        d[0] = static_cast<uint8_t>(s[bgra ? 2 : 0] * a / 255);
        d[1] = static_cast<uint8_t>(s[1] * a / 255);
        d[2] = static_cast<uint8_t>(s[bgra ? 0 : 2] * a / 255);
        d[3] = 255;
      }
    CGImageRef img = makeImage(width, height, small.data(), kCGImageAlphaNoneSkipLast);
    NSMutableData* out = [NSMutableData data];
    CGImageDestinationRef dst = CGImageDestinationCreateWithData((__bridge CFMutableDataRef)out, (__bridge CFStringRef)UTTypeJPEG.identifier, 1, nullptr);
    CGImageDestinationAddImage(dst, img, (__bridge CFDictionaryRef) @{(id)kCGImageDestinationLossyCompressionQuality : @0.6});
    const bool ok = CGImageDestinationFinalize(dst);
    CFRelease(dst);
    CGImageRelease(img);
    if (!ok) return {};
    return std::string("data:image/jpeg;base64,") + [[out base64EncodedStringWithOptions:0] UTF8String];
  }
}

}  // namespace vj::media
