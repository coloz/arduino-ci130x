// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
using std::min;
using String = std::string;
inline char* utoa(unsigned value, char* output, int radix) {
  if (radix != 16 || value > 15) std::abort();
  output[0] = "0123456789abcdef"[value]; output[1] = '\0';
  return output;
}
