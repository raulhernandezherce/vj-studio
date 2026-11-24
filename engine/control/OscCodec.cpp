#include "control/OscCodec.h"

#include <cstring>

namespace vj::control {
namespace {

constexpr int kMaxDepth = 8;

uint32_t be32(const uint8_t* p) { return (uint32_t{p[0]} << 24) | (uint32_t{p[1]} << 16) | (uint32_t{p[2]} << 8) | p[3]; }

bool readString(const uint8_t* d, size_t size, size_t& pos, std::string& out) {
  size_t end = pos;
  while (end < size && d[end]) ++end;
  if (end >= size) return false;
  out.assign(reinterpret_cast<const char*>(d + pos), end - pos);
  pos = (end + 4) & ~size_t{3};
  return pos <= size;
}

void parseInto(const uint8_t* d, size_t size, std::vector<OscMessage>& out, int depth) {
  if (size < 4 || depth > kMaxDepth) return;
  if (size >= 16 && std::memcmp(d, "#bundle", 8) == 0) {
    size_t pos = 16;
    while (pos + 4 <= size) {
      const size_t len = be32(d + pos);
      pos += 4;
      if (len > size - pos) return;
      parseInto(d + pos, len, out, depth + 1);
      pos += len;
    }
    return;
  }
  size_t pos = 0;
  OscMessage m;
  if (!readString(d, size, pos, m.address) || m.address.empty() || m.address[0] != '/') return;
  std::string tags;
  if (pos < size && !readString(d, size, pos, tags)) return;
  for (size_t i = 1; i < tags.size(); ++i) {
    switch (tags[i]) {
      case 'i':
      case 'f':
      case 'r':
        if (pos + 4 > size) return;
        if (tags[i] == 'i') m.args.emplace_back(static_cast<int32_t>(be32(d + pos)));
        else if (tags[i] == 'r') m.args.emplace_back(OscColor{be32(d + pos)});
        else {
          const uint32_t bits = be32(d + pos);
          float f;
          std::memcpy(&f, &bits, 4);
          m.args.emplace_back(f);
        }
        pos += 4;
        break;
      case 's': {
        std::string s;
        if (!readString(d, size, pos, s)) return;
        m.args.emplace_back(std::move(s));
        break;
      }
      case 'T': m.args.emplace_back(int32_t{1}); break;
      case 'F': m.args.emplace_back(int32_t{0}); break;
      default: return;
    }
  }
  out.push_back(std::move(m));
}

void pad(std::string& s) {
  do s.push_back('\0');
  while (s.size() % 4);
}

void put32(std::string& s, uint32_t v) {
  for (int i = 3; i >= 0; --i) s.push_back(static_cast<char>((v >> (i * 8)) & 0xFF));
}

}  // namespace

std::vector<OscMessage> parseOsc(const uint8_t* data, size_t size) {
  std::vector<OscMessage> out;
  parseInto(data, size, out, 0);
  return out;
}

std::string encodeOsc(const OscMessage& m) {
  std::string out = m.address;
  pad(out);
  std::string tags = ",", body;
  for (const auto& a : m.args) {
    if (auto* i = std::get_if<int32_t>(&a)) {
      tags += 'i';
      put32(body, static_cast<uint32_t>(*i));
    } else if (auto* f = std::get_if<float>(&a)) {
      tags += 'f';
      uint32_t bits;
      std::memcpy(&bits, f, 4);
      put32(body, bits);
    } else if (auto* s = std::get_if<std::string>(&a)) {
      tags += 's';
      std::string v = *s;
      pad(v);
      body += v;
    } else if (auto* c = std::get_if<OscColor>(&a)) {
      tags += 'r';
      put32(body, c->rgba);
    }
  }
  pad(tags);
  return out + tags + body;
}

std::string encodeBundle(const std::vector<OscMessage>& messages) {
  std::string out("#bundle\0", 8);
  put32(out, 0);
  put32(out, 1);  // "inmediatamente"
  for (const auto& m : messages) {
    const std::string e = encodeOsc(m);
    put32(out, static_cast<uint32_t>(e.size()));
    out += e;
  }
  return out;
}

}  // namespace vj::control
