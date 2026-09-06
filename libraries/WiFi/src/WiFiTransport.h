#pragma once
#include <Arduino.h>
#include <ESPLink.h>
#include <C3Protocol.h>
#include "WiFiSharedPtr.h"
#include "WiFiPlatform.h"
#include <new>
#include <algorithm>

namespace espwifi {
inline void address(c3::Writer &w, const IPAddress &ip) {
  uint8_t bytes[16] = {};
  const uint8_t count = isIPv6(ip) ? 16 : 4;
  for (uint8_t i = 0; i < count; ++i) bytes[i] = ip[i];
  w.u8(count == 16 ? 6 : 4); w.u8(addressZone(ip)); w.bytes(bytes, sizeof(bytes));
}
inline IPAddress address(c3::Reader &r, int32_t &error) {
  const uint8_t family = r.u8(), zone = r.u8();
  uint8_t bytes[16] = {}; r.bytes(bytes, sizeof(bytes));
  IPAddress result;
  int32_t converted = c3::Ok;
  if (family == 6) converted = platform::makeIPv6(bytes, zone, result, 0);
  else if (family == 0 || family == 4) result = IPAddress(bytes[0], bytes[1], bytes[2], bytes[3]);
  else converted = c3::ProtocolError;
  if (!r.ok()) converted = c3::ProtocolError;
  if (error == c3::Ok) error = converted;
  return result;
}
inline int32_t call(c3::Opcode op, const uint8_t *request, size_t length,
                    uint8_t *reply, size_t &replyLength, uint32_t timeout = 5000) {
  return ESPLink.request(static_cast<uint16_t>(op), request, length, reply, replyLength, timeout);
}
inline int32_t command(c3::Opcode op, const uint8_t *request, size_t length,
                       uint32_t timeout = 5000) {
  size_t n = 0; return call(op, request, length, nullptr, n, timeout);
}
inline bool current(uint32_t session) { return ESPLink.ready() && session && session == ESPLink.session(); }
inline void close(uint32_t handle, uint32_t session) {
  if (!handle || !current(session)) return;
  uint8_t data[4]; c3::put32(data, handle);
  command(c3::Opcode::SocketClose, data, sizeof(data), 1500);
}
inline String macString(const uint8_t *mac) {
  char text[18]; snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X",
    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]); return String(text);
}
}
