#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

#ifndef I2C_BUFFER_LENGTH
#define I2C_BUFFER_LENGTH 32
#endif

class TwoWire : public Stream {
public:
  TwoWire();

  // CI13XX exposes one IIC controller. Its default route comes from the
  // selected variant; alternate pins are not supported by this wrapper.
  bool begin();
  bool begin(int sda, int scl, uint32_t frequency = 0);
  void end();

  bool setClock(uint32_t frequency);
  uint32_t getClock() const;

  void beginTransmission(uint8_t address);
  uint8_t endTransmission(bool sendStop);
  uint8_t endTransmission();

  size_t requestFrom(uint8_t address, size_t quantity, bool sendStop = true);

  size_t write(uint8_t data) override;
  size_t write(const uint8_t *data, size_t quantity) override;
  using Print::write;

  int available() override;
  int read() override;
  int peek() override;
  void flush() override;

  // Last Arduino Wire status: 0 success, 1 buffer overflow, 2 address/bus
  // failure, 3 data NACK, 4 unsupported/other error.
  uint8_t lastError() const;

private:
  bool configure(uint32_t frequency);
  void clearRx();

  uint8_t _txBuffer[I2C_BUFFER_LENGTH];
  uint8_t _rxBuffer[I2C_BUFFER_LENGTH];
  size_t _txLength;
  size_t _rxLength;
  size_t _rxIndex;
  uint32_t _frequency;
  uint8_t _txAddress;
  uint8_t _pendingAddress;
  uint8_t _lastError;
  bool _begun;
  bool _transmitting;
  bool _txOverflow;
  bool _pendingWrite;
};

extern TwoWire Wire;
