#include "Arduino.h"
#include "ArduinoEvent.h"
#include "PeripheralManager.h"

#include <string.h>

extern "C" {
#include "FreeRTOS.h"
#include "task.h"
#include "ci130x_core_eclic.h"
#include "ci130x_uart.h"
}

namespace {
constexpr uint16_t kRxMask = SERIAL_RX_BUFFER_SIZE - 1U;
constexpr uint16_t kTxMask = SERIAL_TX_BUFFER_SIZE - 1U;
#ifndef SERIAL_WRITE_TIMEOUT_MS
constexpr uint32_t kDefaultWriteTimeoutMs = 1000U;
#else
constexpr uint32_t kDefaultWriteTimeoutMs = SERIAL_WRITE_TIMEOUT_MS;
#endif

TickType_t timeoutTicks(uint32_t timeoutMs) {
    if (timeoutMs == 0U) return 0U;
    const uint64_t ticks =
        (static_cast<uint64_t>(timeoutMs) + portTICK_PERIOD_MS - 1U) /
        portTICK_PERIOD_MS;
    return static_cast<TickType_t>(ticks > portMAX_DELAY ? portMAX_DELAY
                                                         : (ticks ? ticks : 1U));
}

UART_TypeDef *uartForNumber(uint8_t number) {
    switch (number) {
        case 1: return UART1;
        case 2: return UART2;
        default: return UART0;
    }
}

uint32_t irqForNumber(uint8_t number) {
    switch (number) {
        case 1: return UART1_IRQn;
        case 2: return UART2_IRQn;
        default: return UART0_IRQn;
    }
}

PeripheralOwner ownerForNumber(uint8_t number) {
    switch (number) {
        case 1: return PeripheralOwner::Serial1;
        case 2: return PeripheralOwner::Serial2;
        default: return PeripheralOwner::Serial0;
    }
}

PeripheralResource resourceForNumber(uint8_t number) {
    switch (number) {
        case 1: return PeripheralResource::Uart1;
        case 2: return PeripheralResource::Uart2;
        default: return PeripheralResource::Uart0;
    }
}

void pinsForNumber(uint8_t number, uint8_t (&pins)[2]) {
    switch (number) {
        case 1:
            pins[0] = TX1;
            pins[1] = RX1;
            break;
        case 2:
            pins[0] = TX2;
            pins[1] = RX2;
            break;
        default:
            pins[0] = TX;
            pins[1] = RX;
            break;
    }
}

bool supportedBaud(unsigned long baud) {
    switch (baud) {
        case 2400: case 4800: case 9600: case 19200: case 38400:
        case 57600: case 115200: case 230400: case 380400: case 460800:
        case 921600: case 1000000: case 2000000: case 3000000:
            return true;
        default:
            return false;
    }
}

bool decodeConfig(uint32_t config, UART_WordLength &wordLength,
                  UART_StopBits &stopBits, UART_Parity &parity) {
    switch (config & SERIAL_DATA_MASK) {
        case SERIAL_DATA_5: wordLength = UART_WordLength_5b; break;
        case SERIAL_DATA_6: wordLength = UART_WordLength_6b; break;
        case SERIAL_DATA_7: wordLength = UART_WordLength_7b; break;
        case SERIAL_DATA_8: wordLength = UART_WordLength_8b; break;
        default: return false;
    }
    switch (config & SERIAL_STOP_BIT_MASK) {
        case SERIAL_STOP_BIT_1: stopBits = UART_StopBits_1; break;
        case SERIAL_STOP_BIT_1_5: stopBits = UART_StopBits_1_5; break;
        case SERIAL_STOP_BIT_2: stopBits = UART_StopBits_2; break;
        default: return false;
    }
    switch (config & SERIAL_PARITY_MASK) {
        case SERIAL_PARITY_NONE: parity = UART_Parity_No; break;
        case SERIAL_PARITY_ODD: parity = UART_Parity_Odd; break;
        case SERIAL_PARITY_EVEN: parity = UART_Parity_Even; break;
        default: return false;  // CI130X has no mark/space parity mode.
    }
    return (config & ~(SERIAL_DATA_MASK | SERIAL_STOP_BIT_MASK |
                       SERIAL_PARITY_MASK)) == 0U;
}
}  // namespace

HardwareSerial Serial(0);
HardwareSerial Serial1(1);
HardwareSerial Serial2(2);

HardwareSerial::HardwareSerial(uint8_t uartNumber)
    : _uartNumber(uartNumber),
      _rxHead(0),
      _rxTail(0),
      _txHead(0),
      _txTail(0),
      _rxBuffer{},
      _txBuffer{},
      _errorCounts{},
      _rxWaiter(nullptr),
      _txWaiter(nullptr),
      _lastError(HardwareSerialStartError::None),
      _started(false) {}

void HardwareSerial::begin(unsigned long baud, uint32_t config) {
    if (!supportedBaud(baud)) {
        _lastError = HardwareSerialStartError::UnsupportedBaud;
        return;
    }

    UART_WordLength wordLength;
    UART_StopBits stopBits;
    UART_Parity parity;
    if (!decodeConfig(config, wordLength, stopBits, parity)) {
        _lastError = HardwareSerialStartError::UnsupportedConfig;
        return;
    }

    if (_started) {
        end();
    }

    uint8_t pins[2];
    pinsForNumber(_uartNumber, pins);
    const PeripheralOwner owner = ownerForNumber(_uartNumber);
    const PeripheralResource resource = resourceForNumber(_uartNumber);
    if (!PeripheralManager.claim(owner, pins, 2, &resource, 1)) {
        _lastError = HardwareSerialStartError::ResourceBusy;
        return;
    }
    detachInterrupt(pins[0]);
    detachInterrupt(pins[1]);

    UART_TypeDef *uart = uartForNumber(_uartNumber);
    const uint32_t irq = irqForNumber(_uartNumber);
    eclic_irq_disable(irq);
    UARTInterruptConfig(uart, static_cast<UART_BaudRate>(baud));
    eclic_irq_disable(irq);
    UART_EN(uart, DISABLE);
    UART_IntMaskConfig(uart, UART_AllInt, ENABLE);
    UART_IntClear(uart, UART_AllInt);
    UART_FIFOClear(uart);
    UART_TXFIFOByteWordConfig(uart, UART_Byte);
    UART_LCRConfig(uart, wordLength, stopBits, parity);
    UART_RXFIFOConfig(uart, UART_FIFOLevel1);
    UART_TXFIFOConfig(uart, UART_FIFOLevel1_8);
    UART_TimeoutConfig(uart, 32);
    UART_CRConfig(uart, UART_TXE, ENABLE);
    UART_CRConfig(uart, UART_RXE, ENABLE);
    UART_CRConfig(uart, UART_NCED, ENABLE);

    _rxHead = _rxTail = 0;
    _txHead = _txTail = 0;
    _rxWaiter = nullptr;
    _txWaiter = nullptr;
    clearErrorCounts();
    _lastError = HardwareSerialStartError::None;
    _started = true;

    UART_IntMaskConfig(uart, UART_RXInt, DISABLE);
    UART_IntMaskConfig(uart, UART_RXTimeoutInt, DISABLE);
    UART_IntMaskConfig(uart, UART_FramingErrorInt, DISABLE);
    UART_IntMaskConfig(uart, UART_ParityErrorInt, DISABLE);
    UART_IntMaskConfig(uart, UART_BreakErrorInt, DISABLE);
    UART_IntMaskConfig(uart, UART_OverrunErrorInt, DISABLE);
    UART_EN(uart, ENABLE);
    eclic_clear_pending(irq);
    eclic_irq_enable(irq);
}

void HardwareSerial::end() {
    if (!_started) return;
    (void)flush(kDefaultWriteTimeoutMs);

    UART_TypeDef *uart = uartForNumber(_uartNumber);
    eclic_irq_disable(irqForNumber(_uartNumber));
    UART_IntMaskConfig(uart, UART_AllInt, ENABLE);
    UART_EN(uart, DISABLE);
    UART_IntClear(uart, UART_AllInt);
    UART_FIFOClear(uart);
    UART_CRConfig(uart, UART_TXE, DISABLE);
    UART_CRConfig(uart, UART_RXE, DISABLE);
    _started = false;
    _rxHead = _rxTail = 0;
    _txHead = _txTail = 0;
    TaskHandle_t rxWaiter = static_cast<TaskHandle_t>(_rxWaiter);
    TaskHandle_t txWaiter = static_cast<TaskHandle_t>(_txWaiter);
    _rxWaiter = nullptr;
    _txWaiter = nullptr;
    if (rxWaiter != nullptr) xTaskNotifyGive(rxWaiter);
    if (txWaiter != nullptr && txWaiter != rxWaiter) xTaskNotifyGive(txWaiter);

    uint8_t pins[2];
    pinsForNumber(_uartNumber, pins);
    const PeripheralOwner owner = ownerForNumber(_uartNumber);
    (void)pinModeOwned(pins[0], INPUT, owner);
    (void)pinModeOwned(pins[1], INPUT, owner);
    const PeripheralResource resource = resourceForNumber(_uartNumber);
    PeripheralManager.release(owner, pins, 2, &resource, 1);
}

uint16_t HardwareSerial::rxCount() const {
    return static_cast<uint16_t>((_rxHead - _rxTail) & kRxMask);
}

uint16_t HardwareSerial::txCount() const {
    return static_cast<uint16_t>((_txHead - _txTail) & kTxMask);
}

int HardwareSerial::available() {
    return _started ? rxCount() : 0;
}

int HardwareSerial::availableForWrite() {
    return _started ? static_cast<int>(kTxMask - txCount()) : 0;
}

int HardwareSerial::peek() {
    if (!_started || _rxHead == _rxTail) return -1;
    return _rxBuffer[_rxTail];
}

int HardwareSerial::read() {
    if (!_started) return -1;
    taskENTER_CRITICAL();
    if (_rxHead == _rxTail) {
        taskEXIT_CRITICAL();
        return -1;
    }
    const uint8_t value = _rxBuffer[_rxTail];
    _rxTail = static_cast<uint16_t>((_rxTail + 1U) & kRxMask);
    taskEXIT_CRITICAL();
    return value;
}

void HardwareSerial::pumpTxLocked() {
    UART_TypeDef *const uart = uartForNumber(_uartNumber);
    while (_txTail != _txHead && !UART_FLAGSTAT(uart, UART_TXFF)) {
        UART_TXDATAConfig(uart, _txBuffer[_txTail]);
        _txTail = static_cast<uint16_t>((_txTail + 1U) & kTxMask);
    }
    UART_IntMaskConfig(uart, UART_TXInt,
                       _txHead == _txTail ? ENABLE : DISABLE);
}

bool HardwareSerial::waitForData(unsigned long timeoutMs) {
    if (!_started || available() > 0) return available() > 0;
    if (timeoutMs == 0U ||
        xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
        return false;
    }

    TaskHandle_t current = xTaskGetCurrentTaskHandle();
    taskENTER_CRITICAL();
    if (_rxHead != _rxTail) {
        taskEXIT_CRITICAL();
        return true;
    }
    if (_rxWaiter != nullptr && _rxWaiter != current) {
        taskEXIT_CRITICAL();
        _lastError = HardwareSerialStartError::Busy;
        delay(timeoutMs > portTICK_PERIOD_MS ? portTICK_PERIOD_MS : timeoutMs);
        return available() > 0;
    }
    _rxWaiter = current;
    taskEXIT_CRITICAL();

    (void)ulTaskNotifyTake(pdTRUE, timeoutTicks(timeoutMs));
    taskENTER_CRITICAL();
    if (_rxWaiter == current) _rxWaiter = nullptr;
    const bool ready = _rxHead != _rxTail;
    taskEXIT_CRITICAL();
    return ready;
}

bool HardwareSerial::waitForTxSpace(uint32_t timeoutMs) {
    if (!_started || availableForWrite() > 0) return availableForWrite() > 0;
    if (timeoutMs == 0U ||
        xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
        return false;
    }

    TaskHandle_t current = xTaskGetCurrentTaskHandle();
    taskENTER_CRITICAL();
    pumpTxLocked();
    if (static_cast<uint16_t>((_txHead + 1U) & kTxMask) != _txTail) {
        taskEXIT_CRITICAL();
        return true;
    }
    if (_txWaiter != nullptr && _txWaiter != current) {
        taskEXIT_CRITICAL();
        _lastError = HardwareSerialStartError::Busy;
        return false;
    }
    _txWaiter = current;
    taskEXIT_CRITICAL();

    (void)ulTaskNotifyTake(pdTRUE, timeoutTicks(timeoutMs));
    taskENTER_CRITICAL();
    if (_txWaiter == current) _txWaiter = nullptr;
    pumpTxLocked();
    const bool ready = static_cast<uint16_t>((_txHead + 1U) & kTxMask) !=
                       _txTail;
    taskEXIT_CRITICAL();
    return ready;
}

void HardwareSerial::flush() { (void)flush(kDefaultWriteTimeoutMs); }

bool HardwareSerial::flush(uint32_t timeoutMs) {
    if (!_started) return true;
    const uint32_t started = millis();
    for (;;) {
        taskENTER_CRITICAL();
        pumpTxLocked();
        const bool complete = _txHead == _txTail &&
                              UART_FLAGSTAT(uartForNumber(_uartNumber),
                                            UART_TXFE);
        taskEXIT_CRITICAL();
        if (!_started || complete) {
            _lastError = HardwareSerialStartError::None;
            return true;
        }
        const uint32_t elapsed = millis() - started;
        if (elapsed >= timeoutMs) {
            _lastError = HardwareSerialStartError::Timeout;
            return false;
        }
        (void)waitForTxSpace(timeoutMs - elapsed);
        // Once the software ring is empty, no further TX-space interrupt is
        // guaranteed. Sleep cooperatively while the final FIFO bytes drain.
        delay(1U);
    }
}

size_t HardwareSerial::tryWrite(uint8_t value) {
    return tryWrite(&value, 1U);
}

size_t HardwareSerial::tryWrite(const uint8_t *buffer, size_t size) {
    if (buffer == nullptr || size == 0U || !_started) return 0U;

    taskENTER_CRITICAL();
    UART_TypeDef *const uart = uartForNumber(_uartNumber);
    pumpTxLocked();
    size_t written = 0U;

    // Feed an idle hardware FIFO directly before filling the software ring.
    while (_txHead == _txTail && written < size &&
           !UART_FLAGSTAT(uart, UART_TXFF)) {
        UART_TXDATAConfig(uart, buffer[written++]);
    }

    while (written < size) {
        const uint16_t next = static_cast<uint16_t>((_txHead + 1U) & kTxMask);
        if (next == _txTail) break;
        const uint16_t contiguous = _txHead < _txTail
                                        ? static_cast<uint16_t>(_txTail - _txHead - 1U)
                                        : static_cast<uint16_t>(SERIAL_TX_BUFFER_SIZE - _txHead -
                                                                (_txTail == 0U ? 1U : 0U));
        size_t chunk = size - written;
        if (chunk > contiguous) chunk = contiguous;
        if (chunk == 0U) break;
        memcpy(&_txBuffer[_txHead], &buffer[written], chunk);
        _txHead = static_cast<uint16_t>((_txHead + chunk) & kTxMask);
        written += chunk;
    }
    if (_txHead != _txTail) UART_IntMaskConfig(uart, UART_TXInt, DISABLE);
    taskEXIT_CRITICAL();
    return written;
}

size_t HardwareSerial::write(uint8_t value) {
    return write(value, kDefaultWriteTimeoutMs);
}

size_t HardwareSerial::write(uint8_t value, uint32_t timeoutMs) {
    return write(&value, 1U, timeoutMs);
}

size_t HardwareSerial::write(const uint8_t *buffer, size_t size) {
    return write(buffer, size, kDefaultWriteTimeoutMs);
}

size_t HardwareSerial::write(const uint8_t *buffer, size_t size,
                             uint32_t timeoutMs) {
    if (buffer == nullptr || !_started) return 0U;
    _lastError = HardwareSerialStartError::None;
    const uint32_t started = millis();
    size_t written = 0U;
    while (written < size) {
        written += tryWrite(buffer + written, size - written);
        if (written == size) {
            _lastError = HardwareSerialStartError::None;
            break;
        }
        const uint32_t elapsed = millis() - started;
        if (elapsed >= timeoutMs) {
            _lastError = HardwareSerialStartError::Timeout;
            break;
        }
        if (!waitForTxSpace(timeoutMs - elapsed) &&
            _lastError == HardwareSerialStartError::Busy) {
            break;
        }
    }
    return written;
}

HardwareSerialErrorCounts HardwareSerial::errorCounts() const {
    taskENTER_CRITICAL();
    const HardwareSerialErrorCounts result = {
        _errorCounts.bufferOverflow, _errorCounts.hardwareOverrun,
        _errorCounts.framing, _errorCounts.parity,
        _errorCounts.breakCondition};
    taskEXIT_CRITICAL();
    return result;
}

void HardwareSerial::clearErrorCounts() {
    taskENTER_CRITICAL();
    _errorCounts.bufferOverflow = 0;
    _errorCounts.hardwareOverrun = 0;
    _errorCounts.framing = 0;
    _errorCounts.parity = 0;
    _errorCounts.breakCondition = 0;
    taskEXIT_CRITICAL();
}

void HardwareSerial::handleInterrupt() {
    UART_TypeDef *uart = uartForNumber(_uartNumber);
    if (!_started) {
        UART_IntClear(uart, UART_AllInt);
        return;
    }

    const bool rxWasEmpty = _rxHead == _rxTail;
    bool overrunReportedByData = false;
    while (!UART_FLAGSTAT(uart, UART_RXFE)) {
        const uint32_t raw = uart->UARTRdDR;
        if (raw & (1U << (UART_FramingError + 8))) ++_errorCounts.framing;
        if (raw & (1U << (UART_ParityError + 8))) ++_errorCounts.parity;
        if (raw & (1U << (UART_BreakError + 8))) ++_errorCounts.breakCondition;
        if (raw & (1U << (UART_OverrunError + 8))) {
            ++_errorCounts.hardwareOverrun;
            overrunReportedByData = true;
        }

        const uint16_t next = static_cast<uint16_t>((_rxHead + 1U) & kRxMask);
        if (next == _rxTail) {
            ++_errorCounts.bufferOverflow;
        } else {
            _rxBuffer[_rxHead] = static_cast<uint8_t>(raw);
            _rxHead = next;
        }
    }

    if (!overrunReportedByData &&
        UART_MaskIntState(uart, UART_OverrunErrorInt)) {
        ++_errorCounts.hardwareOverrun;
    }

    bool txSpaceCreated = false;
    if (UART_MaskIntState(uart, UART_TXInt)) {
        const uint16_t oldTail = _txTail;
        while (_txTail != _txHead && !UART_FLAGSTAT(uart, UART_TXFF)) {
            UART_TXDATAConfig(uart, _txBuffer[_txTail]);
            _txTail = static_cast<uint16_t>((_txTail + 1U) & kTxMask);
        }
        if (_txTail == _txHead) {
            UART_IntMaskConfig(uart, UART_TXInt, ENABLE);
        }
        txSpaceCreated = oldTail != _txTail;
    }
    UART_IntClear(uart, UART_AllInt);

    BaseType_t higherPriorityTaskWoken = pdFALSE;
    if (rxWasEmpty && _rxHead != _rxTail) {
        TaskHandle_t waiter = static_cast<TaskHandle_t>(_rxWaiter);
        if (waiter != nullptr) {
            vTaskNotifyGiveFromISR(waiter, &higherPriorityTaskWoken);
        }
        chipintelli_arduino_wake_from_isr();
    }
    if (txSpaceCreated) {
        TaskHandle_t waiter = static_cast<TaskHandle_t>(_txWaiter);
        if (waiter != nullptr) {
            vTaskNotifyGiveFromISR(waiter, &higherPriorityTaskWoken);
        }
    }
    portYIELD_FROM_ISR(higherPriorityTaskWoken);
}

extern "C" void UART0_IRQHandler(void) { Serial.handleInterrupt(); }
extern "C" void UART1_IRQHandler(void) { Serial1.handleInterrupt(); }
extern "C" void UART2_IRQHandler(void) { Serial2.handleInterrupt(); }

void serialEvent(void) __attribute__((weak));
void serialEvent1(void) __attribute__((weak));
void serialEvent2(void) __attribute__((weak));

void serialEventRun(void) {
    if (serialEvent && Serial.available()) serialEvent();
    if (serialEvent1 && Serial1.available()) serialEvent1();
    if (serialEvent2 && Serial2.available()) serialEvent2();
}
