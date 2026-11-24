#include <gtest/gtest.h>

#include "control/WsCodec.h"

using namespace vj::control;

TEST(WsCodec, Rfc6455AcceptExample) {
  EXPECT_EQ(websocketAccept("dGhlIHNhbXBsZSBub25jZQ=="), "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

TEST(WsCodec, Base64Padding) {
  EXPECT_EQ(base64("f"), "Zg==");
  EXPECT_EQ(base64("fo"), "Zm8=");
  EXPECT_EQ(base64("foo"), "Zm9v");
}

TEST(WsCodec, Sha1LongerThanOneBlock) {
  EXPECT_EQ(base64(sha1(std::string(1000, 'a'))), "KR6abGaZSUm1e6XmUDYemPw2sbo=");
}

TEST(WsCodec, RoundTripSizes) {
  for (size_t n : {size_t{0}, size_t{125}, size_t{126}, size_t{65535}, size_t{65536}}) {
    const std::string payload(n, 'x');
    const std::string enc = encodeFrame(WsOpcode::Text, payload);
    WsFrame f;
    size_t used = 0;
    ASSERT_EQ(parseFrame(enc, f, used, 1 << 20), WsParse::Ok);
    EXPECT_EQ(f.payload, payload);
    EXPECT_EQ(used, enc.size());
    EXPECT_FALSE(f.masked);
  }
}

TEST(WsCodec, UnmasksClientFrame) {
  const std::string frame("\x81\x85\x37\xfa\x21\x3d\x7f\x9f\x4d\x51\x58", 11);  // RFC 6455, 5.7
  WsFrame f;
  size_t used = 0;
  ASSERT_EQ(parseFrame(frame, f, used, 100), WsParse::Ok);
  EXPECT_EQ(f.payload, "Hello");
  EXPECT_TRUE(f.masked);
}

TEST(WsCodec, PartialAndOversize) {
  const std::string enc = encodeFrame(WsOpcode::Text, std::string(200, 'x'));
  WsFrame f;
  size_t used = 0;
  EXPECT_EQ(parseFrame(enc.substr(0, 10), f, used, 1000), WsParse::NeedMore);
  EXPECT_EQ(parseFrame(enc, f, used, 100), WsParse::TooLarge);
}
