// SPDX-License-Identifier: MIT
#pragma once
// Private implementation header: Arduino cores do not share a universal UART
// count. Inspect documented core capability macros, never #ifdef Serial2 (a
// C++ object), and never assume that an unknown core's Serial is a hardware UART.
#if defined(ARDUINO_ARCH_STM32) && !defined(ESPLINK_DEFAULT_SERIAL)
  #if __has_include(<Serial.h>)
    #include <Serial.h>
    #define ESPLINK_STM32_UART_TYPE Uart
  #else
    #include <HardwareSerial.h>
    #define ESPLINK_STM32_UART_TYPE HardwareSerial
  #endif
  #include <pinmap.h>
  #include <PeripheralPins.h>
#endif

namespace esplink_detail {
#if defined(ARDUINO_ARCH_STM32) && !defined(ESPLINK_DEFAULT_SERIAL) && \
    defined(HAL_UART_MODULE_ENABLED) && !defined(HAL_UART_MODULE_ONLY)
using DefaultSTM32Serial = ESPLINK_STM32_UART_TYPE;
struct STM32Port { void* peripheral; DefaultSTM32Serial* existing; };

inline bool beginDefaultSTM32Serial(ESPLinkClass& link) {
  // Ordinary UART/USART ports are ordered by instance number. LPUART is a
  // separate numbering family and is used only when no ordinary UART is routed.
  // HAVE_HWSERIAL means an actual core-owned object exists; merely having the
  // peripheral (or an extern declaration) does not provide a linkable SerialN.
  const STM32Port ports[] = {
#if defined(USART10) || defined(UART10)
  #if defined(USART10)
    { USART10,
  #else
    { UART10,
  #endif
  #if defined(HAVE_HWSERIAL10)
      &Serial10 },
  #else
      nullptr },
  #endif
#endif
#if defined(UART9)
    { UART9,
  #if defined(HAVE_HWSERIAL9)
      &Serial9 },
  #else
      nullptr },
  #endif
#endif
#if defined(USART8) || defined(UART8)
  #if defined(USART8)
    { USART8,
  #else
    { UART8,
  #endif
  #if defined(HAVE_HWSERIAL8)
      &Serial8 },
  #else
      nullptr },
  #endif
#endif
#if defined(USART7) || defined(UART7)
  #if defined(USART7)
    { USART7,
  #else
    { UART7,
  #endif
  #if defined(HAVE_HWSERIAL7)
      &Serial7 },
  #else
      nullptr },
  #endif
#endif
#if defined(USART6)
    { USART6,
  #if defined(HAVE_HWSERIAL6)
      &Serial6 },
  #else
      nullptr },
  #endif
#endif
#if defined(USART5) || defined(UART5)
  #if defined(USART5)
    { USART5,
  #else
    { UART5,
  #endif
  #if defined(HAVE_HWSERIAL5)
      &Serial5 },
  #else
      nullptr },
  #endif
#endif
#if defined(USART4) || defined(UART4)
  #if defined(USART4)
    { USART4,
  #else
    { UART4,
  #endif
  #if defined(HAVE_HWSERIAL4)
      &Serial4 },
  #else
      nullptr },
  #endif
#endif
#if defined(USART3)
    { USART3,
  #if defined(HAVE_HWSERIAL3)
      &Serial3 },
  #else
      nullptr },
  #endif
#endif
#if defined(USART2)
    { USART2,
  #if defined(HAVE_HWSERIAL2)
      &Serial2 },
  #else
      nullptr },
  #endif
#endif
#if defined(USART1)
    { USART1,
  #if defined(HAVE_HWSERIAL1)
      &Serial1 },
  #else
      nullptr },
  #endif
#endif
#if defined(LPUART3)
    { LPUART3,
  #if defined(HAVE_HWSERIALLP3)
      &SerialLP3 },
  #else
      nullptr },
  #endif
#endif
#if defined(LPUART2)
    { LPUART2,
  #if defined(HAVE_HWSERIALLP2)
      &SerialLP2 },
  #else
      nullptr },
  #endif
#endif
#if defined(LPUART1)
    { LPUART1,
  #if defined(HAVE_HWSERIALLP1)
      &SerialLP1 },
  #else
      nullptr },
  #endif
#endif
    { nullptr, nullptr }
  };
  for (const auto& port : ports) {
    if (!port.peripheral) break;
    if (pinmap_pin(port.peripheral, PinMap_UART_RX) == NC ||
        pinmap_pin(port.peripheral, PinMap_UART_TX) == NC) continue;
    if (port.existing) return link.begin(*port.existing, ESPLINK_DEFAULT_BAUD);
    // Match the core's peripheral constructor, including PIN_SERIALn overrides
    // and the variant PinMap's first RX/TX pair. Retain one UART for program
    // lifetime so the global ESPLink can safely release it during destruction.
    alignas(DefaultSTM32Serial) static uint8_t storage[sizeof(DefaultSTM32Serial)];
    static DefaultSTM32Serial* serial = nullptr;
    if (!serial) serial = new (storage) DefaultSTM32Serial(port.peripheral);
    return link.begin(*serial, ESPLINK_DEFAULT_BAUD);
  }
  return false; // No full-duplex UART is routed by this board's pin map.
}
#endif

inline bool beginDefaultSerial(ESPLinkClass& link) {
#if defined(ESPLINK_DEFAULT_SERIAL)
  return link.begin(ESPLINK_DEFAULT_SERIAL, ESPLINK_DEFAULT_BAUD);
#elif defined(ARDUINO_ARCH_CI13XX)
  // Keep this expression concrete: overload resolution must select the CI
  // HardwareSerial backend (RX ring, DMA, notifications and bulk reads).
  return link.begin(Serial2, ESPLINK_DEFAULT_BAUD);
#elif defined(ARDUINO_ARCH_STM32)
  #if defined(HAL_UART_MODULE_ENABLED) && !defined(HAL_UART_MODULE_ONLY)
    return beginDefaultSTM32Serial(link);
  #else
    (void)link;
    return false;
  #endif
#elif defined(ARDUINO_ARCH_ESP32)
  #if defined(NO_GLOBAL_INSTANCES) || defined(NO_GLOBAL_SERIAL)
    (void)link;
    return false;
  #elif SOC_UART_NUM > 5
    return link.begin(Serial5, ESPLINK_DEFAULT_BAUD);
  #elif SOC_UART_NUM > 4
    return link.begin(Serial4, ESPLINK_DEFAULT_BAUD);
  #elif SOC_UART_NUM > 3
    return link.begin(Serial3, ESPLINK_DEFAULT_BAUD);
  #elif SOC_UART_NUM > 2
    return link.begin(Serial2, ESPLINK_DEFAULT_BAUD);
  #elif SOC_UART_NUM > 1
    return link.begin(Serial1, ESPLINK_DEFAULT_BAUD);
  #else
    return link.begin(Serial0, ESPLINK_DEFAULT_BAUD);
  #endif
#elif defined(HAVE_HWSERIAL10)
  return link.begin(Serial10, ESPLINK_DEFAULT_BAUD);
#elif defined(HAVE_HWSERIAL9)
  return link.begin(Serial9, ESPLINK_DEFAULT_BAUD);
#elif defined(HAVE_HWSERIAL8)
  return link.begin(Serial8, ESPLINK_DEFAULT_BAUD);
#elif defined(HAVE_HWSERIAL7)
  return link.begin(Serial7, ESPLINK_DEFAULT_BAUD);
#elif defined(HAVE_HWSERIAL6)
  return link.begin(Serial6, ESPLINK_DEFAULT_BAUD);
#elif defined(HAVE_HWSERIAL5)
  return link.begin(Serial5, ESPLINK_DEFAULT_BAUD);
#elif defined(HAVE_HWSERIAL4)
  return link.begin(Serial4, ESPLINK_DEFAULT_BAUD);
#elif defined(HAVE_HWSERIAL3)
  return link.begin(Serial3, ESPLINK_DEFAULT_BAUD);
#elif defined(HAVE_HWSERIAL2)
  return link.begin(Serial2, ESPLINK_DEFAULT_BAUD);
#elif defined(HAVE_HWSERIAL1)
  return link.begin(Serial1, ESPLINK_DEFAULT_BAUD);
#elif defined(SERIAL_PORT_HARDWARE6)
  return link.begin(SERIAL_PORT_HARDWARE6, ESPLINK_DEFAULT_BAUD);
#elif defined(SERIAL_PORT_HARDWARE5)
  return link.begin(SERIAL_PORT_HARDWARE5, ESPLINK_DEFAULT_BAUD);
#elif defined(SERIAL_PORT_HARDWARE4)
  return link.begin(SERIAL_PORT_HARDWARE4, ESPLINK_DEFAULT_BAUD);
#elif defined(SERIAL_PORT_HARDWARE3)
  return link.begin(SERIAL_PORT_HARDWARE3, ESPLINK_DEFAULT_BAUD);
#elif defined(SERIAL_PORT_HARDWARE2)
  return link.begin(SERIAL_PORT_HARDWARE2, ESPLINK_DEFAULT_BAUD);
#elif defined(SERIAL_PORT_HARDWARE1)
  return link.begin(SERIAL_PORT_HARDWARE1, ESPLINK_DEFAULT_BAUD);
#elif defined(SERIAL_PORT_HARDWARE_OPEN)
  return link.begin(SERIAL_PORT_HARDWARE_OPEN, ESPLINK_DEFAULT_BAUD);
#elif defined(SERIAL_PORT_HARDWARE)
  return link.begin(SERIAL_PORT_HARDWARE, ESPLINK_DEFAULT_BAUD);
#elif defined(ARDUINO_ARCH_AVR) && defined(HAVE_HWSERIAL0)
  return link.begin(Serial, ESPLINK_DEFAULT_BAUD);
#else
  // Explicit begin(port, baud) remains available on unrecognized Arduino cores.
  (void)link;
  return false;
#endif
}
} // namespace esplink_detail

#ifdef ESPLINK_STM32_UART_TYPE
#undef ESPLINK_STM32_UART_TYPE
#endif
