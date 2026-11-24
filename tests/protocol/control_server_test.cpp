#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "control/ControlServer.h"
#include "control/WsCodec.h"
#include "core/generated/Parameters.gen.h"

using namespace vj;
using namespace vj::control;
using nlohmann::json;

namespace {

class RawClient {
 public:
  explicit RawClient(int port) {
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    connect(fd_, reinterpret_cast<sockaddr*>(&a), sizeof a);
  }
  ~RawClient() { close(fd_); }

  void sendRaw(const std::string& s) { (void)!write(fd_, s.data(), s.size()); }

  std::string http(const std::string& request) {
    sendRaw(request);
    std::string out;
    while (out.find("\r\n\r\n") == std::string::npos && pump()) {}
    return out = take();
  }

  bool upgrade(const std::string& extra = "") {
    const std::string r = http("GET /api/v1 HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                               "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n" +
                               extra + "\r\n");
    return r.find("101") != std::string::npos && r.find("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != std::string::npos;
  }

  void sendMasked(WsOpcode op, const std::string& text) {
    std::string f;
    f.push_back(static_cast<char>(0x80 | static_cast<uint8_t>(op)));
    if (text.size() < 126) {
      f.push_back(static_cast<char>(0x80 | text.size()));
    } else {
      f.push_back(static_cast<char>(0xFE));
      f.push_back(static_cast<char>(text.size() >> 8));
      f.push_back(static_cast<char>(text.size() & 0xFF));
    }
    const char mask[4] = {1, 2, 3, 4};
    f.append(mask, 4);
    for (size_t i = 0; i < text.size(); ++i) f.push_back(text[i] ^ mask[i % 4]);
    sendRaw(f);
  }

  // Devuelve la siguiente trama, o nullopt si se agota el tiempo o se cierra.
  std::optional<WsFrame> next(int timeoutMs = 2000) {
    for (;;) {
      WsFrame f;
      size_t used = 0;
      if (parseFrame(in_, f, used, 1 << 24) == WsParse::Ok) {
        in_.erase(0, used);
        return f;
      }
      if (!pump(timeoutMs)) return std::nullopt;
    }
  }

  json call(const json& j) {
    sendMasked(WsOpcode::Text, j.dump());
    for (;;) {
      auto f = next();
      if (!f) return {};
      auto r = json::parse(f->payload);
      if (r["type"] == "response") return r;
    }
  }

 private:
  bool pump(int timeoutMs = 2000) {
    pollfd p{fd_, POLLIN, 0};
    if (poll(&p, 1, timeoutMs) <= 0) return false;
    char b[65536];
    const ssize_t n = read(fd_, b, sizeof b);
    if (n <= 0) return false;
    in_.append(b, static_cast<size_t>(n));
    return true;
  }
  std::string take() {
    std::string s;
    s.swap(in_);
    return s;
  }
  int fd_;
  std::string in_;
};

class Server : public ::testing::Test {
 protected:
  void SetUp() override {
    params.defineAll(schema::parameters());
    ASSERT_TRUE(server.start("127.0.0.1", 0));
  }
  ParameterRegistry params;
  EventBus events;
  ControlServer server{params, events};
};

}  // namespace

TEST(Origin, OnlyLocalOriginsPass) {
  EXPECT_TRUE(ControlServer::originAllowed(""));
  EXPECT_TRUE(ControlServer::originAllowed("file://"));
  EXPECT_TRUE(ControlServer::originAllowed("http://localhost:5173"));
  EXPECT_TRUE(ControlServer::originAllowed("http://127.0.0.1:5173"));
  EXPECT_FALSE(ControlServer::originAllowed("https://evil.example"));
  EXPECT_FALSE(ControlServer::originAllowed("http://localhost.evil.example"));
  EXPECT_FALSE(ControlServer::originAllowed("http://127.0.0.1.evil.example:80"));
}

TEST_F(Server, Health) {
  RawClient c(server.port());
  const auto r = c.http("GET /api/v1/health HTTP/1.1\r\nHost: localhost\r\n\r\n");
  EXPECT_NE(r.find("200 OK"), std::string::npos);
  EXPECT_NE(r.find("vjstudio"), std::string::npos);
}

TEST_F(Server, UnknownRoute404) {
  RawClient c(server.port());
  EXPECT_NE(c.http("GET /x HTTP/1.1\r\nHost: localhost\r\n\r\n").find("404"), std::string::npos);
}

TEST_F(Server, ForeignOriginRejected) {
  RawClient c(server.port());
  EXPECT_FALSE(c.upgrade("Origin: https://evil.example\r\n"));
}

TEST_F(Server, EveryAction) {
  RawClient c(server.port());
  ASSERT_TRUE(c.upgrade());
  EXPECT_TRUE(c.call({{"action", "list"}})["ok"]);
  EXPECT_EQ(c.call({{"action", "get"}, {"parameter", "/output/opacity"}})["value"], 1.0);
  EXPECT_EQ(c.call({{"action", "set"}, {"parameter", "/output/opacity"}, {"value", 0.5}})["value"], 0.5);
  EXPECT_DOUBLE_EQ(params.getFloat("/output/opacity"), 0.5);
  EXPECT_EQ(c.call({{"action", "reset"}, {"parameter", "/output/opacity"}})["value"], 1.0);
  EXPECT_TRUE(c.call({{"action", "trigger"}, {"parameter", "/composition/eject"}})["ok"]);
  EXPECT_TRUE(c.call({{"action", "subscribe"}, {"parameter", "/output/opacity"}})["ok"]);
  EXPECT_TRUE(c.call({{"action", "unsubscribe"}, {"parameter", "/output/opacity"}})["ok"]);
}

TEST_F(Server, SubscribersReceivePushedUpdatesOthersDoNot) {
  RawClient sub(server.port()), other(server.port());
  ASSERT_TRUE(sub.upgrade());
  ASSERT_TRUE(other.upgrade());
  ASSERT_TRUE(sub.call({{"action", "subscribe"}, {"parameter", "/output/opacity"}})["ok"]);
  params.set("/output/opacity", 0.3);
  auto f = sub.next();
  ASSERT_TRUE(f);
  auto j = json::parse(f->payload);
  EXPECT_EQ(j["type"], "update");
  EXPECT_EQ(j["parameter"], "/output/opacity");
  EXPECT_EQ(j["value"], 0.3);
  EXPECT_FALSE(other.next(100));

  // Quien pide el cambio también recibe el aviso si está suscrito.
  auto r = sub.call({{"action", "set"}, {"parameter", "/output/opacity"}, {"value", 0.4}});
  EXPECT_TRUE(r["ok"]);
}

TEST_F(Server, EventsReachConnectedClients) {
  RawClient c(server.port());
  ASSERT_TRUE(c.upgrade());
  events.publish("media.loaded", {{"path", "a"}});
  auto f = c.next();
  ASSERT_TRUE(f);
  auto j = json::parse(f->payload);
  EXPECT_EQ(j["type"], "event");
  EXPECT_EQ(j["topic"], "media.loaded");
}

TEST_F(Server, PingPong) {
  RawClient c(server.port());
  ASSERT_TRUE(c.upgrade());
  c.sendMasked(WsOpcode::Ping, "hi");
  auto f = c.next();
  ASSERT_TRUE(f);
  EXPECT_EQ(f->opcode, WsOpcode::Pong);
  EXPECT_EQ(f->payload, "hi");
}

TEST_F(Server, UnmaskedFrameClosesConnection) {
  RawClient c(server.port());
  ASSERT_TRUE(c.upgrade());
  c.sendRaw(encodeFrame(WsOpcode::Text, "{}"));
  auto f = c.next();
  ASSERT_TRUE(f);
  EXPECT_EQ(f->opcode, WsOpcode::Close);
}

TEST_F(Server, MalformedJsonGetsErrorAndConnectionSurvives) {
  RawClient c(server.port());
  ASSERT_TRUE(c.upgrade());
  c.sendMasked(WsOpcode::Text, "{no");
  auto f = c.next();
  ASSERT_TRUE(f);
  EXPECT_FALSE(json::parse(f->payload)["ok"]);
  EXPECT_TRUE(c.call({{"action", "list"}})["ok"]);
}

TEST_F(Server, ClientDisconnectDoesNotDisturbOthers) {
  RawClient keep(server.port());
  ASSERT_TRUE(keep.upgrade());
  {
    RawClient gone(server.port());
    ASSERT_TRUE(gone.upgrade());
    ASSERT_TRUE(gone.call({{"action", "subscribe"}, {"parameter", "*"}})["ok"]);
  }
  for (int i = 0; i < 50; ++i) params.set("/output/opacity", i / 100.0);
  EXPECT_TRUE(keep.call({{"action", "list"}})["ok"]);
}

TEST_F(Server, MonitorFramesOnlyToThoseWhoAsked) {
  RawClient want(server.port()), other(server.port());
  ASSERT_TRUE(want.upgrade());
  ASSERT_TRUE(other.upgrade());
  EXPECT_FALSE(server.monitorWanted());
  ASSERT_TRUE(want.call({{"action", "monitor"}, {"value", true}})["ok"]);
  EXPECT_TRUE(server.monitorWanted());
  server.sendMonitor(1, 2, 1, {1, 2, 3, 4, 5, 6, 7, 8});
  auto f = want.next();
  ASSERT_TRUE(f);
  EXPECT_EQ(f->opcode, WsOpcode::Binary);
  ASSERT_EQ(f->payload.size(), 13u);
  EXPECT_EQ(f->payload[0], 1);
  EXPECT_EQ(f->payload[2], 2);
  EXPECT_EQ(f->payload[4], 1);
  EXPECT_FALSE(other.next(100));
}

TEST_F(Server, CommandAndHistoryHook) {
  CommandBus bus;
  bus.add("eco", [](const json& a) { return a; });
  ParameterRegistry p2;
  p2.defineAll(schema::parameters());
  EventBus e2;
  ControlServer s2(p2, e2, &bus);
  int hooked = 0;
  s2.api().setHistoryHook([&](const std::string&, const Value&, const Value&) { ++hooked; });
  ASSERT_TRUE(s2.start("127.0.0.1", 0));
  std::atomic<bool> done{false};
  std::thread pump([&] { while (!done) { bus.pump(); usleep(1000); } });
  RawClient c(s2.port());
  ASSERT_TRUE(c.upgrade());
  auto r = c.call({{"action", "command"}, {"name", "eco"}, {"args", {{"a", 1}}}});
  EXPECT_EQ(r["result"]["a"], 1);
  EXPECT_FALSE(c.call({{"action", "command"}, {"name", "nada"}})["ok"]);
  c.call({{"action", "set"}, {"parameter", "/composition/master"}, {"value", 0.5}});
  EXPECT_EQ(hooked, 0);
  c.call({{"action", "set"}, {"parameter", "/composition/master"}, {"value", 0.4}, {"history", true}});
  EXPECT_EQ(hooked, 1);
  done = true;
  pump.join();
}

TEST_F(Server, PortInUseFails) {
  ControlServer second(params, events);
  EXPECT_FALSE(second.start("127.0.0.1", server.port()));
}
