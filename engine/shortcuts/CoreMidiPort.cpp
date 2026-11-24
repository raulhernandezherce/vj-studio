#include "shortcuts/CoreMidiPort.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/CoreMIDI.h>
#include <mach/mach_time.h>

#include <algorithm>
#include <set>

namespace vj::shortcuts {
namespace {

std::string nameOf(MIDIObjectRef o) {
  CFStringRef s = nullptr;
  if (MIDIObjectGetStringProperty(o, kMIDIPropertyDisplayName, &s) != noErr || !s) return {};
  char buf[256] = {};
  CFStringGetCString(s, buf, sizeof buf, kCFStringEncodingUTF8);
  CFRelease(s);
  return buf;
}

}  // namespace

namespace {

double toSeconds(uint64_t host) {
  static const mach_timebase_info_data_t info = [] {
    mach_timebase_info_data_t i;
    mach_timebase_info(&i);
    return i;
  }();
  return static_cast<double>(host) * info.numer / info.denom / 1e9;
}

}  // namespace

double hostSeconds() { return toSeconds(mach_absolute_time()); }

struct CoreMidiPort::Impl {
  MIDIClientRef client = 0;
  MIDIPortRef in = 0, out = 0;
  std::mutex mutex;
  std::deque<MidiMessage> queue;
  std::map<MIDIEndpointRef, std::string> connected;
  std::set<std::string> names;

  static void read(const MIDIPacketList* list, void* self, void* src) {
    auto* impl = static_cast<Impl*>(self);
    const std::string device = *static_cast<std::string*>(src);
    const double arrival = hostSeconds();
    const MIDIPacket* p = &list->packet[0];
    std::lock_guard lock(impl->mutex);
    for (UInt32 i = 0; i < list->numPackets; ++i) {
      // CoreMIDI entrega por tandas: la hora buena es la del paquete, no la de llegada.
      const double t = p->timeStamp ? toSeconds(p->timeStamp) : arrival;
      for (UInt16 b = 0; b < p->length;) {
        const uint8_t status = p->data[b];
        if (status >= 0xF8) {  // tiempo real: un solo byte
          impl->queue.push_back({device, status, 0, 0, t});
          ++b;
        } else if (status >= 0x80 && status < 0xF0) {
          const uint8_t kind = status & 0xF0;
          const int len = kind == 0xC0 || kind == 0xD0 ? 2 : 3;
          if (b + len > p->length) break;
          impl->queue.push_back({device, status, p->data[b + 1], len == 3 ? p->data[b + 2] : uint8_t{0}, t});
          b = static_cast<UInt16>(b + len);
        } else {
          ++b;  // SysEx y restos: no se usan
        }
      }
      p = MIDIPacketNext(p);
    }
    while (impl->queue.size() > 4096) impl->queue.pop_front();
  }
};

CoreMidiPort::CoreMidiPort() : impl_(std::make_unique<Impl>()) {
  if (MIDIClientCreate(CFSTR("VJ Studio"), nullptr, nullptr, &impl_->client) != noErr) return;
  if (MIDIInputPortCreate(impl_->client, CFSTR("Entrada"), &Impl::read, impl_.get(), &impl_->in) != noErr) return;
  if (MIDIOutputPortCreate(impl_->client, CFSTR("Salida"), &impl_->out) != noErr) return;
  ok_ = true;
  refresh();
}

CoreMidiPort::~CoreMidiPort() {
  if (impl_->client) MIDIClientDispose(impl_->client);
}

void CoreMidiPort::refresh() {
  if (!ok_) return;
  const ItemCount n = MIDIGetNumberOfSources();
  for (ItemCount i = 0; i < n; ++i) {
    MIDIEndpointRef src = MIDIGetSource(i);
    if (impl_->connected.count(src)) continue;
    auto [it, _] = impl_->connected.emplace(src, nameOf(src));
    MIDIPortConnectSource(impl_->in, src, &it->second);
  }
}

std::vector<std::string> CoreMidiPort::inputs() const {
  std::vector<std::string> out;
  for (const auto& [_, name] : impl_->connected) out.push_back(name);
  return out;
}

std::vector<MidiMessage> CoreMidiPort::take() {
  std::lock_guard lock(impl_->mutex);
  std::vector<MidiMessage> out(impl_->queue.begin(), impl_->queue.end());
  impl_->queue.clear();
  return out;
}

void CoreMidiPort::send(const std::string& device, const std::vector<uint8_t>& bytes) {
  if (!ok_) return;
  Byte buffer[256];
  auto* list = reinterpret_cast<MIDIPacketList*>(buffer);
  MIDIPacket* p = MIDIPacketListInit(list);
  p = MIDIPacketListAdd(list, sizeof buffer, p, 0, bytes.size(), bytes.data());
  if (!p) return;
  const ItemCount n = MIDIGetNumberOfDestinations();
  for (ItemCount i = 0; i < n; ++i) {
    MIDIEndpointRef dst = MIDIGetDestination(i);
    if (device.empty() || nameOf(dst) == device) MIDISend(impl_->out, dst, list);
  }
}

}  // namespace vj::shortcuts
