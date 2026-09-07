// SPDX-License-Identifier: MIT
// Shared Arduino host / ESP32-C3 wire contract. No Arduino or SDK dependencies.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Keep this choice in the dependency-free wire header: every translation unit
// sees the same architecture default, even when ESPLinkConfig.h is not included.
// Overrides must be supplied as a build-wide compiler definition.
#ifndef C3_CRC32C_USE_TABLE
  #if defined(ARDUINO_ARCH_CI13XX)
    #define C3_CRC32C_USE_TABLE 1
  #else
    #define C3_CRC32C_USE_TABLE 0
  #endif
#endif
#if C3_CRC32C_USE_TABLE != 0 && C3_CRC32C_USE_TABLE != 1
  #error "C3_CRC32C_USE_TABLE must be 0 or 1"
#endif

namespace c3 {
static constexpr uint8_t ProtocolVersion = 1;
static constexpr size_t MaxPayload = 1024;
static constexpr size_t HeaderSize = 20;
static constexpr size_t MaxDecoded = HeaderSize + MaxPayload + 4;
static constexpr size_t MaxEncoded = MaxDecoded + MaxDecoded / 254 + 2;
static constexpr size_t MaxWire = MaxEncoded + 2;
static constexpr uint32_t DefaultBaud = 921600;
static constexpr uint32_t RetryMs = 300;

enum Status : int32_t {
  Ok = 0, InvalidArgument = -1, Unsupported = -2, NoMemory = -3,
  Busy = -4, Timeout = -5, NotConnected = -6, IoError = -7,
  ProtocolError = -8, OutcomeUnknown = -9, StaleHandle = -10,
  BufferTooSmall = -11, ResultExpired = -12, SecurityError = -13,
  WouldBlock = -14, LinkLost = -15, NotFound = -16
};
enum class Type : uint8_t {
  Hello = 1, HelloReply = 2, Request = 3, Response = 4,
  Ack = 5, Ping = 6, Pong = 7
};
enum Feature : uint32_t {
  WiFi = 1U, TCP = 2U, UDP = 4U, TLS = 8U, HCI = 16U,
  IPv6 = 32U, StationAP = 64U, OTA = 128U, MDNS = 256U
};
enum class Opcode : uint16_t {
  Echo = 0x0001, SystemInfo = 0x0002, Restart = 0x0003,
  WifiMode = 0x0100, WifiBegin = 0x0101, WifiDisconnect = 0x0102,
  WifiStatus = 0x0103, WifiConfig = 0x0104, WifiScanStart = 0x0105,
  WifiScanStatus = 0x0106, WifiScanResult = 0x0107, WifiScanDelete = 0x0108,
  WifiAPBegin = 0x0110, WifiAPConfig = 0x0111, WifiAPStop = 0x0112,
  WifiAPStatus = 0x0113, WifiHostname = 0x0114, WifiReconnect = 0x0115,
  WifiAutoReconnect = 0x0116, WifiSetOption = 0x0117,
  WifiDNS = 0x0118, WifiGetOption = 0x0119,
  DnsResolve = 0x0120, TimeConfig = 0x0121, TimeGet = 0x0122,
  SocketOpen = 0x0200, SocketConnect = 0x0201, SocketRead = 0x0202,
  SocketWrite = 0x0203, SocketStatus = 0x0204, SocketClose = 0x0205,
  SocketListen = 0x0206, SocketAccept = 0x0207, SocketOption = 0x0208,
  SocketClearRx = 0x0209,
  UdpBind = 0x0220, UdpTxBegin = 0x0221, UdpTxData = 0x0222,
  UdpTxEnd = 0x0223, UdpRxBegin = 0x0224, UdpRead = 0x0225,
  CertBegin = 0x0240, CertWrite = 0x0241, CertCommit = 0x0242,
  CertDelete = 0x0243, SocketTLS = 0x0244,
  HciBegin = 0x0300, HciEnd = 0x0301, HciWrite = 0x0302,
  HciRead = 0x0303, HciStatus = 0x0304
};
enum class SocketKind : uint8_t { TCP = 0, TLS = 1, UDP = 2, Server = 3 };
enum class SocketOption : uint8_t { NoDelay = 1, TimeoutMs = 2, KeepAliveIdle = 3, KeepAliveInterval = 4, KeepAliveCount = 5 };
enum class WifiOption : uint8_t { Sleep = 1, TxPower = 2, AutoReconnect = 3 };
enum class CertKind : uint8_t { CA = 0, Certificate = 1, PrivateKey = 2 };

inline uint16_t get16(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
inline uint32_t get32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
inline void put16(uint8_t* p, uint16_t n) { p[0] = uint8_t(n); p[1] = uint8_t(n >> 8); }
inline void put32(uint8_t* p, uint32_t n) { for (unsigned i = 0; i < 4; ++i) p[i] = uint8_t(n >> (8 * i)); }

class Writer {
 public:
  Writer(uint8_t* data, size_t capacity) : data_(data), capacity_(capacity), size_(0), ok_(true) {}
  bool bytes(const void* data, size_t n) {
    if (!ok_ || n > capacity_ - size_ || (n && (!data || !data_))) { ok_ = false; return false; }
    if (n) memcpy(data_ + size_, data, n);
    size_ += n; return true;
  }
  bool u8(uint8_t n) { return bytes(&n, 1); }
  bool u16(uint16_t n) { uint8_t b[2]; put16(b, n); return bytes(b, 2); }
  bool u32(uint32_t n) { uint8_t b[4]; put32(b, n); return bytes(b, 4); }
  bool i32(int32_t n) { return u32(static_cast<uint32_t>(n)); }
  bool string(const char* s) {
    const size_t n = s ? strlen(s) : 0;
    if (n > 65535) { ok_ = false; return false; }
    return u16(static_cast<uint16_t>(n)) && bytes(s, n);
  }
  bool ok() const { return ok_; }
  size_t size() const { return size_; }
 private:
  uint8_t* data_; size_t capacity_, size_; bool ok_;
};

class Reader {
 public:
  Reader(const uint8_t* data, size_t size) : data_(data), size_(size), pos_(0), ok_(data || !size) {}
  bool bytes(void* out, size_t n) {
    if (!ok_ || n > size_ - pos_ || (n && !out)) { ok_ = false; return false; }
    if (n) memcpy(out, data_ + pos_, n);
    pos_ += n; return true;
  }
  uint8_t u8() { uint8_t n = 0; bytes(&n, 1); return n; }
  uint16_t u16() { uint8_t b[2] = {}; bytes(b, 2); return get16(b); }
  uint32_t u32() { uint8_t b[4] = {}; bytes(b, 4); return get32(b); }
  int32_t i32() { return static_cast<int32_t>(u32()); }
  bool string(char* out, size_t capacity) {
    size_t n = u16();
    if (!ok_ || !out || !capacity || n >= capacity || n > remaining()) { ok_ = false; return false; }
    bytes(out, n); out[n] = 0; return ok_;
  }
  const uint8_t* current() const { return data_ ? data_ + pos_ : nullptr; }
  size_t remaining() const { return size_ - pos_; }
  bool ok() const { return ok_; }
  bool done() const { return ok_ && pos_ == size_; }
 private:
  const uint8_t* data_; size_t size_, pos_; bool ok_;
};

struct Frame {
  Type type = Type::Request;
  uint32_t session = 0, id = 0;
  uint16_t opcode = 0, size = 0;
  int32_t status = Ok;
  uint8_t payload[MaxPayload] = {};
};

#if C3_CRC32C_USE_TABLE
namespace detail {
struct Crc32cTable {
  uint32_t values[256] = {};
  constexpr Crc32cTable() {
    for (unsigned i = 0; i < 256; ++i) {
      uint32_t crc = i;
      for (unsigned bit = 0; bit < 8; ++bit)
        crc = (crc >> 1) ^ ((0U - (crc & 1U)) & 0x82f63b78U);
      values[i] = crc;
    }
  }
};
// One 1 KiB read-only table across translation units, without startup work
// or heap allocation. Physical placement follows the platform linker script;
// CI13xx currently loads .rodata into SRAM. Its toolchain uses C++17.
inline constexpr Crc32cTable Crc32cLookup{};
} // namespace detail
#endif

inline uint32_t crc32c(const uint8_t* data, size_t size) {
  uint32_t crc = 0xffffffffU;
  for (size_t i = 0; i < size; ++i) {
#if C3_CRC32C_USE_TABLE
    crc = (crc >> 8) ^ detail::Crc32cLookup.values[(crc ^ data[i]) & 0xffU];
#else
    crc ^= data[i];
    for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((0U - (crc & 1U)) & 0x82f63b78U);
#endif
  }
  return crc ^ 0xffffffffU;
}

inline size_t cobsEncode(const uint8_t* src, size_t n, uint8_t* dst, size_t capacity) {
  if (!dst || !capacity || (n && !src)) return 0;
  size_t out = 1, codePos = 0; uint8_t code = 1;
  for (size_t i = 0; i < n; ++i) {
    if (src[i] == 0) {
      dst[codePos] = code;
      if (out >= capacity) return 0;
      codePos = out++; code = 1;
    } else {
      if (out >= capacity) return 0;
      dst[out++] = src[i];
      if (++code == 0xff) {
        dst[codePos] = code;
        if (out >= capacity) return 0;
        codePos = out++; code = 1;
      }
    }
  }
  dst[codePos] = code; return out;
}
inline size_t cobsDecode(const uint8_t* src, size_t n, uint8_t* dst, size_t capacity) {
  size_t in = 0, out = 0;
  if (!src || !n || !dst) return 0;
  while (in < n) {
    const uint8_t code = src[in++];
    if (!code || size_t(code - 1) > n - in || size_t(code - 1) > capacity - out) return 0;
    for (unsigned j = 1; j < code; ++j) {
      if (!src[in]) return 0;
      dst[out++] = src[in++];
    }
    if (code != 0xff && in < n) {
      if (out >= capacity) return 0;
      dst[out++] = 0;
    }
  }
  return out;
}
inline size_t encode(const Frame& f, uint8_t* wire, size_t capacity) {
  if (f.size > MaxPayload || !wire || capacity < 3) return 0;
  uint8_t raw[MaxDecoded];
  raw[0] = 0xc1; raw[1] = 0xc3; raw[2] = ProtocolVersion; raw[3] = static_cast<uint8_t>(f.type);
  put32(raw + 4, f.session); put32(raw + 8, f.id); put16(raw + 12, f.opcode);
  put16(raw + 14, f.size); put32(raw + 16, static_cast<uint32_t>(f.status));
  if (f.size) memcpy(raw + HeaderSize, f.payload, f.size);
  put32(raw + HeaderSize + f.size, crc32c(raw, HeaderSize + f.size));
  wire[0] = 0;
  const size_t n = cobsEncode(raw, HeaderSize + f.size + 4, wire + 1, capacity - 2);
  if (!n) return 0;
  wire[n + 1] = 0; return n + 2;
}
class Decoder {
 public:
  uint32_t crcErrors = 0, malformed = 0, overflows = 0;
  void reset() { size_ = 0; discard_ = false; }
  bool feed(uint8_t byte, Frame& out) {
    if (byte) {
      if (discard_) return false;
      if (size_ >= sizeof(encoded_)) { ++overflows; size_ = 0; discard_ = true; return false; }
      encoded_[size_++] = byte; return false;
    }
    if (discard_) { reset(); return false; }
    if (!size_) return false;
    const size_t n = cobsDecode(encoded_, size_, raw_, sizeof(raw_)); size_ = 0;
    if (n < HeaderSize + 4 || raw_[0] != 0xc1 || raw_[1] != 0xc3 || raw_[2] != ProtocolVersion || raw_[3] < 1 || raw_[3] > 7) { ++malformed; return false; }
    const uint16_t len = get16(raw_ + 14);
    if (len > MaxPayload || n != HeaderSize + len + 4) { ++malformed; return false; }
    if (crc32c(raw_, n - 4) != get32(raw_ + n - 4)) { ++crcErrors; return false; }
    out.type = static_cast<Type>(raw_[3]); out.session = get32(raw_ + 4); out.id = get32(raw_ + 8);
    out.opcode = get16(raw_ + 12); out.size = len; out.status = static_cast<int32_t>(get32(raw_ + 16));
    if (len) memcpy(out.payload, raw_ + HeaderSize, len);
    return true;
  }
 private:
  uint8_t encoded_[MaxEncoded] = {}, raw_[MaxDecoded] = {};
  size_t size_ = 0; bool discard_ = false;
};
} // namespace c3
