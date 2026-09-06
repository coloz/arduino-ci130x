// SPDX-License-Identifier: MIT
// Native tests exercise the exact dependency-free codec shared by both MCUs.
#include "../src/C3Protocol.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <algorithm>

#ifdef EXPECT_C3_CRC32C_USE_TABLE
static_assert(C3_CRC32C_USE_TABLE == EXPECT_C3_CRC32C_USE_TABLE, "CRC architecture default/override mismatch");
#endif
static uint32_t referenceCrc32c(const uint8_t* data, size_t size) {
  uint32_t crc = 0xffffffffU;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((0U - (crc & 1U)) & 0x82f63b78U);
  }
  return crc ^ 0xffffffffU;
}
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); std::abort(); } } while (0)
static unsigned feed(c3::Decoder& d, const std::vector<uint8_t>& wire, c3::Frame& f) {
  unsigned count = 0;
  for (uint8_t b : wire) if (d.feed(b, f)) ++count;
  return count;
}
static std::vector<uint8_t> encode(const c3::Frame& f) {
  std::vector<uint8_t> bytes(c3::MaxWire);
  const size_t n = c3::encode(f, bytes.data(), bytes.size());
  CHECK(n > 0 && n <= c3::MaxWire); bytes.resize(n); return bytes;
}
static void same(const c3::Frame& a, const c3::Frame& b) {
  CHECK(a.type == b.type && a.session == b.session && a.id == b.id);
  CHECK(a.opcode == b.opcode && a.status == b.status && a.size == b.size);
  CHECK(std::memcmp(a.payload, b.payload, a.size) == 0);
}
int main() {
  CHECK(c3::crc32c(reinterpret_cast<const uint8_t*>("123456789"), 9) == 0xe3069283U);
  CHECK(c3::crc32c(nullptr, 0) == 0);
  uint32_t random = 0x1306c3;
  c3::Frame original, decoded;
  original.type = c3::Type::Response; original.session = 0xffffffffU;
  original.id = 0xabcdef01; original.opcode = 0x0203; original.status = c3::OutcomeUnknown;
  for (unsigned pattern = 0; pattern < 4; ++pattern) {
    for (size_t len = 0; len <= c3::MaxPayload; ++len) {
      original.size = static_cast<uint16_t>(len);
      for (size_t i = 0; i < len; ++i) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        original.payload[i] = pattern == 0 ? 0 : pattern == 1 ? 255 : pattern == 2 ? uint8_t(i) : uint8_t(random);
      }
      CHECK(c3::crc32c(original.payload, len) == referenceCrc32c(original.payload, len));
      const auto wire = encode(original);
      CHECK(wire.front() == 0 && wire.back() == 0);
      CHECK(std::find(wire.begin() + 1, wire.end() - 1, uint8_t(0)) == wire.end() - 1);
      c3::Decoder d;
      CHECK(feed(d, wire, decoded) == 1); same(original, decoded);
      CHECK(feed(d, wire, decoded) == 1); // Back-to-back frames.
    }
  }
  original.status = c3::Ok;
  const auto good = encode(original);
  // Every bit in a maximum decoded frame, including the header and CRC.
  uint8_t raw[c3::MaxDecoded], encoded[c3::MaxWire];
  const size_t rawSize = c3::cobsDecode(good.data() + 1, good.size() - 2, raw, sizeof(raw));
  CHECK(rawSize == c3::MaxDecoded);
  for (size_t i = 0; i < rawSize; ++i) for (unsigned bit = 0; bit < 8; ++bit) {
    raw[i] ^= uint8_t(1U << bit);
    encoded[0] = 0;
    size_t n = c3::cobsEncode(raw, rawSize, encoded + 1, sizeof(encoded) - 2);
    CHECK(n != 0); encoded[n + 1] = 0;
    c3::Decoder d;
    CHECK(feed(d, {encoded, encoded + n + 2}, decoded) == 0);
    CHECK(feed(d, good, decoded) == 1); same(original, decoded);
    raw[i] ^= uint8_t(1U << bit);
  }
  // Lost/inserted bytes, all possible truncation points, delimiter recovery.
  for (size_t i = 1; i + 1 < good.size(); ++i) {
    c3::Decoder d;
    auto damaged = good; damaged.erase(damaged.begin() + i);
    CHECK(feed(d, damaged, decoded) == 0);
    CHECK(feed(d, good, decoded) == 1);
    damaged = good; damaged.insert(damaged.begin() + i, 0x55);
    CHECK(feed(d, damaged, decoded) == 0);
    CHECK(feed(d, good, decoded) == 1);
    d.reset();
    CHECK(feed(d, {good.begin(), good.begin() + i}, decoded) == 0);
    CHECK(feed(d, good, decoded) == 1);
  }
  c3::Decoder d;
  CHECK(feed(d, std::vector<uint8_t>(c3::MaxEncoded + 50, 1), decoded) == 0);
  CHECK(d.overflows == 1);
  CHECK(feed(d, good, decoded) == 1); same(original, decoded);
  CHECK(feed(d, {0, 0, 0, 1, 0, 255, 1, 0}, decoded) == 0);
  CHECK(feed(d, good, decoded) == 1);
  // Exact capacity and malformed field handling cannot overrun a buffer.
  uint8_t guarded[16]; std::memset(guarded, 0xa5, sizeof(guarded));
  c3::Writer w(guarded + 1, 14);
  CHECK(w.u8(7) && w.u16(0xbeef) && w.i32(-15) && w.string("hello"));
  CHECK(w.size() == 14 && w.ok()); CHECK(!w.u8(1));
  CHECK(guarded[0] == 0xa5 && guarded[15] == 0xa5);
  c3::Reader r(guarded + 1, 14); char text[6];
  CHECK(r.u8() == 7 && r.u16() == 0xbeef && r.i32() == -15);
  CHECK(r.string(text, sizeof(text)) && std::strcmp(text, "hello") == 0 && r.done());
  CHECK(r.u8() == 0 && !r.ok() && r.remaining() == 0);
  c3::Reader bad(nullptr, 1); CHECK(!bad.ok() && !bad.bytes(text, 1));
  c3::Writer null(nullptr, 1); CHECK(!null.u8(1));
  uint8_t hugeString[] = {0xff, 0xff}; c3::Reader invalid(hugeString, 2);
  CHECK(!invalid.string(text, sizeof(text)));
  original.size = c3::MaxPayload + 1; CHECK(c3::encode(original, encoded, sizeof(encoded)) == 0);
  original.size = c3::MaxPayload; CHECK(c3::encode(original, encoded, 1) == 0);
  CHECK(c3::cobsEncode(nullptr, 1, encoded, sizeof(encoded)) == 0);
  std::printf("PASS: %u assertions; all payload lengths, CRC bit faults and UART resynchronization\n", checks);
}
