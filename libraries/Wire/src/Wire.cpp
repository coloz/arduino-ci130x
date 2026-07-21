#include "Wire.h"

extern "C" {
#include "ci130x_dpmu.h"
#include "ci130x_iic.h"
#include "ci130x_scu.h"
}

namespace {
constexpr uint32_t kDefaultClock = 100000;
constexpr uint32_t kMinClock = 10000;
constexpr uint32_t kMaxClock = 400000;
constexpr uint32_t kProbeTimeout = static_cast<uint32_t>(LONG_TIME_OUT);

constexpr uint32_t kCommandTransfer = 1U << 0;
constexpr uint32_t kCommandStop = 1U << 3;
constexpr uint32_t kCommandStart = 1U << 4;
constexpr uint32_t kInterruptClearAll = 0x7fU;
constexpr uint32_t kStopInterrupt = 1U << 4;
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
static_assert(offsetof(IicRegisters, transmitData) == 0x20,
              "CI130X IIC TX register offset mismatch");
static_assert(offsetof(IicRegisters, status) == 0x2c,
              "CI130X IIC status register offset mismatch");

bool waitForMask(volatile uint32_t *value, uint32_t mask, bool set) {
  uint32_t remaining = kProbeTimeout;
  while (remaining-- != 0U) {
    if (((*value & mask) != 0U) == set) {
      return true;
    }
  }
  return false;
}
}

TwoWire Wire;

TwoWire::TwoWire()
    : _txLength(0),
      _rxLength(0),
      _rxIndex(0),
      _frequency(kDefaultClock),
      _txAddress(0),
      _pendingAddress(0),
      _lastError(0),
      _begun(false),
      _transmitting(false),
      _txOverflow(false),
      _pendingWrite(false) {}

bool TwoWire::configure(uint32_t frequency) {
  if (frequency < kMinClock || frequency > kMaxClock) {
    _lastError = 4;
    return false;
  }

  // The selected variant owns the physical IIC0 route. CI1302/CI1303 use
  // PA2/PA3, while the CI1306 development board uses PB7/PC0.
  dpmu_set_io_reuse(
      static_cast<PinPad_Name>(g_APinDescription[SDA].pad),
      static_cast<IOResue_FUNCTION>(SDA_MUX));
  dpmu_set_io_reuse(
      static_cast<PinPad_Name>(g_APinDescription[SCL].pad),
      static_cast<IOResue_FUNCTION>(SCL_MUX));
  iic_polling_init(IIC0, frequency / 1000U, 0, LONG_TIME_OUT);

  _frequency = frequency;
  _begun = true;
  _lastError = 0;
  return true;
}

bool TwoWire::begin() {
  return configure(_frequency);
}

bool TwoWire::begin(int sda, int scl, uint32_t frequency) {
  if ((sda >= 0 && sda != SDA) ||
      (scl >= 0 && scl != SCL)) {
    _lastError = 4;
    return false;
  }
  return configure(frequency == 0 ? _frequency : frequency);
}

void TwoWire::end() {
  _begun = false;
  _transmitting = false;
  _pendingWrite = false;
  _txLength = 0;
  clearRx();
}

bool TwoWire::setClock(uint32_t frequency) {
  if (frequency < kMinClock || frequency > kMaxClock) {
    _lastError = 4;
    return false;
  }
  _frequency = frequency;
  return !_begun || configure(frequency);
}

uint32_t TwoWire::getClock() const {
  return _frequency;
}

bool TwoWire::probe(uint8_t address) {
  if (address > 0x7fU || _transmitting || _pendingWrite) {
    _lastError = 4;
    return false;
  }
  if (!_begun && !begin()) {
    return false;
  }

  IicRegisters *registers =
      reinterpret_cast<IicRegisters *>(static_cast<uintptr_t>(IIC0));
  if (!waitForMask(&registers->status, kStatusBusy, false)) {
    _lastError = 4;
    return false;
  }

  registers->transmitData = static_cast<uint32_t>(address) << 1U;
  registers->interruptClear = kStopInterrupt;
  registers->command = kCommandStart | kCommandTransfer;

  const bool startCompleted =
      waitForMask(&registers->status, kStatusInterrupt, true);
  const uint32_t status = registers->status;

  // Once START has been requested, every result path must issue STOP. This
  // includes ACK, NACK, transfer error, arbitration loss, and local timeout.
  registers->command = kCommandStop | kCommandTransfer;
  registers->interruptClear = kInterruptClearAll;
  const bool stopCompleted =
      waitForMask(&registers->status, kStatusInterrupt, true);
  registers->interruptClear = kInterruptClearAll;
  const bool busReleased =
      waitForMask(&registers->status, kStatusBusy, false);

  if (!startCompleted || !stopCompleted || !busReleased) {
    _lastError = 4;
  } else if ((status &
              (kStatusTransferError | kStatusArbitrationLost)) != 0U) {
    _lastError = 4;
  } else if ((status & kStatusNack) != 0U) {
    _lastError = 2;
  } else {
    _lastError = 0;
  }
  return _lastError == 0;
}

void TwoWire::beginTransmission(uint8_t address) {
  if (!_begun && !begin()) {
    return;
  }

  // A no-STOP write is deferred so it can be submitted together with the
  // following read through the SDK's multi-transmission API. Starting a new
  // write before that read invalidates the deferred transaction.
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
  if (!_transmitting) {
    setWriteError();
    return 0;
  }
  if (_txLength >= I2C_BUFFER_LENGTH) {
    _txOverflow = true;
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
  while (written < quantity && write(data[written]) == 1) {
    ++written;
  }
  return written;
}

uint8_t TwoWire::endTransmission(bool sendStop) {
  if (!_begun || !_transmitting) {
    _lastError = 4;
    return _lastError;
  }
  _transmitting = false;

  if (_txOverflow) {
    _txLength = 0;
    _lastError = 1;
    return _lastError;
  }

  if (_txLength == 0) {
    if (!sendStop) {
      _lastError = 4;
      return _lastError;
    }
    (void)probe(_txAddress);
    return _lastError;
  }

  if (!sendStop) {
    _pendingAddress = _txAddress;
    _pendingWrite = true;
    _lastError = 0;
    return 0;
  }

  uint8_t lastAck = 0;
  int32_t sent = iic_master_polling_send(
      IIC0, _txAddress, reinterpret_cast<const char *>(_txBuffer),
      static_cast<int32_t>(_txLength), &lastAck);

  if (sent == static_cast<int32_t>(_txLength) && lastAck == 0) {
    _lastError = 0;
  } else if (lastAck != 0 || sent >= 0) {
    _lastError = 3;
  } else {
    _lastError = 2;
  }
  return _lastError;
}

uint8_t TwoWire::endTransmission() {
  return endTransmission(true);
}

void TwoWire::clearRx() {
  _rxLength = 0;
  _rxIndex = 0;
}

size_t TwoWire::requestFrom(uint8_t address, size_t quantity, bool sendStop) {
  clearRx();
  if (!_begun && !begin()) {
    return 0;
  }
  if (quantity == 0) {
    if (_pendingWrite) {
      // A deferred write must be followed by a real read; otherwise no SDK
      // transaction would ever be emitted.
      _pendingWrite = false;
      _lastError = 4;
      return 0;
    }
    _lastError = 0;
    return 0;
  }
  if (quantity > I2C_BUFFER_LENGTH) {
    quantity = I2C_BUFFER_LENGTH;
  }

  address &= 0x7fU;
  int32_t received = -1;

  if (_pendingWrite) {
    if (address != _pendingAddress) {
      _pendingWrite = false;
      _lastError = 4;
      return 0;
    }

    multi_transmission_msg messages[2] = {};
    messages[0].buf = reinterpret_cast<char *>(_txBuffer);
    messages[0].size = static_cast<int>(_txLength);
    messages[0].flag = IIC_M_WRITE;
    messages[1].buf = reinterpret_cast<char *>(_rxBuffer);
    messages[1].size = static_cast<int>(quantity);
    messages[1].flag = IIC_M_READ;

    int32_t completed =
        iic_master_multi_transmission(IIC0, address, messages, 2);
    if (completed == 2 && messages[1].read_size == static_cast<int>(quantity)) {
      received = messages[1].read_size;
    }
    _pendingWrite = false;
  } else {
    received = iic_master_polling_recv(
        IIC0, address, reinterpret_cast<char *>(_rxBuffer),
        static_cast<int32_t>(quantity));
  }

  // The SDK polling receiver always emits STOP. A false sendStop is accepted
  // for source compatibility, but cannot keep the bus claimed afterwards.
  (void)sendStop;

  if (received < 0) {
    _lastError = 2;
    return 0;
  }
  if (received > static_cast<int32_t>(quantity)) {
    received = static_cast<int32_t>(quantity);
  }
  _rxLength = static_cast<size_t>(received);
  _lastError = (_rxLength == quantity) ? 0 : 4;
  return _rxLength;
}

int TwoWire::available() {
  return static_cast<int>(_rxLength - _rxIndex);
}

int TwoWire::read() {
  if (_rxIndex >= _rxLength) {
    return -1;
  }
  return _rxBuffer[_rxIndex++];
}

int TwoWire::peek() {
  if (_rxIndex >= _rxLength) {
    return -1;
  }
  return _rxBuffer[_rxIndex];
}

void TwoWire::flush() {}

uint8_t TwoWire::lastError() const {
  return _lastError;
}
