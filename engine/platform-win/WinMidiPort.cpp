// MIDI con WinMM, tras la misma clase que CoreMidiPort.
#include "platform-win/WinCommon.h"

#include <mmeapi.h>

#include <algorithm>
#include <chrono>
#include <map>

#include "shortcuts/CoreMidiPort.h"

namespace vj::shortcuts {
namespace {
std::string deviceName(const wchar_t* name) { return win::utf8(name); }
}  // namespace

double hostSeconds() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct CoreMidiPort::Impl {
  struct In {
    HMIDIIN handle = nullptr;
    std::string name;
    Impl* owner = nullptr;
  };
  std::mutex mutex;
  std::vector<MidiMessage> queue;
  std::vector<std::unique_ptr<In>> inputs;
  std::map<std::string, HMIDIOUT> outputs;

  static void CALLBACK onInput(HMIDIIN, UINT msg, DWORD_PTR user, DWORD_PTR data, DWORD_PTR) {
    if (msg != MIM_DATA) return;
    auto* in = reinterpret_cast<In*>(user);
    MidiMessage m;
    m.device = in->name;
    m.status = static_cast<uint8_t>(data & 0xFF);
    m.data1 = static_cast<uint8_t>((data >> 8) & 0xFF);
    m.data2 = static_cast<uint8_t>((data >> 16) & 0xFF);
    m.time = hostSeconds();
    std::lock_guard lock(in->owner->mutex);
    in->owner->queue.push_back(std::move(m));
  }
  void closeAll() {
    for (auto& in : inputs) {
      midiInStop(in->handle);
      midiInReset(in->handle);
      midiInClose(in->handle);
    }
    inputs.clear();
    for (auto& [_, out] : outputs) midiOutClose(out);
    outputs.clear();
  }
};

CoreMidiPort::CoreMidiPort() : impl_(std::make_unique<Impl>()) {
  ok_ = true;
  refresh();
}
CoreMidiPort::~CoreMidiPort() { impl_->closeAll(); }

void CoreMidiPort::refresh() {
  impl_->closeAll();
  for (UINT i = 0; i < midiInGetNumDevs(); ++i) {
    MIDIINCAPSW caps{};
    if (midiInGetDevCapsW(i, &caps, sizeof caps) != MMSYSERR_NOERROR) continue;
    auto in = std::make_unique<Impl::In>();
    in->name = deviceName(caps.szPname);
    in->owner = impl_.get();
    if (midiInOpen(&in->handle, i, reinterpret_cast<DWORD_PTR>(&Impl::onInput), reinterpret_cast<DWORD_PTR>(in.get()), CALLBACK_FUNCTION) != MMSYSERR_NOERROR) continue;
    midiInStart(in->handle);
    impl_->inputs.push_back(std::move(in));
  }
  for (UINT i = 0; i < midiOutGetNumDevs(); ++i) {
    MIDIOUTCAPSW caps{};
    HMIDIOUT out = nullptr;
    if (midiOutGetDevCapsW(i, &caps, sizeof caps) != MMSYSERR_NOERROR) continue;
    if (midiOutOpen(&out, i, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR) impl_->outputs[deviceName(caps.szPname)] = out;
  }
}

std::vector<MidiMessage> CoreMidiPort::take() {
  std::lock_guard lock(impl_->mutex);
  std::vector<MidiMessage> out;
  out.swap(impl_->queue);
  return out;
}

std::vector<std::string> CoreMidiPort::inputs() const {
  std::vector<std::string> out;
  for (const auto& in : impl_->inputs) out.push_back(in->name);
  return out;
}

void CoreMidiPort::send(const std::string& device, const std::vector<uint8_t>& bytes) {
  // Mensajes cortos de hasta 3 bytes: lo que emite el programa (notas, CC y reloj).
  DWORD packed = 0;
  for (size_t i = 0; i < std::min<size_t>(bytes.size(), 3); ++i) packed |= static_cast<DWORD>(bytes[i]) << (8 * i);
  for (auto& [name, out] : impl_->outputs)
    if (device.empty() || device == name) midiOutShortMsg(out, packed);
}

}  // namespace vj::shortcuts
