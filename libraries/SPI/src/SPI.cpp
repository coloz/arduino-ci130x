#include "SPI.h"

namespace {
constexpr uint32_t kDefaultClock = 100000;
constexpr uint32_t kMaximumClock = 500000;
}  // namespace

SPISettings::SPISettings()
    : _clock(kDefaultClock), _bitOrder(MSBFIRST), _dataMode(SPI_MODE0) {}

SPISettings::SPISettings(uint32_t clock, uint8_t bitOrder, uint8_t dataMode)
    : _clock(clock),
      _bitOrder(bitOrder == LSBFIRST ? LSBFIRST : MSBFIRST),
      _dataMode(dataMode & 0x03U) {}

SPIClass SPI;

SPIClass::SPIClass(uint8_t bus)
    : _sck(-1),
      _miso(-1),
      _mosi(-1),
      _ss(-1),
      _clock(kDefaultClock),
      _halfPeriodUs(5),
      _bitOrder(MSBFIRST),
      _dataMode(SPI_MODE0),
      _begun(false),
      _inTransaction(false) {
  (void)bus;
}

bool SPIClass::validPin(int8_t pin) {
  return pin >= 0 && pin < static_cast<int8_t>(NUM_DIGITAL_PINS) &&
         (g_APinDescription[pin].capabilities & PIN_CAP_GPIO) != 0;
}

bool SPIClass::distinctPins(int8_t sck, int8_t miso, int8_t mosi,
                            int8_t ss) {
  const int8_t pins[] = {sck, miso, mosi, ss};
  for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
    if (pins[i] < 0) {
      continue;
    }
    for (size_t j = i + 1; j < sizeof(pins) / sizeof(pins[0]); ++j) {
      if (pins[i] == pins[j]) {
        return false;
      }
    }
  }
  return true;
}

bool SPIClass::begin(int8_t sck, int8_t miso, int8_t mosi, int8_t ss) {
  if (sck < 0 && miso < 0 && mosi < 0 && ss < 0) {
    sck = SCK;
    miso = MISO;
    mosi = MOSI;
    ss = SS;
  }

  if (!validPin(sck) || (miso >= 0 && !validPin(miso)) ||
      (mosi >= 0 && !validPin(mosi)) || (ss >= 0 && !validPin(ss)) ||
      (miso < 0 && mosi < 0) || !distinctPins(sck, miso, mosi, ss)) {
    return false;
  }

  if (_begun) {
    end();
  }

  _sck = sck;
  _miso = miso;
  _mosi = mosi;
  _ss = ss;

  pinMode(_sck, OUTPUT);
  digitalWrite(_sck, clockIdleLevel());
  if (_mosi >= 0) {
    pinMode(_mosi, OUTPUT);
    digitalWrite(_mosi, LOW);
  }
  if (_miso >= 0) {
    pinMode(_miso, INPUT);
  }
  if (_ss >= 0) {
    pinMode(_ss, OUTPUT);
    digitalWrite(_ss, HIGH);
  }

  _begun = true;
  _inTransaction = false;
  return true;
}

void SPIClass::end() {
  if (!_begun) {
    return;
  }

  digitalWrite(_sck, clockIdleLevel());
  if (_ss >= 0) {
    digitalWrite(_ss, HIGH);
    pinMode(_ss, INPUT);
  }
  if (_mosi >= 0) {
    pinMode(_mosi, INPUT);
  }
  if (_miso >= 0) {
    pinMode(_miso, INPUT);
  }
  pinMode(_sck, INPUT);

  _sck = -1;
  _miso = -1;
  _mosi = -1;
  _ss = -1;
  _begun = false;
  _inTransaction = false;
}

void SPIClass::updateTiming(uint32_t frequency) {
  if (frequency == 0) {
    frequency = kDefaultClock;
  }
  if (frequency > kMaximumClock) {
    frequency = kMaximumClock;
  }
  _clock = frequency;
  _halfPeriodUs = (500000U + frequency - 1U) / frequency;
  if (_halfPeriodUs == 0) {
    _halfPeriodUs = 1;
  }
}

void SPIClass::waitHalfPeriod() const {
  delayMicroseconds(_halfPeriodUs);
}

uint8_t SPIClass::clockIdleLevel() const {
  return (_dataMode & 0x02U) != 0 ? HIGH : LOW;
}

void SPIClass::beginTransaction(const SPISettings &settings) {
  if (!_begun && !begin()) {
    return;
  }
  setBitOrder(settings._bitOrder);
  setDataMode(settings._dataMode);
  setFrequency(settings._clock);
  _inTransaction = true;
}

void SPIClass::endTransaction() {
  if (_begun) {
    digitalWrite(_sck, clockIdleLevel());
  }
  _inTransaction = false;
}

void SPIClass::setBitOrder(uint8_t bitOrder) {
  _bitOrder = bitOrder == LSBFIRST ? LSBFIRST : MSBFIRST;
}

void SPIClass::setDataMode(uint8_t dataMode) {
  _dataMode = dataMode & 0x03U;
  if (_begun) {
    digitalWrite(_sck, clockIdleLevel());
  }
}

void SPIClass::setFrequency(uint32_t frequency) {
  updateTiming(frequency);
}

uint8_t SPIClass::transfer(uint8_t data) {
  if (!_begun && !begin()) {
    return 0;
  }

  const uint8_t idle = clockIdleLevel();
  const uint8_t active = idle == LOW ? HIGH : LOW;
  const bool sampleOnTrailingEdge = (_dataMode & 0x01U) != 0;
  uint8_t received = 0;

  for (uint8_t i = 0; i < 8; ++i) {
    const uint8_t bitIndex =
        _bitOrder == LSBFIRST ? i : static_cast<uint8_t>(7U - i);
    const uint8_t outputLevel = (data & (1U << bitIndex)) != 0 ? HIGH : LOW;

    if (!sampleOnTrailingEdge) {
      if (_mosi >= 0) {
        digitalWrite(_mosi, outputLevel);
      }
      waitHalfPeriod();
      digitalWrite(_sck, active);
      if (_miso >= 0 && digitalRead(_miso) == HIGH) {
        received |= static_cast<uint8_t>(1U << bitIndex);
      }
      waitHalfPeriod();
      digitalWrite(_sck, idle);
    } else {
      digitalWrite(_sck, active);
      if (_mosi >= 0) {
        digitalWrite(_mosi, outputLevel);
      }
      waitHalfPeriod();
      digitalWrite(_sck, idle);
      if (_miso >= 0 && digitalRead(_miso) == HIGH) {
        received |= static_cast<uint8_t>(1U << bitIndex);
      }
      waitHalfPeriod();
    }
  }
  return received;
}

uint16_t SPIClass::transfer16(uint16_t data) {
  if (_bitOrder == LSBFIRST) {
    const uint8_t low = transfer(static_cast<uint8_t>(data));
    const uint8_t high = transfer(static_cast<uint8_t>(data >> 8));
    return static_cast<uint16_t>(low) |
           (static_cast<uint16_t>(high) << 8);
  }
  const uint8_t high = transfer(static_cast<uint8_t>(data >> 8));
  const uint8_t low = transfer(static_cast<uint8_t>(data));
  return (static_cast<uint16_t>(high) << 8) | low;
}

uint32_t SPIClass::transfer32(uint32_t data) {
  uint32_t received = 0;
  if (_bitOrder == LSBFIRST) {
    for (uint8_t shift = 0; shift < 32; shift += 8) {
      received |= static_cast<uint32_t>(
                      transfer(static_cast<uint8_t>(data >> shift)))
                  << shift;
    }
  } else {
    for (int8_t shift = 24; shift >= 0; shift -= 8) {
      received |= static_cast<uint32_t>(
                      transfer(static_cast<uint8_t>(data >> shift)))
                  << shift;
    }
  }
  return received;
}

void SPIClass::transfer(void *data, uint32_t size) {
  transferBytes(static_cast<const uint8_t *>(data),
                static_cast<uint8_t *>(data), size);
}

void SPIClass::transferBytes(const uint8_t *data, uint8_t *out,
                             uint32_t size) {
  for (uint32_t i = 0; i < size; ++i) {
    const uint8_t received = transfer(data == nullptr ? 0xffU : data[i]);
    if (out != nullptr) {
      out[i] = received;
    }
  }
}

void SPIClass::write(uint8_t data) {
  (void)transfer(data);
}

void SPIClass::write16(uint16_t data) {
  (void)transfer16(data);
}

void SPIClass::write32(uint32_t data) {
  (void)transfer32(data);
}

void SPIClass::writeBytes(const uint8_t *data, uint32_t size) {
  transferBytes(data, nullptr, size);
}

void SPIClass::usingInterrupt(int interruptNumber) {
  (void)interruptNumber;
}

void SPIClass::notUsingInterrupt(int interruptNumber) {
  (void)interruptNumber;
}
