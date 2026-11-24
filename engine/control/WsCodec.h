#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace vj::control {

std::string sha1(const std::string& data);
std::string base64(const std::string& data);
// Valor de Sec-WebSocket-Accept para una clave de cliente (RFC 6455, 4.2.2).
std::string websocketAccept(const std::string& key);

enum class WsOpcode : uint8_t { Continuation = 0, Text = 1, Binary = 2, Close = 8, Ping = 9, Pong = 10 };

struct WsFrame {
  bool fin = true;
  WsOpcode opcode = WsOpcode::Text;
  bool masked = false;
  std::string payload;
};

enum class WsParse { NeedMore, Ok, TooLarge };

// Lee una trama de buffer desde offset. En Ok, consumed indica los bytes usados.
WsParse parseFrame(const std::string& buffer, WsFrame& out, size_t& consumed, size_t maxPayload);
// Trama de servidor (sin máscara).
std::string encodeFrame(WsOpcode opcode, const std::string& payload);

}  // namespace vj::control
