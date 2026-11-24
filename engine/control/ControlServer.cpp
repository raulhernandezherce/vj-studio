#include "control/ControlServer.h"

#include "core/Net.h"

#include <algorithm>
#include <cctype>
#include <csignal>
#include <vector>

#include "control/RestApi.h"
#include "control/WsCodec.h"

using nlohmann::json;

namespace vj::control {
namespace {

constexpr size_t kMaxFrame = 1 << 20;
constexpr size_t kMaxHeader = 16 * 1024;
constexpr size_t kMaxQueue = 8 << 20;
constexpr size_t kMaxBody = 4 << 20;

std::string headerValue(const std::string& head, std::string name) {
  std::string lower = head;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
  std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
  const auto at = lower.find("\r\n" + name + ":");
  if (at == std::string::npos) return {};
  size_t start = at + name.size() + 3;
  const size_t end = head.find("\r\n", start);
  while (start < end && head[start] == ' ') ++start;
  return head.substr(start, end - start);
}

std::string httpResponse(const std::string& status, const std::string& body) {
  return "HTTP/1.1 " + status + "\r\nContent-Type: application/json\r\nContent-Length: " +
         std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
}

using net::setNonBlocking;

}  // namespace

bool ControlServer::originAllowed(const std::string& origin) {
  if (origin.empty() || origin == "file://") return true;
  for (const std::string prefix : {"http://localhost", "http://127.0.0.1"}) {
    if (origin.rfind(prefix, 0) == 0) {
      const char next = origin.size() > prefix.size() ? origin[prefix.size()] : '\0';
      if (next == '\0' || next == ':') return true;
    }
  }
  return false;
}

bool ControlServer::hostAllowed(const std::string& host) {
  const std::string name = host.substr(0, host.rfind(':'));
  return host.empty() || name == "localhost" || name == "127.0.0.1" || name == "[::1]" || host == "localhost" || host == "127.0.0.1";
}

bool ControlServer::start(const std::string& host, int port) {
#if !defined(_WIN32)
  std::signal(SIGPIPE, SIG_IGN);
#endif
  localOnly_ = host == "127.0.0.1";
  listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (listenFd_ < 0) return false;
  int yes = 1;
  net::setOption(listenFd_, SOL_SOCKET, SO_REUSEADDR, yes);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));
  if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1 ||
      bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || listen(listenFd_, 16) != 0) {
    net::closeSocket(listenFd_);
    listenFd_ = -1;
    return false;
  }
  socklen_t len = sizeof addr;
  getsockname(listenFd_, reinterpret_cast<sockaddr*>(&addr), &len);
  port_ = ntohs(addr.sin_port);
  setNonBlocking(listenFd_);
  if (!net::wakePair(wakeFds_)) return false;

  listener_ = params_.addListener("", [this](const ParamDesc& d, const Value& v) {
    json update = {{"type", d.type == ParamType::Trigger ? "trigger" : "update"}, {"parameter", d.path}};
    if (d.type != ParamType::Trigger) update["value"] = toJson(v);
    broadcast(d.path, encodeFrame(WsOpcode::Text, update.dump(-1, ' ', false, json::error_handler_t::replace)));
  });
  eventHandler_ = events_.subscribe("", [this](const std::string& topic, const json& data) {
    const json e = {{"type", "event"}, {"topic", topic}, {"data", data}};
    broadcastAll(encodeFrame(WsOpcode::Text, e.dump(-1, ' ', false, json::error_handler_t::replace)));
  });
  running_ = true;
  thread_ = std::thread([this] { run(); });
  return true;
}

void ControlServer::stop() {
  if (!running_.exchange(false)) return;
  params_.removeListener(listener_);
  events_.unsubscribe(eventHandler_);
  wake();
  thread_.join();
  for (auto& [fd, _] : clients_) net::closeSocket(fd);
  clients_.clear();
  net::closeSocket(listenFd_);
  net::closeSocket(wakeFds_[0]);
  net::closeSocket(wakeFds_[1]);
}

void ControlServer::wake() {
  net::wakeSignal(wakeFds_[1]);
}

void ControlServer::send(Client& c, const std::string& bytes) {
  // Un cliente que no lee no puede hacer crecer la memoria del motor.
  if (c.out.size() + bytes.size() > kMaxQueue) c.closing = true;
  else c.out += bytes;
}

void ControlServer::broadcast(const std::string& path, const std::string& frame) {
  bool any = false;
  {
    std::lock_guard lock(mutex_);
    for (auto& [_, c] : clients_) {
      if (c->upgraded && c->subs.matches(path)) {
        send(*c, frame);
        any = true;
      }
    }
  }
  if (any) wake();
}

void ControlServer::broadcastAll(const std::string& frame) {
  {
    std::lock_guard lock(mutex_);
    for (auto& [_, c] : clients_)
      if (c->upgraded) send(*c, frame);
  }
  wake();
}

bool ControlServer::monitorWanted() {
  std::lock_guard lock(mutex_);
  for (auto& [_, c] : clients_)
    if (c->upgraded && c->subs.monitor) return true;
  return false;
}

void ControlServer::sendMonitor(int monitor, int width, int height, const std::vector<uint8_t>& rgba) {
  std::string payload(5, '\0');
  payload[0] = static_cast<char>(monitor);
  payload[1] = static_cast<char>(width >> 8);
  payload[2] = static_cast<char>(width & 0xFF);
  payload[3] = static_cast<char>(height >> 8);
  payload[4] = static_cast<char>(height & 0xFF);
  payload.append(reinterpret_cast<const char*>(rgba.data()), rgba.size());
  const std::string frame = encodeFrame(WsOpcode::Binary, payload);
  {
    std::lock_guard lock(mutex_);
    for (auto& [_, c] : clients_)
      // Un monitor lento salta fotogramas: nunca retrasa ni llena la cola del cliente.
      if (c->upgraded && c->subs.monitor && c->out.size() < frame.size() * 2) send(*c, frame);
  }
  wake();
}

void ControlServer::run() {
  while (running_) {
    std::vector<pollfd> fds = {net::pollEntry(listenFd_, POLLIN), net::pollEntry(wakeFds_[0], POLLIN)};
    {
      std::lock_guard lock(mutex_);
      for (auto& [fd, c] : clients_)
        fds.push_back(net::pollEntry(fd, static_cast<short>(POLLIN | (c->out.empty() ? 0 : POLLOUT))));
    }
    if (poll(fds.data(), fds.size(), 500) < 0) continue;
    if (fds[1].revents & POLLIN) {
      net::wakeDrain(wakeFds_[0]);
    }
    if (fds[0].revents & POLLIN) acceptClient();
    for (size_t i = 2; i < fds.size(); ++i) {
      Client* c;
      {
        std::lock_guard lock(mutex_);
        auto it = clients_.find(fds[i].fd);
        if (it == clients_.end()) continue;
        c = it->second.get();
      }
      if (fds[i].revents & (POLLIN | POLLHUP | POLLERR)) readClient(*c);
      std::lock_guard lock(mutex_);
      while (!c->out.empty()) {
        const ssize_t n = ::send(c->fd, c->out.data(), static_cast<int>(c->out.size()), 0);
        if (n > 0) c->out.erase(0, static_cast<size_t>(n));
        else break;
      }
      if (c->closing && (c->out.empty() || c->out.size() > kMaxQueue / 2)) {
        net::closeSocket(c->fd);
        clients_.erase(c->fd);
      }
    }
  }
}

void ControlServer::acceptClient() {
  for (;;) {
    const int fd = accept(listenFd_, nullptr, nullptr);
    if (fd < 0) return;
    setNonBlocking(fd);
    int yes = 1;
    net::setOption(fd, IPPROTO_TCP, TCP_NODELAY, yes);
#ifdef SO_NOSIGPIPE
    net::setOption(fd, SOL_SOCKET, SO_NOSIGPIPE, yes);
#endif
    auto c = std::make_unique<Client>();
    c->fd = fd;
    std::lock_guard lock(mutex_);
    clients_[fd] = std::move(c);
  }
}

void ControlServer::readClient(Client& c) {
  char buf[8192];
  for (;;) {
    const ssize_t n = recv(c.fd, buf, sizeof buf, 0);
    if (n == 0) {
      std::lock_guard lock(mutex_);
      c.closing = true;
      c.out.clear();
      return;
    }
    if (n < 0) break;
    c.in.append(buf, static_cast<size_t>(n));
  }
  if (!c.upgraded) handleHttp(c);
  if (c.upgraded) handleFrames(c);
}

void ControlServer::handleHttp(Client& c) {
  const auto end = c.in.find("\r\n\r\n");
  if (end == std::string::npos) {
    if (c.in.size() > kMaxHeader) {
      std::lock_guard lock(mutex_);
      c.closing = true;
    }
    return;
  }
  const std::string head = c.in.substr(0, end + 2);
  const std::string length = headerValue(head, "Content-Length");
  const size_t bodySize = length.empty() ? 0 : static_cast<size_t>(std::strtoull(length.c_str(), nullptr, 10));
  if (bodySize > kMaxBody) {
    std::lock_guard lock(mutex_);
    send(c, httpResponse("413 Payload Too Large", R"({"error":"demasiado grande"})"));
    c.closing = true;
    return;
  }
  if (c.in.size() < end + 4 + bodySize) return;  // falta parte del cuerpo
  const std::string body = c.in.substr(end + 4, bodySize);
  c.in.erase(0, end + 4 + bodySize);
  const std::string line = head.substr(0, head.find("\r\n"));
  const std::string key = headerValue(head, "Sec-WebSocket-Key");
  const size_t space = line.find(' '), space2 = line.find(' ', space + 1);
  const std::string method = line.substr(0, space), target = line.substr(space + 1, space2 - space - 1);

  if (!originAllowed(headerValue(head, "Origin")) || (localOnly_ && !hostAllowed(headerValue(head, "Host")))) {
    std::lock_guard lock(mutex_);
    send(c, httpResponse("403 Forbidden", R"({"error":"origen no permitido"})"));
    c.closing = true;
    return;
  }
  if (target == "/api/v1" && method == "GET" && !key.empty()) {
    std::lock_guard lock(mutex_);
    send(c, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
            "Sec-WebSocket-Accept: " + websocketAccept(key) + "\r\n\r\n");
    c.upgraded = true;
    return;
  }
  if (target == "/api/v1/health" && method == "GET") {
    const json j = {{"app", "vjstudio"}, {"version", params_.getString("/engine/version")}};
    std::lock_guard lock(mutex_);
    send(c, httpResponse("200 OK", j.dump()));
    c.closing = true;
    return;
  }
  // Puede llamar a un comando y esperar al hilo principal: sin bloqueo tomado.
  const HttpResponse r = handleRest(method, target, body, params_, commands_);
  static const std::map<int, std::string> reasons = {{200, "OK"}, {400, "Bad Request"}, {404, "Not Found"}, {405, "Method Not Allowed"}, {409, "Conflict"}, {503, "Service Unavailable"}};
  const auto reason = reasons.find(r.status);
  std::lock_guard lock(mutex_);
  send(c, "HTTP/1.1 " + std::to_string(r.status) + " " + (reason != reasons.end() ? reason->second : "Error") + "\r\nContent-Type: " + r.contentType +
              "\r\nContent-Length: " + std::to_string(r.body.size()) + "\r\nConnection: close\r\n\r\n" + r.body);
  c.closing = true;
}

void ControlServer::handleFrames(Client& c) {
  for (;;) {
    WsFrame f;
    size_t used = 0;
    const WsParse r = parseFrame(c.in, f, used, kMaxFrame);
    if (r == WsParse::NeedMore) return;
    const bool bad = r == WsParse::TooLarge || !f.masked || !f.fin;
    if (bad) {
      std::lock_guard lock(mutex_);
      send(c, encodeFrame(WsOpcode::Close, std::string("\x03\xEA", 2)));
      c.closing = true;
      return;
    }
    c.in.erase(0, used);
    if (f.opcode == WsOpcode::Text) {
      Subscriptions subs;
      {
        std::lock_guard lock(mutex_);
        subs = c.subs;
      }
      // Sin bloqueo: un set dispara oyentes que vuelven a entrar en broadcast().
      const std::string reply = api_.handleText(f.payload, subs);
      std::lock_guard lock(mutex_);
      c.subs = std::move(subs);
      send(c, encodeFrame(WsOpcode::Text, reply));
    } else if (f.opcode == WsOpcode::Ping) {
      std::lock_guard lock(mutex_);
      send(c, encodeFrame(WsOpcode::Pong, f.payload));
    } else if (f.opcode == WsOpcode::Close) {
      std::lock_guard lock(mutex_);
      send(c, encodeFrame(WsOpcode::Close, f.payload));
      c.closing = true;
      return;
    }
  }
}

}  // namespace vj::control
