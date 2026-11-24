#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

#include "audio/AudioEngine.h"

namespace vj::audio {
namespace {

std::string cfString(CFStringRef s) {
  if (!s) return {};
  char buf[512] = {};
  CFStringGetCString(s, buf, sizeof buf, kCFStringEncodingUTF8);
  return buf;
}

int channelCount(AudioObjectID id, AudioObjectPropertyScope scope) {
  AudioObjectPropertyAddress a{kAudioDevicePropertyStreamConfiguration, scope, kAudioObjectPropertyElementMain};
  UInt32 size = 0;
  if (AudioObjectGetPropertyDataSize(id, &a, 0, nullptr, &size) != noErr) return 0;
  std::vector<uint8_t> mem(size);
  auto* list = reinterpret_cast<AudioBufferList*>(mem.data());
  if (AudioObjectGetPropertyData(id, &a, 0, nullptr, &size, list) != noErr) return 0;
  int n = 0;
  for (UInt32 i = 0; i < list->mNumberBuffers; ++i) n += static_cast<int>(list->mBuffers[i].mNumberChannels);
  return n;
}

AudioObjectID deviceFor(const std::string& uid) {
  AudioObjectPropertyAddress a{kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
  UInt32 size = 0;
  AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, nullptr, &size);
  std::vector<AudioObjectID> ids(size / sizeof(AudioObjectID));
  AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, &size, ids.data());
  for (auto id : ids) {
    AudioObjectPropertyAddress u{kAudioDevicePropertyDeviceUID, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    CFStringRef s = nullptr;
    UInt32 sz = sizeof s;
    if (AudioObjectGetPropertyData(id, &u, 0, nullptr, &sz, &s) == noErr && s) {
      const std::string value = cfString(s);
      CFRelease(s);
      if (value == uid) return id;
    }
  }
  return kAudioObjectUnknown;
}

AudioUnit makeHal() {
  AudioComponentDescription d{};
  d.componentType = kAudioUnitType_Output;
  d.componentSubType = kAudioUnitSubType_HALOutput;
  d.componentManufacturer = kAudioUnitManufacturer_Apple;
  AudioComponent c = AudioComponentFindNext(nullptr, &d);
  AudioUnit unit = nullptr;
  if (!c || AudioComponentInstanceNew(c, &unit) != noErr) throw std::runtime_error("sin unidad de audio");
  return unit;
}

class Output final : public AudioDevice {
 public:
  Output(AudioEngine& engine, const std::string& uid, int bufferFrames, int master, int preview)
      : engine_(engine), master_(master), preview_(preview), unit_(makeHal()) {
    if (!uid.empty()) {
      AudioObjectID id = deviceFor(uid);
      if (id == kAudioObjectUnknown) throw std::runtime_error("dispositivo de salida no encontrado");
      AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &id, sizeof id);
      channels_ = std::max(2, channelCount(id, kAudioObjectPropertyScopeOutput));
    } else {
      AudioObjectID id = kAudioObjectUnknown;
      UInt32 size = sizeof id;
      AudioObjectPropertyAddress a{kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
      AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, &size, &id);
      AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &id, sizeof id);
      channels_ = std::max(2, channelCount(id, kAudioObjectPropertyScopeOutput));
    }
    AudioStreamBasicDescription f{};
    f.mSampleRate = kSampleRate;
    f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    f.mFramesPerPacket = 1;
    f.mChannelsPerFrame = static_cast<UInt32>(channels_);
    f.mBitsPerChannel = 32;
    f.mBytesPerFrame = f.mBytesPerPacket = 4 * f.mChannelsPerFrame;
    AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f);
    UInt32 frames = static_cast<UInt32>(bufferFrames);
    AudioUnitSetProperty(unit_, kAudioDevicePropertyBufferFrameSize, kAudioUnitScope_Global, 0, &frames, sizeof frames);
    AURenderCallbackStruct cb{&Output::callback, this};
    AudioUnitSetProperty(unit_, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb);
    if (AudioUnitInitialize(unit_) != noErr) throw std::runtime_error("no se pudo iniciar la salida de audio");
    if (AudioOutputUnitStart(unit_) != noErr) throw std::runtime_error("no se pudo arrancar la salida de audio");
  }
  ~Output() override {
    AudioOutputUnitStop(unit_);
    AudioUnitUninitialize(unit_);
    AudioComponentInstanceDispose(unit_);
  }

 private:
  static OSStatus callback(void* self, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 frames, AudioBufferList* data) {
    auto* o = static_cast<Output*>(self);
    o->engine_.render(static_cast<float*>(data->mBuffers[0].mData), static_cast<int>(frames), o->channels_, o->master_, o->preview_);
    return noErr;
  }
  AudioEngine& engine_;
  int master_, preview_, channels_ = 2;
  AudioUnit unit_;
};

class Input final : public AudioDevice {
 public:
  Input(AudioEngine& engine, const std::string& uid, int channel, float gain)
      : engine_(engine), channel_(channel), gain_(gain), unit_(makeHal()) {
    UInt32 on = 1, off = 0;
    AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &on, sizeof on);
    AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &off, sizeof off);
    AudioObjectID id = deviceFor(uid);
    if (id == kAudioObjectUnknown) throw std::runtime_error("dispositivo de entrada no encontrado");
    AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &id, sizeof id);
    channels_ = std::max(1, channelCount(id, kAudioObjectPropertyScopeInput));
    AudioStreamBasicDescription f{};
    f.mSampleRate = kSampleRate;
    f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    f.mFramesPerPacket = 1;
    f.mChannelsPerFrame = static_cast<UInt32>(channels_);
    f.mBitsPerChannel = 32;
    f.mBytesPerFrame = f.mBytesPerPacket = 4 * f.mChannelsPerFrame;
    AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &f, sizeof f);
    AURenderCallbackStruct cb{&Input::callback, this};
    AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Global, 0, &cb, sizeof cb);
    if (AudioUnitInitialize(unit_) != noErr || AudioOutputUnitStart(unit_) != noErr) throw std::runtime_error("no se pudo arrancar la entrada de audio");
  }
  ~Input() override {
    AudioOutputUnitStop(unit_);
    AudioUnitUninitialize(unit_);
    AudioComponentInstanceDispose(unit_);
  }

 private:
  static OSStatus callback(void* self, AudioUnitRenderActionFlags* flags, const AudioTimeStamp* ts, UInt32 bus, UInt32 frames, AudioBufferList*) {
    auto* o = static_cast<Input*>(self);
    frames = std::min<UInt32>(frames, 4096);
    AudioBufferList list{};
    list.mNumberBuffers = 1;
    list.mBuffers[0].mNumberChannels = static_cast<UInt32>(o->channels_);
    list.mBuffers[0].mDataByteSize = frames * 4 * static_cast<UInt32>(o->channels_);
    list.mBuffers[0].mData = o->buffer_.data();
    if (AudioUnitRender(o->unit_, flags, ts, bus, frames, &list) != noErr) return noErr;
    const int ch = std::min(o->channel_, o->channels_ - 1);
    for (UInt32 i = 0; i < frames; ++i) o->mono_[i] = o->buffer_[i * static_cast<UInt32>(o->channels_) + static_cast<UInt32>(ch)] * o->gain_;
    o->engine_.pushInput(o->mono_.data(), static_cast<int>(frames));
    return noErr;
  }
  AudioEngine& engine_;
  int channel_, channels_ = 1;
  float gain_;
  AudioUnit unit_;
  std::vector<float> buffer_ = std::vector<float>(4096 * 64), mono_ = std::vector<float>(4096);
};

}  // namespace

std::vector<DeviceInfo> listDevices() {
  std::vector<DeviceInfo> out;
  AudioObjectPropertyAddress a{kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
  UInt32 size = 0;
  if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, nullptr, &size) != noErr) return out;
  std::vector<AudioObjectID> ids(size / sizeof(AudioObjectID));
  AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, &size, ids.data());
  for (auto id : ids) {
    DeviceInfo d;
    CFStringRef s = nullptr;
    UInt32 sz = sizeof s;
    AudioObjectPropertyAddress uid{kAudioDevicePropertyDeviceUID, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    if (AudioObjectGetPropertyData(id, &uid, 0, nullptr, &sz, &s) == noErr && s) {
      d.id = cfString(s);
      CFRelease(s);
    }
    s = nullptr;
    sz = sizeof s;
    AudioObjectPropertyAddress name{kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    if (AudioObjectGetPropertyData(id, &name, 0, nullptr, &sz, &s) == noErr && s) {
      d.name = cfString(s);
      CFRelease(s);
    }
    d.inputs = channelCount(id, kAudioObjectPropertyScopeInput);
    d.outputs = channelCount(id, kAudioObjectPropertyScopeOutput);
    out.push_back(std::move(d));
  }
  return out;
}

std::unique_ptr<AudioDevice> openOutput(AudioEngine& e, const std::string& id, int buffer, int master, int preview) {
  return std::make_unique<Output>(e, id, buffer, master, preview);
}

std::unique_ptr<AudioDevice> openInput(AudioEngine& e, const std::string& id, int channel, float gain) {
  return std::make_unique<Input>(e, id, channel, gain);
}

}  // namespace vj::audio
