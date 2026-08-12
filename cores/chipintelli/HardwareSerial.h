#ifndef HardwareSerial_h
#define HardwareSerial_h

#include "Stream.h"

#ifndef SERIAL_RX_BUFFER_SIZE
#define SERIAL_RX_BUFFER_SIZE 128
#endif

#ifndef SERIAL_TX_BUFFER_SIZE
#define SERIAL_TX_BUFFER_SIZE 128
#endif

#ifndef SERIAL0_RX_BUFFER_SIZE
#define SERIAL0_RX_BUFFER_SIZE SERIAL_RX_BUFFER_SIZE
#endif
#ifndef SERIAL0_TX_BUFFER_SIZE
#define SERIAL0_TX_BUFFER_SIZE SERIAL_TX_BUFFER_SIZE
#endif
#ifndef SERIAL1_RX_BUFFER_SIZE
#define SERIAL1_RX_BUFFER_SIZE SERIAL_RX_BUFFER_SIZE
#endif
#ifndef SERIAL1_TX_BUFFER_SIZE
#define SERIAL1_TX_BUFFER_SIZE SERIAL_TX_BUFFER_SIZE
#endif
#ifndef SERIAL2_RX_BUFFER_SIZE
#define SERIAL2_RX_BUFFER_SIZE SERIAL_RX_BUFFER_SIZE
#endif
#ifndef SERIAL2_TX_BUFFER_SIZE
#define SERIAL2_TX_BUFFER_SIZE SERIAL_TX_BUFFER_SIZE
#endif

static_assert(SERIAL_RX_BUFFER_SIZE >= 2 &&
                  (SERIAL_RX_BUFFER_SIZE & (SERIAL_RX_BUFFER_SIZE - 1)) == 0,
              "SERIAL_RX_BUFFER_SIZE must be a power of two");
static_assert(SERIAL_TX_BUFFER_SIZE >= 2 &&
                  (SERIAL_TX_BUFFER_SIZE & (SERIAL_TX_BUFFER_SIZE - 1)) == 0,
              "SERIAL_TX_BUFFER_SIZE must be a power of two");

#define CHIPINTELLI_SERIAL_BUFFER_ASSERT(port)                              \
static_assert(SERIAL##port##_RX_BUFFER_SIZE >= 2 &&                         \
                  (SERIAL##port##_RX_BUFFER_SIZE &                          \
                   (SERIAL##port##_RX_BUFFER_SIZE - 1)) == 0,               \
              "SERIAL" #port "_RX_BUFFER_SIZE must be a power of two");   \
static_assert(SERIAL##port##_TX_BUFFER_SIZE >= 2 &&                         \
                  (SERIAL##port##_TX_BUFFER_SIZE &                          \
                   (SERIAL##port##_TX_BUFFER_SIZE - 1)) == 0,               \
              "SERIAL" #port "_TX_BUFFER_SIZE must be a power of two")
CHIPINTELLI_SERIAL_BUFFER_ASSERT(0);
CHIPINTELLI_SERIAL_BUFFER_ASSERT(1);
CHIPINTELLI_SERIAL_BUFFER_ASSERT(2);
#undef CHIPINTELLI_SERIAL_BUFFER_ASSERT

enum class HardwareSerialStartError : uint8_t {
    None = 0,
    UnsupportedBaud,
    UnsupportedConfig,
    ResourceBusy,
    Timeout,
    Busy,
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
    bool flush(uint32_t timeoutMs);
    size_t write(uint8_t value) override;
    size_t write(const uint8_t *buffer, size_t size) override;
    size_t write(uint8_t value, uint32_t timeoutMs);
    size_t write(const uint8_t *buffer, size_t size, uint32_t timeoutMs);
    size_t tryWrite(uint8_t value);
    size_t tryWrite(const uint8_t *buffer, size_t size);
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
    bool enableTxDMA(bool enabled = true, size_t threshold = 64U);
    bool txDMAEnabled() const { return _txDMAEnabled; }

    // Called only by the three CI130X UART vectors.
    void handleInterrupt();
    void handleDmaInterrupt();

private:
    bool waitForData(unsigned long timeout) override;
    bool waitForTxSpace(uint32_t timeoutMs);
    size_t writeDma(const uint8_t *buffer, size_t size,
                    uint32_t timeoutMs);
    void pumpTxLocked();
    uint16_t rxCount() const;
    uint16_t txCount() const;

    uint8_t _uartNumber;
    volatile uint16_t _rxHead;
    volatile uint16_t _rxTail;
    volatile uint16_t _txHead;
    volatile uint16_t _txTail;
    uint8_t *_rxBuffer;
    uint8_t *_txBuffer;
    uint16_t _rxMask;
    uint16_t _txMask;
    uint16_t _rxBufferSize;
    uint16_t _txBufferSize;
    volatile HardwareSerialErrorCounts _errorCounts;
    void *_rxWaiter;
    void *_txWaiter;
    HardwareSerialStartError _lastError;
    volatile bool _started;
    volatile bool _txDMABusy;
    bool _txDMAEnabled;
    size_t _txDMAThreshold;
    void *_dmaWaiter;
};

extern HardwareSerial Serial;
extern HardwareSerial Serial1;
extern HardwareSerial Serial2;

void serialEventRun(void);

#endif
