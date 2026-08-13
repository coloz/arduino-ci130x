#include "Wire.h"
#include "ArduinoEvent.h"
#include "PeripheralManager.h"

#include <string.h>

extern "C" {
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
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
constexpr uint32_t kInterruptRxReady = 1U << 1;
constexpr uint32_t kInterruptTxReady = 1U << 2;
constexpr uint32_t kInterruptBusError = 1U << 3;
constexpr uint32_t kInterruptStop = 1U << 4;
constexpr uint32_t kInterruptArbitrationLost = 1U << 5;
constexpr uint32_t kStatusTransferError = 1U << 3;
constexpr uint32_t kStatusArbitrationLost = 1U << 5;
constexpr uint32_t kStatusInterrupt = 1U << 12;
constexpr uint32_t kStatusNack = 1U << 14;
constexpr uint32_t kStatusBusy = 1U << 15;
constexpr uint8_t kErrorOther = 4U;
constexpr uint8_t kErrorTimeout = 5U;
constexpr uint8_t kErrorBusy = 6U;
constexpr uint8_t kErrorArbitrationLost = 7U;
constexpr uint8_t kErrorRecoveryFailed = 8U;
constexpr uint8_t kErrorQueueFull = 9U;
TimerHandle_t s_masterTimeoutTimer;

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

void masterTimeoutTimer(TimerHandle_t) {
  Wire.handleMasterTimeout();
}
}  // namespace

TwoWire Wire;

extern "C" bool chipintelli_wire_master_irq(void) {
  return Wire.handleMasterInterrupt();
}

TwoWire::TwoWire()
    : _txBuffer{},
      _rxBuffer{},
      _slaveRxBuffer{},
      _txLength(0),
      _rxLength(0),
      _rxIndex(0),
      _slaveRxLength(0),
      _slaveTxIndex(0),
      _frequency(kDefaultClock),
      _timeoutMicros(WIRE_DEFAULT_TIMEOUT),
      _txAddress(0),
      _slaveAddress(0),
      _lastError(0),
      _mode(Mode::Stopped),
      _transmitting(false),
      _txOverflow(false),
      _busHeld(false),
      _resetOnTimeout(WIRE_DEFAULT_RESET_WITH_TIMEOUT),
      _timeoutFlag(false),
      _inSlaveRequest(false),
      _slaveRequestActive(false),
      _onReceive(nullptr),
      _onReceiveISR(nullptr),
      _onRequest(nullptr),
      _receiveCallbackPending(false),
      _receiveGeneration(1U),
      _pendingReceiveGeneration(0U),
      _callbackDrops(0U),
      _masterState(MasterState::Idle),
      _masterActive(false),
      _masterCompletionPending(false),
      _masterResult(0U),
      _masterTxIndex(0U),
      _masterTxLength(0U),
      _masterRxLength(0U),
      _masterRxTarget(0U),
      _masterGeneration(0U),
      _masterAddress(0U),
      _masterSendStop(true),
      _masterWaiter(nullptr),
      _asyncReadData(nullptr),
      _masterCallback(nullptr),
      _masterCallbackContext(nullptr),
      _recoveryCount(0U) {}

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
  // IIC0 owns the open-drain output enable through the pad mux. Restore the
  // pad direction after GPIO-based bus recovery; otherwise the GPIO output
  // direction remains latched and disconnects IIC0 from SDA/SCL on CI1306.
  dpmu_set_io_direction(
      static_cast<PinPad_Name>(g_APinDescription[SDA].pad),
      DPMU_IO_DIRECTION_INPUT);
  dpmu_set_io_direction(
      static_cast<PinPad_Name>(g_APinDescription[SCL].pad),
      DPMU_IO_DIRECTION_INPUT);
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
  _busHeld = false;
  _transmitting = false;
  _masterState = MasterState::Idle;
  _masterActive = false;
  _masterCompletionPending = false;
  _slaveRxLength = 0;
  _slaveTxIndex = 0;
  _slaveRequestActive = false;
  _receiveCallbackPending = false;
  if (++_receiveGeneration == 0U) ++_receiveGeneration;
  clearRx();

  if (mode == Mode::Slave) {
    iic_interrupt_init(IIC0, frequency / 1000U, address, LONG_TIME_OUT);
    iic_slave_interrupt_recv(IIC0, slaveReceiveBridge);
    iic_slave_interrupt_send(IIC0, slaveSendBridge);
  } else {
    iic_interrupt_init(IIC0, frequency / 1000U, 0, LONG_TIME_OUT);
    registers()->interruptEnable = kInterruptTimeout | kInterruptRxReady |
                                   kInterruptTxReady | kInterruptBusError |
                                   kInterruptStop |
                                   kInterruptArbitrationLost;
  }
  return true;
}

bool TwoWire::begin() {
  if (!configure(_frequency, 0, Mode::Master)) return false;
  // Arduino uploads and watchdog resets restart the MCU without necessarily
  // resetting I2C peripherals. A slave can therefore be left mid-byte even
  // though the freshly initialized controller reports an idle bus. Recover
  // unconditionally before the first master transaction; checking BUSY alone
  // did not catch this state on SSD1306 displays.
  return recoverBus();
}

bool TwoWire::begin(uint8_t address) {
  return configure(_frequency, address, Mode::Slave);
}

bool TwoWire::begin(int sda, int scl, uint32_t frequency) {
  if ((sda >= 0 && sda != SDA) || (scl >= 0 && scl != SCL)) {
    _lastError = 4;
    return false;
  }
  if (!configure(frequency == 0 ? _frequency : frequency, 0,
                 Mode::Master)) {
    return false;
  }
  return recoverBus();
}

void TwoWire::end() {
  if (_mode == Mode::Stopped) return;

  (void)cancelTransfer();

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
  _busHeld = false;
  _txLength = 0;
  _slaveRxLength = 0;
  _slaveRequestActive = false;
  _receiveCallbackPending = false;
  if (++_receiveGeneration == 0U) ++_receiveGeneration;
  clearRx();
}

bool TwoWire::setClock(uint32_t frequency) {
  if (frequency < kMinClock || frequency > kMaxClock) {
    _lastError = 4;
    return false;
  }
  // Libraries such as U8g2 call setClock() before every I2C transaction.
  // Reconfiguring an already-running controller at the same frequency can
  // glitch the bus between back-to-back transfers and make the next address
  // phase appear as a NACK. Match the usual Wire semantics and make an
  // unchanged clock request a no-op.
  if (_frequency == frequency) {
    _lastError = 0;
    return true;
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
    iic_interrupt_init(IIC0, _frequency / 1000U, 0, LONG_TIME_OUT);
    registers()->interruptEnable = kInterruptTimeout | kInterruptRxReady |
                                   kInterruptTxReady | kInterruptBusError |
                                   kInterruptStop |
                                   kInterruptArbitrationLost;
  }
}

void TwoWire::recoverFromTimeout() {
  _timeoutFlag = true;
  if (_resetOnTimeout) (void)recoverBus();
  _lastError = kErrorTimeout;
}

uint32_t TwoWire::effectiveTimeoutMicros() const {
  return _timeoutMicros != 0U ? _timeoutMicros
                              : static_cast<uint32_t>(WIRE_FAILSAFE_TIMEOUT);
}

bool TwoWire::recoverBus() {
  if (_mode != Mode::Master || _masterActive) {
    _lastError = kErrorBusy;
    return false;
  }

  eclic_irq_disable(IIC0_IRQn);
  registers()->interruptEnable = 0U;
  registers()->interruptClear = 0xffffffffU;
  ++_recoveryCount;

  // Temporarily take the two open-drain pads back as GPIO. A released line is
  // written high; up to nine SCL pulses let a slave advance and release SDA.
  bool configured =
      pinModeOwned(SDA, OUTPUT_OPEN_DRAIN, PeripheralOwner::Wire) &&
      pinModeOwned(SCL, OUTPUT_OPEN_DRAIN, PeripheralOwner::Wire);
  if (configured) {
    digitalWrite(SDA, HIGH);
    digitalWrite(SCL, HIGH);
    delayMicroseconds(5U);
    for (uint8_t pulse = 0U; pulse < 9U && digitalRead(SDA) == LOW; ++pulse) {
      digitalWrite(SCL, LOW);
      delayMicroseconds(5U);
      digitalWrite(SCL, HIGH);
      delayMicroseconds(5U);
    }
    // Generate an explicit STOP: SDA low -> SCL high -> SDA high.
    digitalWrite(SDA, LOW);
    delayMicroseconds(5U);
    digitalWrite(SCL, HIGH);
    delayMicroseconds(5U);
    digitalWrite(SDA, HIGH);
    delayMicroseconds(5U);
  }

  configurePins();
  iic_interrupt_init(IIC0, _frequency / 1000U, 0, LONG_TIME_OUT);
  registers()->interruptEnable = kInterruptTimeout | kInterruptRxReady |
                                 kInterruptTxReady | kInterruptBusError |
                                 kInterruptStop |
                                 kInterruptArbitrationLost;
  _busHeld = false;
  const bool released = configured &&
                        (registers()->status & kStatusBusy) == 0U;
  _lastError = released ? 0U : kErrorRecoveryFailed;
  return released;
}

uint32_t TwoWire::recoveryCount() const { return _recoveryCount; }

bool TwoWire::transferBusy() const {
  return _masterActive || _masterCompletionPending;
}

bool TwoWire::startMasterTransfer(
    uint8_t address, const uint8_t *writeData, size_t writeSize,
    size_t readSize, bool sendStop, TransferCallback callback, void *context,
    void *waiter) {
  if (_mode == Mode::Stopped && !begin()) return false;
  if (_mode != Mode::Master || transferBusy() || _transmitting ||
      address > 0x7fU || writeSize > I2C_BUFFER_LENGTH ||
      readSize > I2C_BUFFER_LENGTH ||
      (writeSize != 0U && writeData == nullptr) ||
      (writeSize == 0U && readSize == 0U && !sendStop)) {
    _lastError = kErrorBusy;
    return false;
  }

  if (!_busHeld && (registers()->status & kStatusBusy) != 0U &&
      !recoverBus()) {
    return false;
  }
  if (writeSize != 0U && writeData != _txBuffer) {
    memcpy(_txBuffer, writeData, writeSize);
  }
  if (callback != nullptr) {
    TickType_t timeoutTicks = static_cast<TickType_t>(
        (static_cast<uint64_t>(effectiveTimeoutMicros()) +
         static_cast<uint64_t>(portTICK_PERIOD_MS) * 1000ULL - 1ULL) /
        (static_cast<uint64_t>(portTICK_PERIOD_MS) * 1000ULL));
    if (timeoutTicks == 0U) timeoutTicks = 1U;
    if (s_masterTimeoutTimer == nullptr) {
      s_masterTimeoutTimer = xTimerCreate("wire-timeout", timeoutTicks,
                                          pdFALSE, nullptr,
                                          masterTimeoutTimer);
    }
    if (s_masterTimeoutTimer == nullptr ||
        xTimerChangePeriod(s_masterTimeoutTimer, timeoutTicks, 0U) != pdPASS) {
      _lastError = kErrorQueueFull;
      return false;
    }
  }

  taskENTER_CRITICAL();
  uint32_t generation = _masterGeneration + 1U;
  if (generation == 0U) generation = 1U;
  _masterGeneration = generation;
  _masterAddress = address;
  _masterTxIndex = 0U;
  _masterTxLength = writeSize;
  _masterRxLength = 0U;
  _masterRxTarget = readSize;
  _masterSendStop = sendStop;
  _masterCallback = callback;
  _masterCallbackContext = context;
  _masterWaiter = waiter;
  _masterResult = 0U;
  _masterCompletionPending = false;
  _masterActive = true;
  const bool readOnly = writeSize == 0U && readSize != 0U;
  _masterState = readOnly ? MasterState::AddressRead
                          : MasterState::AddressWrite;
  taskEXIT_CRITICAL();

  IicRegisters *const reg = registers();
  reg->interruptClear = kInterruptClearAll;
  reg->transmitData = (static_cast<uint32_t>(address) << 1U) |
                      (readOnly ? 1U : 0U);
  reg->command = kCommandStart | kCommandTransfer;
  return true;
}

void TwoWire::issueReadFromIsr() {
  IicRegisters *const reg = registers();
  reg->transmitData = (static_cast<uint32_t>(_masterAddress) << 1U) | 1U;
  _masterState = MasterState::AddressRead;
  reg->command = kCommandStart | kCommandTransfer;
}

void TwoWire::issueStopFromIsr() {
  _masterState = MasterState::Stop;
  registers()->command = kCommandStop | kCommandTransfer;
}

void TwoWire::finishMasterFromIsr(uint8_t result, bool busHeld) {
  _masterResult = result;
  _lastError = result;
  _busHeld = busHeld && result == 0U;
  _masterState = MasterState::Idle;
  _masterActive = false;

  BaseType_t higherPriorityTaskWoken = pdFALSE;
  if (s_masterTimeoutTimer != nullptr) {
    (void)xTimerStopFromISR(s_masterTimeoutTimer, &higherPriorityTaskWoken);
  }
  TaskHandle_t waiter = static_cast<TaskHandle_t>(_masterWaiter);
  _masterWaiter = nullptr;
  if (waiter != nullptr) {
    vTaskNotifyGiveFromISR(waiter, &higherPriorityTaskWoken);
  }
  if (_masterCallback != nullptr) {
    _masterCompletionPending = true;
    if (!chipintelli_arduino_post_event_from_isr(
            masterEvent, this, _masterGeneration)) {
      _masterCompletionPending = false;
      _masterCallback = nullptr;
      _asyncReadData = nullptr;
      ++_callbackDrops;
      _lastError = kErrorQueueFull;
    }
  }
  portYIELD_FROM_ISR(higherPriorityTaskWoken);
}

void TwoWire::handleMasterTimeout() {
  eclic_irq_disable(IIC0_IRQn);
  if (!_masterActive || _masterCallback == nullptr) {
    eclic_irq_enable(IIC0_IRQn);
    return;
  }
  registers()->command = kCommandStop | kCommandTransfer;
  registers()->interruptClear = kInterruptClearAll;
  _timeoutFlag = true;
  _masterResult = kErrorTimeout;
  _lastError = kErrorTimeout;
  _masterState = MasterState::Idle;
  _masterActive = false;
  _busHeld = false;
  _masterCompletionPending = true;
  if (!chipintelli_arduino_post_event(masterEvent, this,
                                       _masterGeneration)) {
    _masterCompletionPending = false;
    _masterCallback = nullptr;
    _masterCallbackContext = nullptr;
    _asyncReadData = nullptr;
    ++_callbackDrops;
    _lastError = kErrorQueueFull;
  }
  eclic_clear_pending(IIC0_IRQn);
  eclic_irq_enable(IIC0_IRQn);
}

bool TwoWire::handleMasterInterrupt() {
  if (_mode != Mode::Master) return false;

  IicRegisters *const reg = registers();
  const uint32_t interrupts = reg->interruptStatus;
  const uint32_t status = reg->status;
  reg->interruptClear = interrupts != 0U ? interrupts : kInterruptClearAll;

  if (!_masterActive) return true;
  if ((interrupts & kInterruptTimeout) != 0U) {
    reg->command = kCommandStop | kCommandTransfer;
    _timeoutFlag = true;
    finishMasterFromIsr(kErrorTimeout, false);
    return true;
  }
  if ((interrupts & kInterruptArbitrationLost) != 0U ||
      (status & kStatusArbitrationLost) != 0U) {
    reg->command = kCommandStop | kCommandTransfer;
    finishMasterFromIsr(kErrorArbitrationLost, false);
    return true;
  }
  if ((interrupts & kInterruptBusError) != 0U ||
      (status & kStatusTransferError) != 0U) {
    reg->command = kCommandStop | kCommandTransfer;
    finishMasterFromIsr(kErrorOther, false);
    return true;
  }

  switch (_masterState) {
    case MasterState::AddressWrite:
      if ((interrupts & kInterruptTxReady) == 0U) break;
      if ((status & kStatusNack) != 0U) {
        reg->command = kCommandStop | kCommandTransfer;
        finishMasterFromIsr(2U, false);
      } else {
        if (_masterTxLength != 0U) {
          reg->transmitData = _txBuffer[0];
          _masterState = MasterState::Write;
          reg->command = kCommandTransfer;
        } else if (_masterRxTarget != 0U) {
          issueReadFromIsr();
        } else if (_masterSendStop) {
          issueStopFromIsr();
        } else {
          finishMasterFromIsr(0U, true);
        }
      }
      break;

    case MasterState::Write:
      if ((interrupts & kInterruptTxReady) == 0U) break;
      if ((status & kStatusNack) != 0U) {
        reg->command = kCommandStop | kCommandTransfer;
        finishMasterFromIsr(3U, false);
        break;
      }
      ++_masterTxIndex;
      if (_masterTxIndex < _masterTxLength) {
        reg->transmitData = _txBuffer[_masterTxIndex];
        reg->command = kCommandTransfer;
      } else if (_masterRxTarget != 0U) {
        issueReadFromIsr();
      } else if (_masterSendStop) {
        issueStopFromIsr();
      } else {
        finishMasterFromIsr(0U, true);
      }
      break;

    case MasterState::AddressRead:
      if ((interrupts & kInterruptTxReady) == 0U) break;
      if ((status & kStatusNack) != 0U) {
        reg->command = kCommandStop | kCommandTransfer;
        finishMasterFromIsr(2U, false);
        break;
      }
      (void)reg->receiveData;  // Required dummy read after the address phase.
      _masterState = MasterState::Read;
      reg->command = kCommandTransfer |
                     (_masterRxTarget == 1U ? kCommandNack : 0U);
      break;

    case MasterState::Read:
      if ((interrupts & kInterruptRxReady) == 0U) break;
      if (_masterRxLength < _masterRxTarget) {
        _rxBuffer[_masterRxLength++] =
            static_cast<uint8_t>(reg->receiveData);
      }
      if (_masterRxLength < _masterRxTarget) {
        const bool last = _masterRxLength + 1U == _masterRxTarget;
        reg->command = kCommandTransfer | (last ? kCommandNack : 0U);
      } else if (_masterSendStop) {
        issueStopFromIsr();
      } else {
        finishMasterFromIsr(0U, true);
      }
      break;

    case MasterState::Stop:
      if ((interrupts & kInterruptStop) != 0U ||
          (status & kStatusBusy) == 0U) {
        finishMasterFromIsr(0U, false);
      }
      break;

    case MasterState::Idle:
      break;
  }
  return true;
}

bool TwoWire::waitMasterTransfer() {
  const uint32_t started = micros();
  const uint32_t timeout = effectiveTimeoutMicros();
  while (_masterActive) {
    const uint32_t elapsed = micros() - started;
    if (elapsed >= timeout) break;
    const uint32_t remainingUs = timeout - elapsed;
    TickType_t wait = static_cast<TickType_t>(
        (static_cast<uint64_t>(remainingUs) +
         static_cast<uint64_t>(portTICK_PERIOD_MS) * 1000ULL - 1ULL) /
        (static_cast<uint64_t>(portTICK_PERIOD_MS) * 1000ULL));
    (void)ulTaskNotifyTake(pdTRUE, wait > 0U ? wait : 1U);
  }
  if (_masterActive) {
    eclic_irq_disable(IIC0_IRQn);
    registers()->command = kCommandStop | kCommandTransfer;
    registers()->interruptClear = kInterruptClearAll;
    _masterActive = false;
    _masterState = MasterState::Idle;
    _masterWaiter = nullptr;
    eclic_clear_pending(IIC0_IRQn);
    eclic_irq_enable(IIC0_IRQn);
    recoverFromTimeout();
    return false;
  }
  if (_masterResult == kErrorTimeout && _resetOnTimeout) {
    const uint8_t result = _masterResult;
    (void)recoverBus();
    _lastError = result;
  }
  return _masterResult == 0U;
}

void TwoWire::masterEvent(void *context, uint32_t generation) {
  TwoWire *wire = static_cast<TwoWire *>(context);
  if (wire == nullptr) return;

  TransferCallback callback = nullptr;
  void *callbackContext = nullptr;
  uint8_t *destination = nullptr;
  size_t received = 0U;
  size_t transferred = 0U;
  uint8_t result = kErrorOther;
  taskENTER_CRITICAL();
  if (wire->_masterCompletionPending &&
      wire->_masterGeneration == generation) {
    callback = wire->_masterCallback;
    callbackContext = wire->_masterCallbackContext;
    destination = wire->_asyncReadData;
    received = wire->_masterRxLength;
    transferred = received != 0U ? received : wire->_masterTxIndex;
    result = wire->_masterResult;
    wire->_masterCompletionPending = false;
    wire->_masterCallback = nullptr;
    wire->_masterCallbackContext = nullptr;
    wire->_asyncReadData = nullptr;
  }
  taskEXIT_CRITICAL();

  if (destination != nullptr && received != 0U) {
    memcpy(destination, wire->_rxBuffer, received);
  }
  if (result == kErrorTimeout && wire->_resetOnTimeout) {
    (void)wire->recoverBus();
    wire->_lastError = result;
  }
  if (callback != nullptr) callback(result, transferred, callbackContext);
}

bool TwoWire::transferAsync(
    uint8_t address, const uint8_t *writeData, size_t writeSize,
    uint8_t *readData, size_t readSize, bool sendStop,
    TransferCallback callback, void *context) {
  if (callback == nullptr || (readSize != 0U && readData == nullptr) ||
      xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
    _lastError = kErrorOther;
    return false;
  }
  _asyncReadData = readData;
  if (!startMasterTransfer(address, writeData, writeSize, readSize, sendStop,
                           callback, context, nullptr)) {
    _asyncReadData = nullptr;
    return false;
  }
  return true;
}

bool TwoWire::cancelTransfer() {
  if (!_masterActive && !_masterCompletionPending && !_busHeld) return true;
  eclic_irq_disable(IIC0_IRQn);
  if (s_masterTimeoutTimer != nullptr &&
      xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
    (void)xTimerStop(s_masterTimeoutTimer, 0U);
  }
  registers()->command = kCommandStop | kCommandTransfer;
  registers()->interruptClear = kInterruptClearAll;
  taskENTER_CRITICAL();
  _masterActive = false;
  _masterCompletionPending = false;
  _masterState = MasterState::Idle;
  _masterWaiter = nullptr;
  _masterCallback = nullptr;
  _masterCallbackContext = nullptr;
  _asyncReadData = nullptr;
  ++_masterGeneration;
  _busHeld = false;
  taskEXIT_CRITICAL();
  eclic_clear_pending(IIC0_IRQn);
  eclic_irq_enable(IIC0_IRQn);
  resetController();
  _lastError = kErrorOther;
  return true;
}

bool TwoWire::probe(uint8_t address) {
  if (address > 0x7fU || _transmitting || transferBusy() ||
      (_mode != Mode::Stopped && _mode != Mode::Master)) {
    _lastError = kErrorBusy;
    return false;
  }
  if (_mode == Mode::Stopped && !begin()) return false;
  TaskHandle_t waiter = xTaskGetCurrentTaskHandle();
  (void)ulTaskNotifyTake(pdTRUE, 0U);
  return startMasterTransfer(address, nullptr, 0U, 0U, true, nullptr,
                             nullptr, waiter) &&
         waitMasterTransfer();
}

void TwoWire::beginTransmission(uint8_t address) {
  if (_mode == Mode::Stopped && !begin()) return;
  if (_mode != Mode::Master) {
    _lastError = 4;
    return;
  }
  if (transferBusy()) {
    _lastError = kErrorBusy;
    return;
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
  TaskHandle_t waiter = xTaskGetCurrentTaskHandle();
  (void)ulTaskNotifyTake(pdTRUE, 0U);
  if (!startMasterTransfer(_txAddress, _txBuffer, _txLength, 0U, sendStop,
                           nullptr, nullptr, waiter)) {
    return _lastError;
  }
  (void)waitMasterTransfer();
  _txLength = 0U;
  return _lastError;
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
    _lastError = 0;
    return 0;
  }
  if (quantity > I2C_BUFFER_LENGTH) quantity = I2C_BUFFER_LENGTH;
  address &= 0x7fU;

  TaskHandle_t waiter = xTaskGetCurrentTaskHandle();
  (void)ulTaskNotifyTake(pdTRUE, 0U);
  if (!startMasterTransfer(address, nullptr, 0U, quantity, sendStop, nullptr,
                           nullptr, waiter)) {
    return 0U;
  }
  (void)waitMasterTransfer();
  _rxLength = _masterRxLength;
  _rxIndex = 0U;
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

void TwoWire::onReceive(void (*callback)(int)) {
  taskENTER_CRITICAL();
  // Invalidate any event queued for the previous registration. A newly
  // installed callback must never receive an older slave transaction.
  if (++_receiveGeneration == 0U) ++_receiveGeneration;
  _receiveCallbackPending = false;
  _onReceive = callback;
  _onReceiveISR = nullptr;
  taskEXIT_CRITICAL();
}

void TwoWire::onRequest(void (*callback)(void)) {
  // A slave must provide the first response byte before the controller can
  // release clock stretching, so this compatibility API is necessarily ISR.
  onRequestISR(callback);
}

void TwoWire::onReceiveISR(void (*callback)(int)) {
  taskENTER_CRITICAL();
  if (++_receiveGeneration == 0U) ++_receiveGeneration;
  _receiveCallbackPending = false;
  _onReceiveISR = callback;
  _onReceive = nullptr;
  taskEXIT_CRITICAL();
}

void TwoWire::onRequestISR(void (*callback)(void)) {
  taskENTER_CRITICAL();
  _onRequest = callback;
  taskEXIT_CRITICAL();
}

uint32_t TwoWire::callbackDrops() const {
  taskENTER_CRITICAL();
  const uint32_t drops = _callbackDrops;
  taskEXIT_CRITICAL();
  return drops;
}

void TwoWire::receiveEvent(void *context, uint32_t value) {
  TwoWire *wire = static_cast<TwoWire *>(context);
  if (wire == nullptr) return;
  void (*callback)(int) = nullptr;
  size_t received = 0U;
  taskENTER_CRITICAL();
  if (wire->_mode == Mode::Slave && wire->_receiveCallbackPending &&
      wire->_pendingReceiveGeneration == value &&
      wire->_receiveGeneration == value) {
    callback = wire->_onReceive;
    received = wire->_rxLength;
  }
  taskEXIT_CRITICAL();

  if (callback != nullptr) callback(static_cast<int>(received));

  taskENTER_CRITICAL();
  if (wire->_receiveCallbackPending &&
      wire->_pendingReceiveGeneration == value) {
    wire->_receiveCallbackPending = false;
  }
  taskEXIT_CRITICAL();
}

void TwoWire::finishSlaveReceive() {
  if (_slaveRxLength == 0) return;
  const size_t received = _slaveRxLength;
  _slaveRxLength = 0;
  if (_receiveCallbackPending) {
    ++_callbackDrops;
    return;
  }
  memcpy(_rxBuffer, _slaveRxBuffer, received);
  _rxLength = received;
  _rxIndex = 0;
  if (_onReceiveISR != nullptr) {
    _onReceiveISR(static_cast<int>(received));
    return;
  }
  if (_onReceive == nullptr) return;
  _receiveCallbackPending = true;
  _pendingReceiveGeneration = _receiveGeneration;
  if (!chipintelli_arduino_post_event_from_isr(
          receiveEvent, this, _pendingReceiveGeneration)) {
    _receiveCallbackPending = false;
    ++_callbackDrops;
  }
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
  _slaveRxBuffer[_slaveRxLength++] = static_cast<uint8_t>(data);
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
