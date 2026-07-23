#ifndef HardwareSerial_h
#define HardwareSerial_h

#include "Stream.h"

#ifndef SERIAL_RX_BUFFER_SIZE
#define SERIAL_RX_BUFFER_SIZE 128
#endif

#ifndef SERIAL_TX_BUFFER_SIZE
#define SERIAL_TX_BUFFER_SIZE 128
#endif

static_assert(SERIAL_RX_BUFFER_SIZE >= 2 &&
                  (SERIAL_RX_BUFFER_SIZE & (SERIAL_RX_BUFFER_SIZE - 1)) == 0,
              "SERIAL_RX_BUFFER_SIZE must be a power of two");
static_assert(SERIAL_TX_BUFFER_SIZE >= 2 &&
                  (SERIAL_TX_BUFFER_SIZE & (SERIAL_TX_BUFFER_SIZE - 1)) == 0,
              "SERIAL_TX_BUFFER_SIZE must be a power of two");

enum class HardwareSerialStartError : uint8_t {
    None = 0,
    UnsupportedBaud,
    UnsupportedConfig,
    ResourceBusy,
};

struct HardwareSerialErrorCounts {
    uint32_t bufferOverflow;
    uint32_t hardwareOverrun;
    uint32_t framing;
    uint32_t parity;
    uint32_t breakCondition;
};

class HardwareSerial : public Stream {
public:
    explicit HardwareSerial(uint8_t uartNumber);
    void begin(unsigned long baud = 921600, uint32_t config = SERIAL_8N1);
    void end();
    int available() override;
    int availableForWrite();
    int peek() override;
    int read() override;
    void flush() override;
    size_t write(uint8_t value) override;
    size_t write(const uint8_t *buffer, size_t size) override;
    inline size_t write(unsigned long value) { return write(static_cast<uint8_t>(value)); }
    inline size_t write(long value) { return write(static_cast<uint8_t>(value)); }
    inline size_t write(unsigned int value) { return write(static_cast<uint8_t>(value)); }
    inline size_t write(int value) { return write(static_cast<uint8_t>(value)); }
    using Print::write;
    operator bool() const { return _started; }
    uint8_t uartNumber() const { return _uartNumber; }
    HardwareSerialStartError lastError() const { return _lastError; }
    HardwareSerialErrorCounts errorCounts() const;
    void clearErrorCounts();

    // Called only by the three CI130X UART vectors.
    void handleInterrupt();

private:
    uint16_t rxCount() const;
    uint16_t txCount() const;

    uint8_t _uartNumber;
    volatile uint16_t _rxHead;
    volatile uint16_t _rxTail;
    volatile uint16_t _txHead;
    volatile uint16_t _txTail;
    uint8_t _rxBuffer[SERIAL_RX_BUFFER_SIZE];
    uint8_t _txBuffer[SERIAL_TX_BUFFER_SIZE];
    volatile HardwareSerialErrorCounts _errorCounts;
    HardwareSerialStartError _lastError;
    volatile bool _started;
};

extern HardwareSerial Serial;
extern HardwareSerial Serial1;
extern HardwareSerial Serial2;

void serialEventRun(void);

#endif
