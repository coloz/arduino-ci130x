#include "Arduino.h"

extern "C" {
#include "ci130x_core_eclic.h"
#include "ci130x_uart.h"
}

static UART_TypeDef *uartForNumber(uint8_t number) {
    switch (number) {
        case 1: return UART1;
        case 2: return UART2;
        default: return UART0;
    }
}

static uint32_t irqForNumber(uint8_t number) {
    switch (number) {
        case 1: return UART1_IRQn;
        case 2: return UART2_IRQn;
        default: return UART0_IRQn;
    }
}

static void releasePins(uint8_t number) {
    switch (number) {
        case 1:
            pinMode(TX1, INPUT);
            pinMode(RX1, INPUT);
            break;
        case 2:
            pinMode(TX2, INPUT);
            pinMode(RX2, INPUT);
            break;
        default:
            pinMode(TX, INPUT);
            pinMode(RX, INPUT);
            break;
    }
}

static bool supportedBaud(unsigned long baud) {
    switch (baud) {
        case 2400: case 4800: case 9600: case 19200: case 38400:
        case 57600: case 115200: case 230400: case 380400: case 460800:
        case 921600: case 1000000: case 2000000: case 3000000:
            return true;
        default:
            return false;
    }
}

HardwareSerial::HardwareSerial(uint8_t uartNumber)
    : _uartNumber(uartNumber), _peek(-1), _started(false) {}

void HardwareSerial::begin(unsigned long baud, uint32_t config) {
    (void)config;
    if (!supportedBaud(baud)) {
        baud = 115200;
    }
    UARTPollingConfig(uartForNumber(_uartNumber), static_cast<UART_BaudRate>(baud));
    _peek = -1;
    _started = true;
}

void HardwareSerial::end() {
    if (!_started) return;
    flush();
    UART_TypeDef *uart = uartForNumber(_uartNumber);
    eclic_irq_disable(irqForNumber(_uartNumber));
    UART_IntMaskConfig(uart, UART_AllInt, ENABLE);
    UART_EN(uart, DISABLE);
    UART_IntClear(uart, UART_AllInt);
    UART_FIFOClear(uart);
    UART_CRConfig(uart, UART_TXE, DISABLE);
    UART_CRConfig(uart, UART_RXE, DISABLE);
    releasePins(_uartNumber);
    _peek = -1;
    _started = false;
}

int HardwareSerial::available() {
    if (!_started) return 0;
    if (_peek >= 0) return 1;
    return UART_FLAGSTAT(uartForNumber(_uartNumber), UART_RXFE) ? 0 : 1;
}

int HardwareSerial::availableForWrite() {
    if (!_started) return 0;
    return UART_FLAGSTAT(uartForNumber(_uartNumber), UART_TXFF) ? 0 : 1;
}

int HardwareSerial::peek() {
    if (!_started) return -1;
    if (_peek < 0 && available()) {
        _peek = static_cast<uint8_t>(UartPollingReceiveData(uartForNumber(_uartNumber)));
    }
    return _peek;
}

int HardwareSerial::read() {
    if (!_started) return -1;
    if (_peek >= 0) {
        int value = _peek;
        _peek = -1;
        return value;
    }
    if (!available()) return -1;
    return static_cast<uint8_t>(UartPollingReceiveData(uartForNumber(_uartNumber)));
}

void HardwareSerial::flush() {
    if (_started) UartPollingSenddone(uartForNumber(_uartNumber));
}

size_t HardwareSerial::write(uint8_t value) {
    if (!_started) return 0;
    UartPollingSenddata(uartForNumber(_uartNumber), static_cast<char>(value));
    return 1;
}

size_t HardwareSerial::write(const uint8_t *buffer, size_t size) {
    if (!buffer || !_started) return 0;
    size_t written = 0;
    for (; written < size; ++written) {
        if (write(buffer[written]) != 1) break;
    }
    return written;
}

HardwareSerial Serial(0);
HardwareSerial Serial1(1);
HardwareSerial Serial2(2);
