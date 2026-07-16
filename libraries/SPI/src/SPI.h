#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

#define SPI_HAS_TRANSACTION 1

#ifndef SPI_MODE0
#define SPI_MODE0 0x00
#define SPI_MODE1 0x01
#define SPI_MODE2 0x02
#define SPI_MODE3 0x03
#endif

#ifndef SPI_LSBFIRST
#define SPI_LSBFIRST LSBFIRST
#define SPI_MSBFIRST MSBFIRST
#endif

class SPISettings {
public:
  SPISettings();
  SPISettings(uint32_t clock, uint8_t bitOrder, uint8_t dataMode);

private:
  friend class SPIClass;
  uint32_t _clock;
  uint8_t _bitOrder;
  uint8_t _dataMode;
};

class SPIClass {
public:
  // The bus argument is accepted for source compatibility. These CI13XX
  // profiles expose no second user SPI bus; every instance is a GPIO master.
  explicit SPIClass(uint8_t bus = 0);

  // With no arguments, uses SCK=PA5(5), MISO=PA2(2), MOSI=PA4(4), SS=PA3(3).
  // Supplying any argument selects a custom route; pass -1 for an unused
  // MISO, MOSI, or SS signal. SS is configured but remains user-controlled.
  bool begin(int8_t sck = -1, int8_t miso = -1, int8_t mosi = -1,
             int8_t ss = -1);
  void end();

  void beginTransaction(const SPISettings &settings);
  void endTransaction();

  void setBitOrder(uint8_t bitOrder);
  void setDataMode(uint8_t dataMode);
  void setFrequency(uint32_t frequency);

  uint8_t transfer(uint8_t data);
  uint16_t transfer16(uint16_t data);
  uint32_t transfer32(uint32_t data);
  void transfer(void *data, uint32_t size);
  void transferBytes(const uint8_t *data, uint8_t *out, uint32_t size);

  void write(uint8_t data);
  void write16(uint16_t data);
  void write32(uint32_t data);
  void writeBytes(const uint8_t *data, uint32_t size);

  // GPIO software SPI cannot mask individual hardware IRQ sources. These
  // transaction-era compatibility hooks intentionally do nothing.
  void usingInterrupt(int interruptNumber);
  void notUsingInterrupt(int interruptNumber);

  int8_t pinSCK() const { return _sck; }
  int8_t pinMISO() const { return _miso; }
  int8_t pinMOSI() const { return _mosi; }
  int8_t pinSS() const { return _ss; }

private:
  static bool validPin(int8_t pin);
  static bool distinctPins(int8_t sck, int8_t miso, int8_t mosi, int8_t ss);
  void updateTiming(uint32_t frequency);
  void waitHalfPeriod() const;
  uint8_t clockIdleLevel() const;

  int8_t _sck;
  int8_t _miso;
  int8_t _mosi;
  int8_t _ss;
  uint32_t _clock;
  uint32_t _halfPeriodUs;
  uint8_t _bitOrder;
  uint8_t _dataMode;
  bool _begun;
  bool _inTransaction;
};

extern SPIClass SPI;
