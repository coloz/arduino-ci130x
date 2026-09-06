#pragma once
#include <Arduino.h>
#include <IPAddress.h>
#include <C3Protocol.h>
#include <stdio.h>

// The Arduino IPAddress API differs between cores. Keep those differences here:
// classic cores have IPv4 only; ArduinoCore-API adds IPv6; some cores add zones.
// The overloads select capabilities from the type rather than a board allowlist.
namespace espwifi {
namespace platform {
template <typename IP>
inline auto family(const IP &ip, int) -> decltype(ip.type(), uint8_t()) {
  return static_cast<unsigned>(ip.type()) == 1 ? 6 : 4;
}
template <typename IP> inline uint8_t family(const IP &, ...) { return 4; }
template <typename IP>
inline auto zone(const IP &ip, int) -> decltype(ip.zone(), uint8_t()) { return ip.zone(); }
template <typename IP> inline uint8_t zone(const IP &, ...) { return 0; }
template <typename IP>
inline auto makeIPv6(const uint8_t *bytes, uint8_t scope, IP &ip, int)
  -> decltype(ip = IP(static_cast<decltype(ip.type())>(1), bytes, scope), int32_t()) {
  ip = IP(static_cast<decltype(ip.type())>(1), bytes, scope);
  return c3::Ok;
}
template <typename IP>
inline auto makeIPv6(const uint8_t *bytes, uint8_t scope, IP &ip, long)
  -> decltype(ip = IP(static_cast<decltype(ip.type())>(1), bytes), int32_t()) {
  if (scope) return c3::Unsupported;
  ip = IP(static_cast<decltype(ip.type())>(1), bytes);
  return c3::Ok;
}
inline int32_t makeIPv6(const uint8_t *, uint8_t, IPAddress &, ...) { return c3::Unsupported; }
}
inline bool isIPv6(const IPAddress &ip) { return platform::family(ip, 0) == 6; }
inline uint8_t addressZone(const IPAddress &ip) { return platform::zone(ip, 0); }
inline String addressString(const IPAddress &ip) {
  char text[64];
  if (!isIPv6(ip)) {
    snprintf(text, sizeof(text), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  } else {
    const size_t length = static_cast<size_t>(snprintf(text, sizeof(text),
      "%x:%x:%x:%x:%x:%x:%x:%x", (ip[0] << 8) | ip[1], (ip[2] << 8) | ip[3],
      (ip[4] << 8) | ip[5], (ip[6] << 8) | ip[7], (ip[8] << 8) | ip[9],
      (ip[10] << 8) | ip[11], (ip[12] << 8) | ip[13], (ip[14] << 8) | ip[15]));
    const auto scope = addressZone(ip);
    if (scope && length < sizeof(text)) snprintf(text + length, sizeof(text) - length, "%%%u", scope);
  }
  return String(text);
}
inline uint8_t dnsFamily(const IPAddress &ip) {
  uint8_t bytes[16] = {}; IPAddress trial;
  if (platform::makeIPv6(bytes, 0, trial, 0) == c3::Unsupported) return 4;
  return isIPv6(ip) ? 6 : 0;
}
}
