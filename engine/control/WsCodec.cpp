#include "control/WsCodec.h"

#include <array>
#include <cstring>

namespace vj::control {

std::string sha1(const std::string& data) {
  std::array<uint32_t, 5> h = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  std::string msg = data;
  const uint64_t bits = static_cast<uint64_t>(data.size()) * 8;
  msg.push_back(static_cast<char>(0x80));
  while (msg.size() % 64 != 56) msg.push_back(0);
  for (int i = 7; i >= 0; --i) msg.push_back(static_cast<char>((bits >> (i * 8)) & 0xFF));
  const auto rol = [](uint32_t v, int n) { return (v << n) | (v >> (32 - n)); };
  for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
      const auto* p = reinterpret_cast<const unsigned char*>(msg.data() + chunk + i * 4);
      w[i] = (uint32_t{p[0]} << 24) | (uint32_t{p[1]} << 16) | (uint32_t{p[2]} << 8) | p[3];
    }
    for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; ++i) {
      uint32_t f, k;
      if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
      else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
      else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
      else { f = b ^ c ^ d; k = 0xCA62C1D6; }
      const uint32_t t = rol(a, 5) + f + e + k + w[i];
      e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
  }
  std::string out;
  for (uint32_t v : h)
    for (int i = 3; i >= 0; --i) out.push_back(static_cast<char>((v >> (i * 8)) & 0xFF));
  return out;
}

std::string base64(const std::string& data) {
  static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  size_t i = 0;
  const auto b = [&](size_t k) { return static_cast<unsigned char>(data[k]); };
  for (; i + 2 < data.size(); i += 3) {
    const uint32_t n = (b(i) << 16) | (b(i + 1) << 8) | b(i + 2);
    out += {t[n >> 18], t[(n >> 12) & 63], t[(n >> 6) & 63], t[n & 63]};
  }
  if (i + 1 == data.size()) {
    const uint32_t n = b(i) << 16;
    out += {t[n >> 18], t[(n >> 12) & 63], '=', '='};
  } else if (i + 2 == data.size()) {
    const uint32_t n = (b(i) << 16) | (b(i + 1) << 8);
    out += {t[n >> 18], t[(n >> 12) & 63], t[(n >> 6) & 63], '='};
  }
  return out;
}

std::string websocketAccept(const std::string& key) {
  return base64(sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"));
}

WsParse parseFrame(const std::string& buf, WsFrame& out, size_t& consumed, size_t maxPayload) {
  if (buf.size() < 2) return WsParse::NeedMore;
  const auto byte = [&](size_t i) { return static_cast<uint8_t>(buf[i]); };
  out.fin = byte(0) & 0x80;
  out.opcode = static_cast<WsOpcode>(byte(0) & 0x0F);
  out.masked = byte(1) & 0x80;
  uint64_t len = byte(1) & 0x7F;
  size_t pos = 2;
  if (len == 126) {
    if (buf.size() < 4) return WsParse::NeedMore;
    len = (uint64_t{byte(2)} << 8) | byte(3);
    pos = 4;
  } else if (len == 127) {
    if (buf.size() < 10) return WsParse::NeedMore;
    len = 0;
    for (int i = 0; i < 8; ++i) len = (len << 8) | byte(2 + i);
    pos = 10;
  }
  if (len > maxPayload) return WsParse::TooLarge;
  const size_t maskLen = out.masked ? 4 : 0;
  if (buf.size() < pos + maskLen + len) return WsParse::NeedMore;
  out.payload.assign(buf, pos + maskLen, static_cast<size_t>(len));
  if (out.masked)
    for (size_t i = 0; i < out.payload.size(); ++i) out.payload[i] ^= buf[pos + (i % 4)];
  consumed = pos + maskLen + static_cast<size_t>(len);
  return WsParse::Ok;
}

std::string encodeFrame(WsOpcode opcode, const std::string& payload) {
  std::string out;
  out.push_back(static_cast<char>(0x80 | static_cast<uint8_t>(opcode)));
  const uint64_t n = payload.size();
  if (n < 126) {
    out.push_back(static_cast<char>(n));
  } else if (n <= 0xFFFF) {
    out.push_back(126);
    out.push_back(static_cast<char>(n >> 8));
    out.push_back(static_cast<char>(n & 0xFF));
  } else {
    out.push_back(127);
    for (int i = 7; i >= 0; --i) out.push_back(static_cast<char>((n >> (i * 8)) & 0xFF));
  }
  out += payload;
  return out;
}

}  // namespace vj::control
