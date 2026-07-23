#include "Arduino.h"
#include "PeripheralManager.h"

extern "C" {
#include "FreeRTOS.h"
#include "task.h"
}

namespace {
constexpr size_t kResourceCount =
    static_cast<size_t>(PeripheralResource::Count);

PeripheralOwner s_pinOwners[NUM_DIGITAL_PINS] = {};
PeripheralOwner s_resourceOwners[kResourceCount] = {};
PeripheralConflict s_lastConflict = {
    PeripheralError::None, PeripheralOwner::None, PeripheralOwner::None, -1,
    PeripheralResource::Count};

bool validOwner(PeripheralOwner owner) {
  return owner != PeripheralOwner::None;
}

bool validResource(PeripheralResource resource) {
  return static_cast<size_t>(resource) < kResourceCount;
}

bool systemReserved(PeripheralResource resource) {
#if defined(USE_BLE) && USE_BLE
  // The fixed Arduino SDK profile enables the vendor BLE stack, whose RF
  // driver owns TIMER3 without going through PeripheralManager.
  return resource == PeripheralResource::Timer3;
#else
  (void)resource;
  return false;
#endif
}

PeripheralOwner currentResourceOwner(PeripheralResource resource) {
  return systemReserved(resource)
             ? PeripheralOwner::System
             : s_resourceOwners[static_cast<size_t>(resource)];
}

bool ownerCanReplace(PeripheralOwner current, PeripheralOwner requested) {
  return current == PeripheralOwner::None || current == requested ||
         (current == PeripheralOwner::GPIO &&
          requested != PeripheralOwner::GPIO);
}
}  // namespace

PeripheralManagerClass PeripheralManager;

void PeripheralManagerClass::setConflict(PeripheralError error,
                                         PeripheralOwner requested,
                                         PeripheralOwner current, int16_t pin,
                                         PeripheralResource resource) {
  taskENTER_CRITICAL();
  s_lastConflict = {error, requested, current, pin, resource};
  taskEXIT_CRITICAL();
}

bool PeripheralManagerClass::claim(
    PeripheralOwner owner, const uint8_t *pins, size_t pinCount,
    const PeripheralResource *resources, size_t resourceCount) {
  if (!validOwner(owner)) {
    setConflict(PeripheralError::InvalidOwner, owner, PeripheralOwner::None,
                -1, PeripheralResource::Count);
    return false;
  }
  for (size_t i = 0; i < pinCount; ++i) {
    if (pins == nullptr || pins[i] >= NUM_DIGITAL_PINS) {
      setConflict(PeripheralError::InvalidPin, owner, PeripheralOwner::None,
                  pins == nullptr ? -1 : pins[i], PeripheralResource::Count);
      return false;
    }
  }
  for (size_t i = 0; i < resourceCount; ++i) {
    if (resources == nullptr || !validResource(resources[i])) {
      setConflict(PeripheralError::InvalidResource, owner,
                  PeripheralOwner::None, -1,
                  resources == nullptr ? PeripheralResource::Count
                                       : resources[i]);
      return false;
    }
  }

  taskENTER_CRITICAL();
  for (size_t i = 0; i < pinCount; ++i) {
    const PeripheralOwner current = s_pinOwners[pins[i]];
    if (!ownerCanReplace(current, owner)) {
      setConflict(PeripheralError::PinBusy, owner, current, pins[i],
                  PeripheralResource::Count);
      taskEXIT_CRITICAL();
      return false;
    }
  }
  for (size_t i = 0; i < resourceCount; ++i) {
    const PeripheralOwner current = currentResourceOwner(resources[i]);
    if (current != PeripheralOwner::None && current != owner) {
      setConflict(PeripheralError::ResourceBusy, owner, current, -1,
                  resources[i]);
      taskEXIT_CRITICAL();
      return false;
    }
  }

  for (size_t i = 0; i < pinCount; ++i) {
    s_pinOwners[pins[i]] = owner;
  }
  for (size_t i = 0; i < resourceCount; ++i) {
    s_resourceOwners[static_cast<size_t>(resources[i])] = owner;
  }
  s_lastConflict = {PeripheralError::None, owner, PeripheralOwner::None, -1,
                    PeripheralResource::Count};
  taskEXIT_CRITICAL();
  return true;
}

bool PeripheralManagerClass::claimPin(PeripheralOwner owner, uint8_t pin) {
  return claim(owner, &pin, 1, nullptr, 0);
}

bool PeripheralManagerClass::claimResource(PeripheralOwner owner,
                                           PeripheralResource resource) {
  return claim(owner, nullptr, 0, &resource, 1);
}

void PeripheralManagerClass::release(
    PeripheralOwner owner, const uint8_t *pins, size_t pinCount,
    const PeripheralResource *resources, size_t resourceCount) {
  taskENTER_CRITICAL();
  if (pins != nullptr) {
    for (size_t i = 0; i < pinCount; ++i) {
      if (pins[i] < NUM_DIGITAL_PINS && s_pinOwners[pins[i]] == owner) {
        s_pinOwners[pins[i]] = PeripheralOwner::None;
      }
    }
  }
  if (resources != nullptr) {
    for (size_t i = 0; i < resourceCount; ++i) {
      if (validResource(resources[i]) &&
          s_resourceOwners[static_cast<size_t>(resources[i])] == owner) {
        s_resourceOwners[static_cast<size_t>(resources[i])] =
            PeripheralOwner::None;
      }
    }
  }
  taskEXIT_CRITICAL();
}

void PeripheralManagerClass::releasePin(PeripheralOwner owner, uint8_t pin) {
  release(owner, &pin, 1, nullptr, 0);
}

void PeripheralManagerClass::releaseResource(PeripheralOwner owner,
                                             PeripheralResource resource) {
  release(owner, nullptr, 0, &resource, 1);
}

void PeripheralManagerClass::releaseAll(PeripheralOwner owner) {
  taskENTER_CRITICAL();
  for (size_t i = 0; i < NUM_DIGITAL_PINS; ++i) {
    if (s_pinOwners[i] == owner) {
      s_pinOwners[i] = PeripheralOwner::None;
    }
  }
  for (size_t i = 0; i < kResourceCount; ++i) {
    if (s_resourceOwners[i] == owner) {
      s_resourceOwners[i] = PeripheralOwner::None;
    }
  }
  taskEXIT_CRITICAL();
}

PeripheralOwner PeripheralManagerClass::pinOwner(uint8_t pin) const {
  if (pin >= NUM_DIGITAL_PINS) {
    return PeripheralOwner::None;
  }
  taskENTER_CRITICAL();
  const PeripheralOwner owner = s_pinOwners[pin];
  taskEXIT_CRITICAL();
  return owner;
}

PeripheralOwner PeripheralManagerClass::resourceOwner(
    PeripheralResource resource) const {
  if (!validResource(resource)) {
    return PeripheralOwner::None;
  }
  taskENTER_CRITICAL();
  const PeripheralOwner owner = currentResourceOwner(resource);
  taskEXIT_CRITICAL();
  return owner;
}

PeripheralConflict PeripheralManagerClass::lastConflict() const {
  taskENTER_CRITICAL();
  const PeripheralConflict result = s_lastConflict;
  taskEXIT_CRITICAL();
  return result;
}

void PeripheralManagerClass::clearLastConflict() {
  taskENTER_CRITICAL();
  s_lastConflict = {PeripheralError::None, PeripheralOwner::None,
                    PeripheralOwner::None, -1, PeripheralResource::Count};
  taskEXIT_CRITICAL();
}

const char *PeripheralManagerClass::ownerName(PeripheralOwner owner) {
  switch (owner) {
    case PeripheralOwner::None: return "none";
    case PeripheralOwner::GPIO: return "GPIO";
    case PeripheralOwner::Serial0: return "Serial";
    case PeripheralOwner::Serial1: return "Serial1";
    case PeripheralOwner::Serial2: return "Serial2";
    case PeripheralOwner::Wire: return "Wire";
    case PeripheralOwner::SPI: return "SPI";
    case PeripheralOwner::Servo: return "Servo";
    case PeripheralOwner::AnalogWrite: return "analogWrite";
    case PeripheralOwner::Tone: return "tone";
    case PeripheralOwner::Adc: return "ADC";
    case PeripheralOwner::Infrared: return "infrared";
    case PeripheralOwner::Timer: return "timer";
    case PeripheralOwner::System: return "system";
  }
  return "unknown";
}

const char *PeripheralManagerClass::resourceName(
    PeripheralResource resource) {
  switch (resource) {
    case PeripheralResource::Uart0: return "UART0";
    case PeripheralResource::Uart1: return "UART1";
    case PeripheralResource::Uart2: return "UART2";
    case PeripheralResource::Iic0: return "IIC0";
    case PeripheralResource::SoftwareSPI: return "software SPI";
    case PeripheralResource::Pwm0: return "PWM0";
    case PeripheralResource::Pwm1: return "PWM1";
    case PeripheralResource::Pwm2: return "PWM2";
    case PeripheralResource::Pwm3: return "PWM3";
    case PeripheralResource::Pwm4: return "PWM4";
    case PeripheralResource::Pwm5: return "PWM5";
    case PeripheralResource::Timer0: return "TIMER0";
    case PeripheralResource::Timer1: return "TIMER1";
    case PeripheralResource::Timer2: return "TIMER2";
    case PeripheralResource::Timer3: return "TIMER3";
    case PeripheralResource::Count: break;
  }
  return "unknown";
}
