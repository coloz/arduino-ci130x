#pragma once

#include <stddef.h>
#include <stdint.h>

// CI130X pads and peripheral blocks are heavily multiplexed.  Libraries use
// this manager before touching a mux register so a failed begin() leaves the
// previous peripheral fully operational instead of partially reconfiguring it.
enum class PeripheralOwner : uint8_t {
  None = 0,
  GPIO,
  Serial0,
  Serial1,
  Serial2,
  Wire,
  SPI,
  Servo,
  AnalogWrite,
  Tone,
  Adc,
  Infrared,
  Timer,
  System,
};

enum class PeripheralResource : uint8_t {
  Uart0 = 0,
  Uart1,
  Uart2,
  Iic0,
  SoftwareSPI,
  Pwm0,
  Pwm1,
  Pwm2,
  Pwm3,
  Pwm4,
  Pwm5,
  Timer0,
  Timer1,
  Timer2,
  Timer3,
  Count,
};

enum class PeripheralError : uint8_t {
  None = 0,
  InvalidOwner,
  InvalidPin,
  InvalidResource,
  PinBusy,
  ResourceBusy,
};

struct PeripheralConflict {
  PeripheralError error;
  PeripheralOwner requestedOwner;
  PeripheralOwner currentOwner;
  int16_t pin;
  PeripheralResource resource;
};

class PeripheralManagerClass {
 public:
  bool claim(PeripheralOwner owner, const uint8_t *pins, size_t pinCount,
             const PeripheralResource *resources, size_t resourceCount);
  bool claimPin(PeripheralOwner owner, uint8_t pin);
  bool claimResource(PeripheralOwner owner, PeripheralResource resource);

  void release(PeripheralOwner owner, const uint8_t *pins, size_t pinCount,
               const PeripheralResource *resources, size_t resourceCount);
  void releasePin(PeripheralOwner owner, uint8_t pin);
  void releaseResource(PeripheralOwner owner, PeripheralResource resource);
  void releaseAll(PeripheralOwner owner);

  PeripheralOwner pinOwner(uint8_t pin) const;
  PeripheralOwner resourceOwner(PeripheralResource resource) const;
  PeripheralConflict lastConflict() const;
  void clearLastConflict();

  static const char *ownerName(PeripheralOwner owner);
  static const char *resourceName(PeripheralResource resource);

 private:
  void setConflict(PeripheralError error, PeripheralOwner requested,
                   PeripheralOwner current, int16_t pin,
                   PeripheralResource resource);
};

extern PeripheralManagerClass PeripheralManager;

// Core/library-only pin configuration entry point.  Unlike public pinMode(),
// this preserves an already acquired peripheral ownership token.
bool pinModeOwned(uint8_t pin, uint8_t mode, PeripheralOwner owner);
