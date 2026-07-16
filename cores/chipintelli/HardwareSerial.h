#ifndef HardwareSerial_h
#define HardwareSerial_h

#include "Stream.h"

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
    using Print::write;
    operator bool() const { return _started; }
    uint8_t uartNumber() const { return _uartNumber; }

private:
    uint8_t _uartNumber;
    int _peek;
    bool _started;
};

extern HardwareSerial Serial;
extern HardwareSerial Serial1;
extern HardwareSerial Serial2;

#endif
