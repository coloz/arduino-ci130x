// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// Deliberately no HardwareSerial, Serial, Serial1, Serial2, or RTOS declarations.
// Production ESPLink must compile with only the public Arduino Stream contract.
class Stream {
 public:
  virtual ~Stream() = default;
  virtual int available() = 0;
  virtual int read() = 0;
  virtual size_t write(const uint8_t* data, size_t size) = 0;
};
unsigned long millis();
unsigned long micros();
void yield();
