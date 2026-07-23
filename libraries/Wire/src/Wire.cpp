#include "Wire.h"
#include "PeripheralManager.h"

extern "C" {
#include "ci130x_core_eclic.h"
#include "ci130x_dpmu.h"
#include "ci130x_iic.h"
#include "ci130x_scu.h"
}

namespace {
constexpr uint32_t kDefaultClock = 100000;
constexpr uint32_t kMinClock = 10000;
constexpr uint32_t kMaxClock = 400000;

constexpr uint32_t kCommandTransfer = 1U << 0;
constexpr uint32_t kCommandNack = 1U << 2;
constexpr uint32_t kCommandStop = 1U << 3;
constexpr uint32_t kCommandStart = 1U << 4;
constexpr uint32_t kInterruptClearAll = 0x7fU;
constexpr uint32_t kInterruptTimeout = 1U << 0;
constexpr uint32_t kStatusTransferError = 1U << 3;
constexpr uint32_t kStatusArbitrationLost = 1U << 5;
constexpr uint32_t kStatusInterrupt = 1U << 12;
constexpr uint32_t kStatusNack = 1U << 14;
constexpr uint32_t kStatusBusy = 1U << 15;

struct IicRegisters {
  volatile uint32_t sclDivider;
  volatile uint32_t startHold;
  volatile uint32_t dataHold;
  volatile uint32_t globalControl;
  volatile uint32_t command;
  volatile uint32_t interruptEnable;
  volatile uint32_t interruptClear;
  volatile uint32_t slaveAddress;
  volatile uint32_t transmitData;
  volatile uint32_t receiveData;
  volatile uint32_t timeout;
  volatile uint32_t status;
  volatile uint32_t busMonitor;
  volatile uint32_t interruptStatus;
};

static_assert(offsetof(IicRegisters, command) == 0x10,
              "CI130X IIC command register offset mismatch");
static_assert(offsetof(IicRegisters, receiveData) == 0x24,
              "CI130X IIC RX register offset mismatch");
static_assert(offsetof(IicRegisters, status) == 0x2c,
              "CI130X IIC status register offset mismatch");

IicRegisters *registers() {
  return reinterpret_cast<IicRegisters *>(static_cast<uintptr_t>(IIC0));
}

bool slaveReceiveBridge(char data, bool stop) {
  return Wire.handleSlaveReceive(data, stop);
}

bool slaveSendBridge(char *data, IIC_SendStateType state,
                     IIC_AckType previousAck) {
  return Wire.handleSlaveSend(data, static_cast<int>(state),
                              static_cast<int>(previousAck));
}
}  // namespace

TwoWire Wire;

TwoWire::TwoWire()
    : _txBuffer{},
      _rxBuffer{},
      _txLength(0),
      _rxLength(0),
      _rxIndex(0),
      _slaveRxLength(0),
      _slaveTxIndex(0),
      _frequency(kDefaultClock),
      _timeoutMicros(WIRE_DEFAULT_TIMEOUT),
      _txAddress(0),
      _pendingAddress(0),
      _slaveAddress(0),
      _lastError(0),
      _mode(Mode::Stopped),
      _transmitting(false),
      _txOverflow(false),
      _pendingWrite(false),
      _resetOnTimeout(WIRE_DEFAULT_RESET_WITH_TIMEOUT),
      _timeoutFlag(false),
      _inSlaveRequest(false),
      _slaveRequestActive(false),
      _onReceive(nullptr),
      _onRequest(nullptr) {}

void TwoWire::configurePins() {
  dpmu_set_adio_reuse(
      static_cast<PinPad_Name>(g_APinDescription[SDA].pad), DIGITAL_MODE);
  dpmu_set_adio_reuse(
      static_cast<PinPad_Name>(g_APinDescription[SCL].pad), DIGITAL_MODE);
  dpmu_set_io_open_drain(
      static_cast<PinPad_Name>(g_APinDescription[SDA].pad), ENABLE);
  dpmu_set_io_open_drain(
      static_cast<PinPad_Name>(g_APinDescription[SCL].pad), ENABLE);
  dpmu_set_io_pull(static_cast<PinPad_Name>(g_APinDescription[SDA].pad),
                   DPMU_IO_PULL_UP);
  dpmu_set_io_pull(static_cast<PinPad_Name>(g_APinDescription[SCL].pad),
                   DPMU_IO_PULL_UP);
  dpmu_set_io_reuse(
      static_cast<PinPad_Name>(g_APinDescription[SDA].pad),
      static_cast<IOResue_FUNCTION>(SDA_MUX));
  dpmu_set_io_reuse(
      static_cast<PinPad_Name>(g_APinDescription[SCL].pad),
      static_cast<IOResue_FUNCTION>(SCL_MUX));
}

bool TwoWire::configure(uint32_t frequency, uint8_t address, Mode mode) {
  if (frequency < kMinClock || frequency > kMaxClock ||
      (mode == Mode::Slave && (address == 0 || address > 0x7fU))) {
    _lastError = 4;
    return false;
  }

  if (_mode != Mode::Stopped) {
    end();
  }

  const uint8_t pins[] = {SDA, SCL};
  const PeripheralResource resource = PeripheralResource::Iic0;
  if (!PeripheralManager.claim(PeripheralOwner::Wire, pins, 2, &resource, 1)) {
    _lastError = 4;
    return false;
  }
  detachInterrupt(SDA);
  detachInterrupt(SCL);

  configurePins();
  _frequency = frequency;
  _slaveAddress = address;
  _mode = mode;
  _lastError = 0;
  _timeoutFlag = false;
  _pendingWrite = false;
  _transmitting = false;
  _slaveRxLength = 0;
  _slaveTxIndex = 0;
  _slaveRequestActive = false;
  clearRx();

  if (mode == Mode::Slave) {
    iic_interrupt_init(IIC0, frequency / 1000U, address, LONG_TIME_OUT);
    iic_slave_interrupt_recv(IIC0, slaveReceiveBridge);
    iic_slave_interrupt_send(IIC0, slaveSendBridge);
  } else {
    iic_polling_init(IIC0, frequency / 1000U, 0, LONG_TIME_OUT);
  }
  return true;
}

bool TwoWire::begin() {
  return configure(_frequency, 0, Mode::Master);
}

bool TwoWire::begin(uint8_t address) {
  return configure(_frequency, address, Mode::Slave);
}

bool TwoWire::begin(int sda, int scl, uint32_t frequency) {
  if ((sda >= 0 && sda != SDA) || (scl >= 0 && scl != SCL)) {
    _lastError = 4;
    return false;
  }
  return configure(frequency == 0 ? _frequency : frequency, 0, Mode::Master);
}

void TwoWire::end() {
  if (_mode == Mode::Stopped) return;

  eclic_irq_disable(IIC0_IRQn);
  registers()->interruptEnable = 0;
  registers()->interruptClear = 0xffffffffU;
  scu_set_device_gate(IIC0, DISABLE);

  const uint8_t pins[] = {SDA, SCL};
  (void)pinModeOwned(SDA, INPUT, PeripheralOwner::Wire);
  (void)pinModeOwned(SCL, INPUT, PeripheralOwner::Wire);
  const PeripheralResource resource = PeripheralResource::Iic0;
  PeripheralManager.release(PeripheralOwner::Wire, pins, 2, &resource, 1);

  _mode = Mode::Stopped;
  _transmitting = false;
  _pendingWrite = false;
  _txLength = 0;
  _slaveRxLength = 0;
  _slaveRequestActive = false;
  clearRx();
}

bool TwoWire::setClock(uint32_t frequency) {
  if (frequency < kMinClock || frequency > kMaxClock) {
    _lastError = 4;
    return false;
  }
  _frequency = frequency;
  if (_mode == Mode::Stopped) return true;
  return configure(frequency, _slaveAddress, _mode);
}

uint32_t TwoWire::getClock() const { return _frequency; }

void TwoWire::setWireTimeout(uint32_t timeout, bool resetOnTimeout) {
  _timeoutMicros = timeout;
  _resetOnTimeout = resetOnTimeout;
  _timeoutFlag = false;
}

bool TwoWire::getWireTimeoutFlag() const { return _timeoutFlag; }

void TwoWire::clearWireTimeoutFlag() { _timeoutFlag = false; }

void TwoWire::resetController() {
  if (_mode == Mode::Master) {
    configurePins();
    iic_polling_init(IIC0, _frequency / 1000U, 0, LONG_TIME_OUT);
  }
}

void TwoWire::recoverFromTimeout() {
  _timeoutFlag = true;
  _lastError = 5;
  registers()->command = kCommandStop | kCommandTransfer;
  registers()->interruptClear = kInterruptClearAll;
  if (_resetOnTimeout) resetController();
}

bool TwoWire::waitForMask(volatile uint32_t *value, uint32_t mask, bool set) {
  const uint32_t started = micros();
  for (;;) {
    if (((*value & mask) != 0U) == set) return true;
    if (_timeoutMicros != 0U &&
        static_cast<uint32_t>(micros() - started) >= _timeoutMicros) {
      recoverFromTimeout();
      return false;
    }
  }
}

bool TwoWire::executeCommand(uint32_t command, uint32_t &status) {
  IicRegisters *const reg = registers();
  reg->interruptClear = kInterruptClearAll;
  reg->command = command;
  if (!waitForMask(&reg->status, kStatusInterrupt, true)) return false;
  status = reg->status;
  if ((reg->interruptStatus & kInterruptTimeout) != 0U) {
    recoverFromTimeout();
    return false;
  }
  reg->interruptClear = kInterruptClearAll;
  return true;
}

bool TwoWire::waitBusIdle() {
  return waitForMask(&registers()->status, kStatusBusy, false);
}

void TwoWire::abortBus() {
  IicRegisters *const reg = registers();
  reg->interruptClear = kInterruptClearAll;
  reg->command = kCommandStop | kCommandTransfer;
  const bool released = waitForMask(&reg->status, kStatusBusy, false);
  if (_resetOnTimeout && !released) {
    resetController();
  }
  reg->interruptClear = kInterruptClearAll;
}

bool TwoWire::stopBus() {
  uint32_t status = 0;
  if (!executeCommand(kCommandStop | kCommandTransfer, status)) return false;
  if (!waitBusIdle()) return false;
  if ((status & (kStatusTransferError | kStatusArbitrationLost)) != 0U) {
    _lastError = 4;
    return false;
  }
  return true;
}

bool TwoWire::beginAddress(uint8_t address, bool read, uint8_t nackError) {
  IicRegisters *const reg = registers();
  reg->transmitData = (static_cast<uint32_t>(address & 0x7fU) << 1U) |
                      (read ? 1U : 0U);
  uint32_t status = 0;
  if (!executeCommand(kCommandStart | kCommandTransfer, status)) return false;
  if ((status & (kStatusTransferError | kStatusArbitrationLost)) != 0U) {
    _lastError = 4;
    abortBus();
    return false;
  }
  if ((status & kStatusNack) != 0U) {
    _lastError = nackError;
    abortBus();
    return false;
  }
  return true;
}

bool TwoWire::writeBytes(const uint8_t *data, size_t quantity) {
  IicRegisters *const reg = registers();
  for (size_t i = 0; i < quantity; ++i) {
    reg->transmitData = data[i];
    uint32_t status = 0;
    if (!executeCommand(kCommandTransfer, status)) return false;
    if ((status & (kStatusTransferError | kStatusArbitrationLost)) != 0U) {
      _lastError = 4;
      abortBus();
      return false;
    }
    if ((status & kStatusNack) != 0U) {
      _lastError = 3;
      abortBus();
      return false;
    }
  }
  return true;
}

size_t TwoWire::readBytes(uint8_t *data, size_t quantity) {
  IicRegisters *const reg = registers();
  (void)reg->receiveData;  // The SDK requires one dummy read after the address.
  size_t received = 0;
  while (received < quantity) {
    const bool last = (received + 1U == quantity);
    uint32_t status = 0;
    const uint32_t command = kCommandTransfer | (last ? kCommandNack : 0U);
    if (!executeCommand(command, status)) break;
    if ((status & (kStatusTransferError | kStatusArbitrationLost)) != 0U) {
      _lastError = 4;
      break;
    }
    data[received++] = static_cast<uint8_t>(reg->receiveData);
  }
  if (received != quantity) abortBus();
  return received;
}

bool TwoWire::probe(uint8_t address) {
  if (address > 0x7fU || _transmitting || _pendingWrite ||
      (_mode != Mode::Stopped && _mode != Mode::Master)) {
    _lastError = 4;
    return false;
  }
  if (_mode == Mode::Stopped && !begin()) return false;
  if (!waitBusIdle() || !beginAddress(address, false, 2)) return false;
  if (!stopBus()) return false;
  _lastError = 0;
  return true;
}

void TwoWire::beginTransmission(uint8_t address) {
  if (_mode == Mode::Stopped && !begin()) return;
  if (_mode != Mode::Master) {
    _lastError = 4;
    return;
  }
  if (_pendingWrite) {
    _pendingWrite = false;
    _lastError = 4;
  }
  _txAddress = address & 0x7fU;
  _txLength = 0;
  _txOverflow = false;
  _transmitting = true;
}

size_t TwoWire::write(uint8_t data) {
  if (!_transmitting && !_inSlaveRequest) {
    setWriteError();
    return 0;
  }
  if (_txLength >= I2C_BUFFER_LENGTH) {
    _txOverflow = true;
    _lastError = 1;
    setWriteError();
    return 0;
  }
  _txBuffer[_txLength++] = data;
  return 1;
}

size_t TwoWire::write(const uint8_t *data, size_t quantity) {
  if (data == nullptr) {
    setWriteError();
    return 0;
  }
  size_t written = 0;
  while (written < quantity && write(data[written]) == 1) ++written;
  return written;
}

uint8_t TwoWire::endTransmission(bool sendStop) {
  if (_mode != Mode::Master || !_transmitting) {
    _lastError = 4;
    return _lastError;
  }
  _transmitting = false;
  if (_txOverflow) {
    _txLength = 0;
    _lastError = 1;
    return _lastError;
  }
  if (!sendStop) {
    _pendingAddress = _txAddress;
    _pendingWrite = true;
    _lastError = 0;
    return 0;
  }
  if (!waitBusIdle() || !beginAddress(_txAddress, false, 2)) return _lastError;
  if (!writeBytes(_txBuffer, _txLength)) return _lastError;
  if (!stopBus()) return _lastError;
  _lastError = 0;
  return 0;
}

uint8_t TwoWire::endTransmission() { return endTransmission(true); }

void TwoWire::clearRx() {
  _rxLength = 0;
  _rxIndex = 0;
}

size_t TwoWire::requestFrom(uint8_t address, size_t quantity, bool sendStop) {
  clearRx();
  if (_mode == Mode::Stopped && !begin()) return 0;
  if (_mode != Mode::Master) {
    _lastError = 4;
    return 0;
  }
  if (quantity == 0) {
    if (_pendingWrite) {
      _pendingWrite = false;
      _lastError = 4;
    } else {
      _lastError = 0;
    }
    return 0;
  }
  if (quantity > I2C_BUFFER_LENGTH) quantity = I2C_BUFFER_LENGTH;
  address &= 0x7fU;

  if (!waitBusIdle()) return 0;
  if (_pendingWrite) {
    if (address != _pendingAddress ||
        !beginAddress(address, false, 2) ||
        !writeBytes(_txBuffer, _txLength)) {
      _pendingWrite = false;
      return 0;
    }
    _pendingWrite = false;
  }
  if (!beginAddress(address, true, 2)) return 0;
  _rxLength = readBytes(_rxBuffer, quantity);

  if (_rxLength != quantity) {
    // readBytes() already issued STOP/recovery and preserved the exact error
    // (including timeout). Return any complete bytes received before it.
    return _rxLength;
  }

  // The current controller wrapper always releases the bus after a read.  It
  // still accepts sendStop=false for source compatibility.
  (void)sendStop;
  if (!stopBus()) {
    clearRx();
    return 0;
  }
  _lastError = (_rxLength == quantity) ? 0 : 4;
  return _rxLength;
}

int TwoWire::available() {
  return _rxIndex <= _rxLength ? static_cast<int>(_rxLength - _rxIndex) : 0;
}

int TwoWire::read() {
  if (_rxIndex >= _rxLength) return -1;
  return _rxBuffer[_rxIndex++];
}

int TwoWire::peek() {
  if (_rxIndex >= _rxLength) return -1;
  return _rxBuffer[_rxIndex];
}

void TwoWire::flush() {}

void TwoWire::onReceive(void (*callback)(int)) { _onReceive = callback; }

void TwoWire::onRequest(void (*callback)(void)) { _onRequest = callback; }

void TwoWire::finishSlaveReceive() {
  if (_slaveRxLength == 0) return;
  _rxLength = _slaveRxLength;
  _rxIndex = 0;
  _slaveRxLength = 0;
  if (_onReceive != nullptr) _onReceive(static_cast<int>(_rxLength));
}

void TwoWire::prepareSlaveResponse() {
  finishSlaveReceive();  // Also handles a write followed by repeated START.
  _txLength = 0;
  _txOverflow = false;
  _slaveTxIndex = 0;
  _inSlaveRequest = true;
  if (_onRequest != nullptr) _onRequest();
  _inSlaveRequest = false;
  _slaveRequestActive = true;
}

bool TwoWire::handleSlaveReceive(char data, bool stop) {
  if (_mode != Mode::Slave) return false;
  if (stop) {
    finishSlaveReceive();
    return true;
  }
  if (_slaveRxLength >= I2C_BUFFER_LENGTH) {
    _lastError = 1;
    return false;
  }
  _rxBuffer[_slaveRxLength++] = static_cast<uint8_t>(data);
  return true;
}

bool TwoWire::handleSlaveSend(char *data, int state, int previousAck) {
  if (_mode != Mode::Slave) return false;
  if (state == static_cast<int>(IIC_SENDSTATE_STOP)) {
    _slaveRequestActive = false;
    _slaveTxIndex = 0;
    return true;
  }
  if (previousAck == static_cast<int>(IIC_ACKTYPE_NACK)) return false;
  if (!_slaveRequestActive) prepareSlaveResponse();
  if (data == nullptr || _slaveTxIndex >= _txLength) return false;
  *data = static_cast<char>(_txBuffer[_slaveTxIndex++]);
  return true;
}

uint8_t TwoWire::lastError() const { return _lastError; }
