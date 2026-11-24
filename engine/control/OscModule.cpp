#include "control/OscModule.h"

#include "core/Net.h"
#if defined(__APPLE__)
#include <dns_sd.h>
#endif

#include <algorithm>
#include <sstream>

#include "control/OscRouter.h"
#include "core/EventBus.h"
#include "core/ValueJson.h"

using nlohmann::json;

namespace vj::control {
namespace {

constexpr size_t kLogLines = 200;  // como el monitor del manual
constexpr size_t kMaxPending = 2000;

std::string describe(const OscMessage& m) {
  std::stringstream s;
  s << m.address;
  for (const auto& a : m.args) {
    if (auto* i = std::get_if<int32_t>(&a)) s << ' ' << *i;
    else if (auto* f = std::get_if<float>(&a)) s << ' ' << *f;
    else if (auto* t = std::get_if<std::string>(&a)) s << " \"" << *t << '"';
    else s << " color";
  }
  return s.str();
}

}  // namespace

bool OscModule::start(EngineContext& ctx) {
  ctx_ = &ctx;
  // Lo escribe cualquier hilo: aquí solo se encola, tick() envía.
  listener_ = ctx.params.addListener("", [this](const ParamDesc& d, const Value& v) {
    if (d.type == ParamType::Trigger || d.path.rfind("/composition/", 0) != 0) return;
    if (!ctx_->params.getBool("/preferences/osc/output")) return;
    std::lock_guard lock(mutex_);
    if (ctx_->params.getString("/preferences/osc/preset") != "Output All" &&
        std::find(allowed_.begin(), allowed_.end(), d.path) == allowed_.end()) return;
    if (pending_.size() < kMaxPending) pending_.push_back({parameterToOsc(d.path), {toOscValue(d, v)}});
  });
  if (ctx.commands) {
    ctx.commands->add("osc.log", [this](const json&) {
      std::lock_guard lock(mutex_);
      return json(std::vector<std::string>(log_.begin(), log_.end()));
    });
    ctx.commands->add("osc.address", [this](const json& a) {
      return json{{"address", parameterToOsc(a.at("path"))}, {"port", ctx_->params.getInt("/preferences/osc/inport")}};
    });
  }
  return true;
}

void OscModule::setOutputAllowed(std::vector<std::string> paths) {
  std::lock_guard lock(mutex_);
  allowed_ = std::move(paths);
}

void OscModule::log(const std::string& direction, const std::string& text) {
  std::lock_guard lock(mutex_);
  log_.push_back(direction + " " + text);
  if (log_.size() > kLogLines) log_.pop_front();
}

void OscModule::closeInput() {
#if defined(__APPLE__)
  if (zeroconf_) {
    DNSServiceRefDeallocate(zeroconf_);
    zeroconf_ = nullptr;
  }
#endif
  if (!running_.exchange(false)) return;
  thread_.join();
  net::closeSocket(fd_);
  fd_ = -1;
  boundPort_ = 0;
}

void OscModule::reconfigure() {
  auto& p = ctx_->params;
  std::stringstream key;
  key << p.getBool("/preferences/osc/input") << p.getInt("/preferences/osc/inport") << p.getBool("/preferences/osc/remote") << p.getBool("/preferences/osc/zeroconf");
  if (key.str() == applied_) return;
  applied_ = key.str();
  closeInput();
  if (!p.getBool("/preferences/osc/input")) {
    p.publish("/preferences/osc/status", "apagado");
    return;
  }
  const int port = static_cast<int>(p.getInt("/preferences/osc/inport"));
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(static_cast<uint16_t>(port));
  // Solo local salvo que se pida lo contrario: OSC acepta órdenes de cualquiera que llegue.
  a.sin_addr.s_addr = htonl(p.getBool("/preferences/osc/remote") ? INADDR_ANY : INADDR_LOOPBACK);
  if (fd < 0 || bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
    if (fd >= 0) net::closeSocket(fd);
    p.publish("/preferences/osc/status", "puerto ocupado");
    return;
  }
  fd_ = fd;
  boundPort_ = port;
  running_ = true;
  thread_ = std::thread([this, fd] { run(fd); });
  p.publish("/preferences/osc/status", "escuchando en " + std::to_string(port));
#if defined(__APPLE__)
  if (p.getBool("/preferences/osc/zeroconf") && p.getBool("/preferences/osc/remote"))
    DNSServiceRegister(&zeroconf_, 0, 0, "VJ Studio", "_osc._udp", nullptr, nullptr, htons(static_cast<uint16_t>(port)), 0, nullptr, nullptr, nullptr);
#endif
}

void OscModule::run(int fd) {
  uint8_t buf[65536];
  while (running_) {
    pollfd pf = net::pollEntry(fd, POLLIN);
    if (poll(&pf, 1, 200) <= 0) continue;
    sockaddr_in from{};
    socklen_t len = sizeof from;
    const ssize_t n = recvfrom(fd, reinterpret_cast<char*>(buf), sizeof buf, 0, reinterpret_cast<sockaddr*>(&from), &len);
    if (n <= 0) continue;
    for (const auto& m : parseOsc(buf, static_cast<size_t>(n))) {
      log("<", describe(m));
      std::string error;
      if (auto reply = applyOsc(m, ctx_->params, &error)) {
        // La consulta se responde al que pregunta.
        const std::string out = encodeOsc(*reply);
        sendto(fd, reinterpret_cast<const char*>(out.data()), static_cast<int>(out.size()), 0, reinterpret_cast<sockaddr*>(&from), len);
        log(">", describe(*reply));
      } else if (!error.empty()) {
        // Una dirección que no es del programa puede tener un atajo asignado.
        double value = 1;
        if (!m.args.empty()) {
          if (auto* f = std::get_if<float>(&m.args[0])) value = *f;
          if (auto* i = std::get_if<int32_t>(&m.args[0])) value = *i;
        }
        ctx_->events.publish("osc.unmapped", {{"address", m.address}, {"value", value}});
        log("!", m.address + ": " + error);
      }
    }
  }
}

void OscModule::flush() {
  std::vector<OscMessage> work;
  {
    std::lock_guard lock(mutex_);
    work.swap(pending_);
  }
  if (work.empty()) return;
  auto& p = ctx_->params;
  if (outFd_ < 0) {
    outFd_ = socket(AF_INET, SOCK_DGRAM, 0);
    const int yes = 1;
    net::setOption(outFd_, SOL_SOCKET, SO_BROADCAST, yes);
  }
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_port = htons(static_cast<uint16_t>(p.getInt("/preferences/osc/outport")));
  if (inet_pton(AF_INET, p.getString("/preferences/osc/outhost").c_str(), &to.sin_addr) != 1) return;
  const auto send = [&](const std::string& data) { sendto(outFd_, data.data(), static_cast<int>(data.size()), 0, reinterpret_cast<sockaddr*>(&to), sizeof to); };
  if (p.getBool("/preferences/osc/bundles")) {
    send(encodeBundle(work));
  } else {
    for (const auto& m : work) send(encodeOsc(m));
  }
  log(">", describe(work.front()) + (work.size() > 1 ? " (+" + std::to_string(work.size() - 1) + ")" : ""));
}

void OscModule::tick(double) {
  reconfigure();
  flush();
}

void OscModule::stop() {
  ctx_->params.removeListener(listener_);
  closeInput();
  if (outFd_ >= 0) net::closeSocket(outFd_);
  outFd_ = -1;
}

}  // namespace vj::control
