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
#ifndef WIRE_FAILSAFE_TIMEOUT
#define WIRE_FAILSAFE_TIMEOUT 25000UL
#endif

class TwoWire : public Stream {
public:
  using TransferCallback =
      void (*)(uint8_t status, size_t transferred, void *context);

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

  // Starts a zero-copy receive / copied-transmit master transaction and
  // returns immediately. The callback runs in the Arduino event dispatcher,
  // never in the IIC ISR. readData must remain valid until the callback.
  bool transferAsync(uint8_t address, const uint8_t *writeData,
                     size_t writeSize, uint8_t *readData, size_t readSize,
                     bool sendStop, TransferCallback callback,
                     void *context = nullptr);
  bool transferBusy() const;
  bool cancelTransfer();
  bool recoverBus();
  uint32_t recoveryCount() const;

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
  void onReceiveISR(void (*callback)(int));
  void onRequestISR(void (*callback)(void));
  uint32_t callbackDrops() const;

  // Arduino Wire status: 0 success, 1 buffer overflow, 2 address NACK,
  // 3 data NACK, 4 other error, 5 timeout, 6 busy, 7 arbitration lost,
  // 8 bus recovery failed, 9 deferred callback queue full.
  uint8_t lastError() const;

  // SDK slave-interrupt bridges. Applications should not call these.
  bool handleMasterInterrupt();
  void handleMasterTimeout();
  bool handleSlaveReceive(char data, bool stop);
  bool handleSlaveSend(char *data, int state, int previousAck);

private:
  enum class Mode : uint8_t { Stopped, Master, Slave };
  enum class MasterState : uint8_t {
    Idle,
    AddressWrite,
    Write,
    AddressRead,
    Read,
    Stop
  };

  bool configure(uint32_t frequency, uint8_t address, Mode mode);
  void configurePins();
  void resetController();
  void recoverFromTimeout();
  void clearRx();
  void finishSlaveReceive();
  void prepareSlaveResponse();
  static void receiveEvent(void *context, uint32_t value);
  static void masterEvent(void *context, uint32_t generation);
  bool startMasterTransfer(uint8_t address, const uint8_t *writeData,
                           size_t writeSize, size_t readSize, bool sendStop,
                           TransferCallback callback, void *context,
                           void *waiter);
  bool waitMasterTransfer();
  void finishMasterFromIsr(uint8_t result, bool busHeld);
  void issueReadFromIsr();
  void issueStopFromIsr();
  uint32_t effectiveTimeoutMicros() const;

  uint8_t _txBuffer[I2C_BUFFER_LENGTH];
  uint8_t _rxBuffer[I2C_BUFFER_LENGTH];
  uint8_t _slaveRxBuffer[I2C_BUFFER_LENGTH];
  size_t _txLength;
  size_t _rxLength;
  size_t _rxIndex;
  volatile size_t _slaveRxLength;
  volatile size_t _slaveTxIndex;
  uint32_t _frequency;
  uint32_t _timeoutMicros;
  uint8_t _txAddress;
  uint8_t _slaveAddress;
  volatile uint8_t _lastError;
  Mode _mode;
  bool _transmitting;
  bool _txOverflow;
  volatile bool _busHeld;
  bool _resetOnTimeout;
  volatile bool _timeoutFlag;
  volatile bool _inSlaveRequest;
  volatile bool _slaveRequestActive;
  void (*_onReceive)(int);
  void (*_onReceiveISR)(int);
  void (*_onRequest)(void);
  volatile bool _receiveCallbackPending;
  volatile uint32_t _receiveGeneration;
  volatile uint32_t _pendingReceiveGeneration;
  volatile uint32_t _callbackDrops;
  volatile MasterState _masterState;
  volatile bool _masterActive;
  volatile bool _masterCompletionPending;
  volatile uint8_t _masterResult;
  volatile size_t _masterTxIndex;
  volatile size_t _masterTxLength;
  volatile size_t _masterRxLength;
  volatile size_t _masterRxTarget;
  volatile uint32_t _masterGeneration;
  uint8_t _masterAddress;
  bool _masterSendStop;
  void *_masterWaiter;
  uint8_t *_asyncReadData;
  TransferCallback _masterCallback;
  void *_masterCallbackContext;
  volatile uint32_t _recoveryCount;
};

extern TwoWire Wire;
