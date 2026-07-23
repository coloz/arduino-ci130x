#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

#ifndef I2C_BUFFER_LENGTH
#define I2C_BUFFER_LENGTH 64
#endif

#define WIRE_HAS_TIMEOUT 1
#ifndef WIRE_DEFAULT_TIMEOUT
#define WIRE_DEFAULT_TIMEOUT 25000UL
#endif
#ifndef WIRE_DEFAULT_RESET_WITH_TIMEOUT
#define WIRE_DEFAULT_RESET_WITH_TIMEOUT true
#endif

class TwoWire : public Stream {
public:
  TwoWire();

  // CI13XX exposes one IIC controller. Its route is fixed by the variant.
  bool begin();
  bool begin(uint8_t address);
  bool begin(int sda, int scl, uint32_t frequency = 0);
  void end();

  bool setClock(uint32_t frequency);
  uint32_t getClock() const;

  void setWireTimeout(uint32_t timeout = WIRE_DEFAULT_TIMEOUT,
                      bool resetOnTimeout = WIRE_DEFAULT_RESET_WITH_TIMEOUT);
  bool getWireTimeoutFlag() const;
  void clearWireTimeoutFlag();

  bool probe(uint8_t address);

  void beginTransmission(uint8_t address);
  uint8_t endTransmission(bool sendStop);
  uint8_t endTransmission();

  size_t requestFrom(uint8_t address, size_t quantity, bool sendStop = true);

  size_t write(uint8_t data) override;
  size_t write(const uint8_t *data, size_t quantity) override;
  inline size_t write(unsigned long value) { return write(static_cast<uint8_t>(value)); }
  inline size_t write(long value) { return write(static_cast<uint8_t>(value)); }
  inline size_t write(unsigned int value) { return write(static_cast<uint8_t>(value)); }
  inline size_t write(int value) { return write(static_cast<uint8_t>(value)); }
  using Print::write;

  int available() override;
  int read() override;
  int peek() override;
  void flush() override;

  void onReceive(void (*callback)(int));
  void onRequest(void (*callback)(void));

  // Arduino Wire status: 0 success, 1 buffer overflow, 2 address NACK,
  // 3 data NACK, 4 other error, 5 timeout.
  uint8_t lastError() const;

  // SDK slave-interrupt bridges. Applications should not call these.
  bool handleSlaveReceive(char data, bool stop);
  bool handleSlaveSend(char *data, int state, int previousAck);

private:
  enum class Mode : uint8_t { Stopped, Master, Slave };

  bool configure(uint32_t frequency, uint8_t address, Mode mode);
  void configurePins();
  void resetController();
  void recoverFromTimeout();
  void clearRx();
  void finishSlaveReceive();
  void prepareSlaveResponse();

  bool waitForMask(volatile uint32_t *value, uint32_t mask, bool set);
  bool executeCommand(uint32_t command, uint32_t &status);
  bool stopBus();
  bool beginAddress(uint8_t address, bool read, uint8_t nackError);
  bool writeBytes(const uint8_t *data, size_t quantity);
  size_t readBytes(uint8_t *data, size_t quantity);
  bool waitBusIdle();
  void abortBus();

  uint8_t _txBuffer[I2C_BUFFER_LENGTH];
  uint8_t _rxBuffer[I2C_BUFFER_LENGTH];
  size_t _txLength;
  size_t _rxLength;
  size_t _rxIndex;
  volatile size_t _slaveRxLength;
  volatile size_t _slaveTxIndex;
  uint32_t _frequency;
  uint32_t _timeoutMicros;
  uint8_t _txAddress;
  uint8_t _pendingAddress;
  uint8_t _slaveAddress;
  volatile uint8_t _lastError;
  Mode _mode;
  bool _transmitting;
  bool _txOverflow;
  bool _pendingWrite;
  bool _resetOnTimeout;
  volatile bool _timeoutFlag;
  volatile bool _inSlaveRequest;
  volatile bool _slaveRequestActive;
  void (*_onReceive)(int);
  void (*_onRequest)(void);
};

extern TwoWire Wire;
