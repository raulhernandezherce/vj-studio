#import <AVFoundation/AVFoundation.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "audio/AudioData.h"

namespace vj::audio {
namespace {
constexpr double kMaxSeconds = 30 * 60;  // 30 minutos ocupan unos 345 MB
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
  @autoreleasepool {
    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    AVURLAsset* asset = [AVURLAsset URLAssetWithURL:url options:nil];
    AVAssetTrack* track = [asset tracksWithMediaType:AVMediaTypeAudio].firstObject;
    if (!track) return nullptr;
    double seconds = CMTimeGetSeconds(asset.duration);
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
    a->worker_ = std::thread([raw, asset, track] {
      @autoreleasepool {
        NSError* err = nil;
        AVAssetReader* reader = [[AVAssetReader alloc] initWithAsset:asset error:&err];
        AVAssetReaderTrackOutput* output = [[AVAssetReaderTrackOutput alloc] initWithTrack:track outputSettings:@{
          AVFormatIDKey : @(kAudioFormatLinearPCM), AVSampleRateKey : @(kSampleRate), AVNumberOfChannelsKey : @2,
          AVLinearPCMBitDepthKey : @16, AVLinearPCMIsFloatKey : @NO, AVLinearPCMIsBigEndianKey : @NO, AVLinearPCMIsNonInterleaved : @NO}];
        output.alwaysCopiesSampleData = NO;
        [reader addOutput:output];
        if (![reader startReading]) return;
        size_t pos = 0;
        while (!raw->stop_ && pos < raw->frames_) {
          @autoreleasepool {
            CMSampleBufferRef sample = [output copyNextSampleBuffer];
            if (!sample) break;
            CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sample);
            const size_t bytes = CMBlockBufferGetDataLength(block);
            const size_t count = std::min(bytes / 4, raw->frames_ - pos);
            CMBlockBufferCopyDataBytes(block, 0, count * 4, raw->samples_.data() + pos * 2);
            pos += count;
            raw->ready_.store(pos, std::memory_order_release);
            CFRelease(sample);
          }
        }
        [reader cancelReading];
      }
    });
    return a;
  }
}

}  // namespace vj::audio
