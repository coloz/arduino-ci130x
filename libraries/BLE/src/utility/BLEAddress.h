// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdint.h>

namespace ble_detail {
// HCI stores addresses least-significant byte first. Write exactly 17 display
// characters plus NUL, without depending on the platform's variadic printf ABI.
inline void formatAddress(const uint8_t address[6], char (&result)[18]) {
  static const char hex[] = "0123456789abcdef";
  for (unsigned i = 0; i < 6; ++i) {
    const uint8_t value = address[5 - i];
    result[i * 3] = hex[value >> 4];
    result[i * 3 + 1] = hex[value & 0x0f];
    if (i < 5) result[i * 3 + 2] = ':';
  }
  result[17] = '\0';
}
} // namespace ble_detail
