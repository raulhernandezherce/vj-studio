#import <AVFoundation/AVFoundation.h>
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <string>

// Vídeo H.264 de 2 s a 30 fps: rojo el primer segundo, azul el segundo.
bool writeFixtureVideo(const std::string& path) {
  @autoreleasepool {
    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    [[NSFileManager defaultManager] removeItemAtURL:url error:nil];
    NSError* err = nil;
    AVAssetWriter* writer = [AVAssetWriter assetWriterWithURL:url fileType:AVFileTypeMPEG4 error:&err];
    if (!writer) return false;
    const int w = 320, h = 180, fps = 30, total = 60;
    AVAssetWriterInput* input = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo
        outputSettings:@{AVVideoCodecKey : AVVideoCodecTypeH264, AVVideoWidthKey : @(w), AVVideoHeightKey : @(h)}];
    AVAssetWriterInputPixelBufferAdaptor* adaptor = [AVAssetWriterInputPixelBufferAdaptor
        assetWriterInputPixelBufferAdaptorWithAssetWriterInput:input
        sourcePixelBufferAttributes:@{(id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA),
                                      (id)kCVPixelBufferWidthKey : @(w), (id)kCVPixelBufferHeightKey : @(h)}];
    [writer addInput:input];
    if (![writer startWriting]) return false;
    [writer startSessionAtSourceTime:kCMTimeZero];
    for (int i = 0; i < total; ++i) {
      while (!input.readyForMoreMediaData) [NSThread sleepForTimeInterval:0.002];
      CVPixelBufferRef px = nullptr;
      CVPixelBufferPoolCreatePixelBuffer(nullptr, adaptor.pixelBufferPool, &px);
      if (!px) return false;
      CVPixelBufferLockBaseAddress(px, 0);
      auto* base = static_cast<uint8_t*>(CVPixelBufferGetBaseAddress(px));
      const size_t stride = CVPixelBufferGetBytesPerRow(px);
      const bool red = i < total / 2;
      for (int y = 0; y < h; ++y) {
        uint8_t* row = base + y * stride;
        for (int x = 0; x < w; ++x) {
          row[x * 4 + 0] = red ? 0 : 255;
          row[x * 4 + 1] = 0;
          row[x * 4 + 2] = red ? 255 : 0;
          row[x * 4 + 3] = 255;
        }
      }
      CVPixelBufferUnlockBaseAddress(px, 0);
      [adaptor appendPixelBuffer:px withPresentationTime:CMTimeMake(i, fps)];
      CVPixelBufferRelease(px);
    }
    [input markAsFinished];
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    [writer finishWritingWithCompletionHandler:^{ dispatch_semaphore_signal(done); }];
    dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
    return writer.status == AVAssetWriterStatusCompleted;
  }
}

// PNG de 4×4: rojo puro con alfa 128. El archivo guarda el alfa sin premultiplicar.
bool writeFixturePng(const std::string& path) {
  @autoreleasepool {
    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    uint8_t px[16 * 4];
    for (int i = 0; i < 16; ++i) {
      px[i * 4] = 128;
      px[i * 4 + 1] = 0;
      px[i * 4 + 2] = 0;
      px[i * 4 + 3] = 128;
    }
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate(px, 4, 4, 8, 16, cs, (uint32_t)kCGImageAlphaPremultipliedLast);
    CGImageRef img = CGBitmapContextCreateImage(ctx);
    CGImageDestinationRef dst = CGImageDestinationCreateWithURL((__bridge CFURLRef)url, (__bridge CFStringRef)UTTypePNG.identifier, 1, nullptr);
    CGImageDestinationAddImage(dst, img, nullptr);
    const bool ok = CGImageDestinationFinalize(dst);
    CFRelease(dst);
    CGImageRelease(img);
    CGContextRelease(ctx);
    CGColorSpaceRelease(cs);
    return ok;
  }
}
