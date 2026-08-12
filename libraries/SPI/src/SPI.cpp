#include "SPI.h"
#include "PeripheralManager.h"

extern "C" {
#include "FreeRTOS.h"
#include "task.h"
#include "ci130x_core_timer.h"
#include "ci130x_gpio.h"
}

namespace {
constexpr uint32_t kDefaultClock = 100000;
constexpr uint32_t kMaximumClock = 4000000;
#ifndef SPI_COOPERATIVE_CHUNK_BYTES
constexpr uint32_t kCooperativeChunkBytes = 64U;
#else
constexpr uint32_t kCooperativeChunkBytes = SPI_COOPERATIVE_CHUNK_BYTES;
#endif
static_assert(kCooperativeChunkBytes > 0U,
              "SPI_COOPERATIVE_CHUNK_BYTES must be greater than zero");

struct GpioRegisters {
  volatile uint32_t data[256];
};

gpio_base_t portBase(uint8_t port) {
  static const gpio_base_t bases[] = {PA, PB, PC, PD};
  return bases[port < 4U ? port : 0U];
}
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
      _halfPeriodTicks(1U),
      _sckMask(0U),
      _mosiMask(0U),
      _misoMask(0U),
      _sckData(nullptr),
      _mosiData(nullptr),
      _misoData(nullptr),
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

  uint8_t pins[4];
  size_t pinCount = 0;
  pins[pinCount++] = static_cast<uint8_t>(sck);
  if (miso >= 0) pins[pinCount++] = static_cast<uint8_t>(miso);
  if (mosi >= 0) pins[pinCount++] = static_cast<uint8_t>(mosi);
  if (ss >= 0) pins[pinCount++] = static_cast<uint8_t>(ss);
  const PeripheralResource resource = PeripheralResource::SoftwareSPI;
  if (!PeripheralManager.claim(PeripheralOwner::SPI, pins, pinCount,
                               &resource, 1)) {
    return false;
  }
  for (size_t i = 0; i < pinCount; ++i) {
    detachInterrupt(pins[i]);
  }

  _sck = sck;
  _miso = miso;
  _mosi = mosi;
  _ss = ss;

  if (!pinModeOwned(_sck, OUTPUT, PeripheralOwner::SPI)) {
    PeripheralManager.release(PeripheralOwner::SPI, pins, pinCount,
                              &resource, 1);
    _sck = _miso = _mosi = _ss = -1;
    return false;
  }
  digitalWrite(_sck, clockIdleLevel());
  if (_mosi >= 0) {
    (void)pinModeOwned(_mosi, OUTPUT, PeripheralOwner::SPI);
    digitalWrite(_mosi, LOW);
  }
  if (_miso >= 0) {
    (void)pinModeOwned(_miso, INPUT, PeripheralOwner::SPI);
  }
  if (_ss >= 0) {
    (void)pinModeOwned(_ss, OUTPUT, PeripheralOwner::SPI);
    digitalWrite(_ss, HIGH);
  }

  cacheGpioRegisters();
  updateTiming(_clock);

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
    (void)pinModeOwned(_ss, INPUT, PeripheralOwner::SPI);
  }
  if (_mosi >= 0) {
    (void)pinModeOwned(_mosi, INPUT, PeripheralOwner::SPI);
  }
  if (_miso >= 0) {
    (void)pinModeOwned(_miso, INPUT, PeripheralOwner::SPI);
  }
  (void)pinModeOwned(_sck, INPUT, PeripheralOwner::SPI);

  uint8_t pins[4];
  size_t pinCount = 0;
  pins[pinCount++] = static_cast<uint8_t>(_sck);
  if (_miso >= 0) pins[pinCount++] = static_cast<uint8_t>(_miso);
  if (_mosi >= 0) pins[pinCount++] = static_cast<uint8_t>(_mosi);
  if (_ss >= 0) pins[pinCount++] = static_cast<uint8_t>(_ss);
  const PeripheralResource resource = PeripheralResource::SoftwareSPI;
  PeripheralManager.release(PeripheralOwner::SPI, pins, pinCount,
                            &resource, 1);

  _sck = -1;
  _miso = -1;
  _mosi = -1;
  _ss = -1;
  _sckData = _mosiData = _misoData = nullptr;
  _sckMask = _mosiMask = _misoMask = 0U;
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
  const uint32_t timerClock = get_systick_clk();
  _halfPeriodTicks = static_cast<uint32_t>(
      (static_cast<uint64_t>(timerClock) + 2ULL * frequency - 1ULL) /
      (2ULL * frequency));
  if (_halfPeriodTicks == 0U) _halfPeriodTicks = 1U;
}

void SPIClass::cacheGpioRegisters() {
  const PinDescription &sck = g_APinDescription[_sck];
  _sckMask = 1U << sck.bit;
  _sckData = &reinterpret_cast<GpioRegisters *>(
                  static_cast<uintptr_t>(portBase(sck.port)))
                  ->data[_sckMask];
  if (_mosi >= 0) {
    const PinDescription &mosi = g_APinDescription[_mosi];
    _mosiMask = 1U << mosi.bit;
    _mosiData = &reinterpret_cast<GpioRegisters *>(
                     static_cast<uintptr_t>(portBase(mosi.port)))
                     ->data[_mosiMask];
  }
  if (_miso >= 0) {
    const PinDescription &miso = g_APinDescription[_miso];
    _misoMask = 1U << miso.bit;
    _misoData = &reinterpret_cast<GpioRegisters *>(
                     static_cast<uintptr_t>(portBase(miso.port)))
                     ->data[_misoMask];
  }
}

void SPIClass::waitUntil(uint64_t deadline) const {
  while (static_cast<int64_t>(get_timer_value() - deadline) < 0) {}
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
  cacheGpioRegisters();
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

  return transferByteHot(data);
}

uint8_t SPIClass::transferByteHot(uint8_t data) const {
  const uint32_t idle = clockIdleLevel() == HIGH ? _sckMask : 0U;
  const uint32_t active = idle == 0U ? _sckMask : 0U;
  const bool sampleOnTrailingEdge = (_dataMode & 0x01U) != 0;
  uint8_t received = 0;
  uint64_t deadline = get_timer_value();

  for (uint8_t i = 0; i < 8; ++i) {
    const uint8_t bitIndex =
        _bitOrder == LSBFIRST ? i : static_cast<uint8_t>(7U - i);
    const uint32_t output = (data & (1U << bitIndex)) != 0U
                                ? _mosiMask
                                : 0U;

    if (!sampleOnTrailingEdge) {
      if (_mosiData != nullptr) *_mosiData = output;
      deadline += _halfPeriodTicks;
      waitUntil(deadline);
      *_sckData = active;
      if (_misoData != nullptr && (*_misoData & _misoMask) != 0U) {
        received |= static_cast<uint8_t>(1U << bitIndex);
      }
      deadline += _halfPeriodTicks;
      waitUntil(deadline);
      *_sckData = idle;
    } else {
      *_sckData = active;
      if (_mosiData != nullptr) *_mosiData = output;
      deadline += _halfPeriodTicks;
      waitUntil(deadline);
      *_sckData = idle;
      if (_misoData != nullptr && (*_misoData & _misoMask) != 0U) {
        received |= static_cast<uint8_t>(1U << bitIndex);
      }
      deadline += _halfPeriodTicks;
      waitUntil(deadline);
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
  if (!_begun && !begin()) return;
  for (uint32_t i = 0; i < size; ++i) {
    const uint8_t received =
        transferByteHot(data == nullptr ? 0xffU : data[i]);
    if (out != nullptr) {
      out[i] = received;
    }
    if (kCooperativeChunkBytes != 0U &&
        (i + 1U) % kCooperativeChunkBytes == 0U &&
        xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
      taskYIELD();
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
