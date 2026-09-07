#!/usr/bin/env python3
"""Compile the production UART selector against representative core contracts.

Run with Python 3 and GCC/Clang (including WSL). All artifacts use a temporary
directory. These are selection/lifetime tests, not physical UART timing tests.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[1] / "src"
STUB = r"""
#include <cstddef>
#include <cstdint>
#include <new>
#include <cassert>
#include "ESPLinkConfig.h"
struct FakeSerial {
  int id;
  explicit FakeSerial(int number) : id(number) {}
  explicit FakeSerial(void* peripheral) : id(int(reinterpret_cast<uintptr_t>(peripheral))) {
    ++constructed;
  }
  static int constructed;
};
int FakeSerial::constructed = 0;
FakeSerial Serial(0), Serial0(0), Serial1(1), Serial2(2), Serial3(3), Serial4(4),
  Serial5(5), Serial6(6), Serial7(7), Serial8(8), Serial9(9), Serial10(10),
  SerialLP1(11), SerialLP2(12), SerialLP3(13), Selected(42);
class ESPLinkClass {
 public:
  FakeSerial* serial = nullptr;
  uint32_t baud = 0;
  bool begin(FakeSerial& port, uint32_t speed) {
    serial = &port; baud = speed; return true;
  }
};
#include "ESPLinkDefaultSerial.h"
int main() {
  ESPLinkClass link;
  const bool ready = esplink_detail::beginDefaultSerial(link);
#if EXPECT_PORT < 0
  assert(!ready && !link.serial);
#else
  assert(ready && link.serial && link.serial->id == EXPECT_PORT);
  assert(link.baud == EXPECT_BAUD);
  auto* first = link.serial;
  assert(esplink_detail::beginDefaultSerial(link) && link.serial == first);
#endif
  assert(FakeSerial::constructed == EXPECT_CONSTRUCTED);
}
"""
PINMAP = r"""
#pragma once
constexpr int NC = -1;
using PinMap = int;
inline int pinmap_pin(void* peripheral, const PinMap* map) {
  const auto n = reinterpret_cast<uintptr_t>(peripheral);
#if defined(TEST_NO_UART_PINS)
  (void)n; (void)map;
  return NC;
#else
  #if defined(TEST_UART6_TX_ONLY)
    if (n == 6 && *map == 1) return NC;
  #else
    (void)map;
  #endif
  return int(n);
#endif
}
"""
PERIPHERALS = r"""
#pragma once
#include "pinmap.h"
const PinMap PinMap_UART_RX[] = {1};
const PinMap PinMap_UART_TX[] = {2};
#define USART1 reinterpret_cast<void*>(uintptr_t(1))
#define USART2 reinterpret_cast<void*>(uintptr_t(2))
#define USART6 reinterpret_cast<void*>(uintptr_t(6))
"""


class DefaultSerialTest(unittest.TestCase):
    def test_core_contracts(self):
        cases = [
            ("unknown core", [], -1, 0, 921600),
            ("CI1306", ["ARDUINO_ARCH_CI13XX", "ESPLINK_CI13XX_RTOS=0"], 2, 0, 921600),
            ("highest HAVE_HWSERIAL", ["HAVE_HWSERIAL1", "HAVE_HWSERIAL6"], 6, 0, 921600),
            ("hardware aliases", ["SERIAL_PORT_HARDWARE1=Serial1", "SERIAL_PORT_HARDWARE5=Serial5"], 5, 0, 921600),
            ("hardware open alias", ["SERIAL_PORT_HARDWARE_OPEN=Serial3"], 3, 0, 921600),
            ("hardware alias", ["SERIAL_PORT_HARDWARE=Serial1"], 1, 0, 921600),
            ("AVR sole UART", ["ARDUINO_ARCH_AVR", "HAVE_HWSERIAL0"], 0, 0, 921600),
            ("ESP32 with LP UART", ["ARDUINO_ARCH_ESP32", "SOC_UART_NUM=6"], 5, 0, 921600),
            ("ESP32 globals disabled", ["ARDUINO_ARCH_ESP32", "SOC_UART_NUM=3", "NO_GLOBAL_SERIAL"], -1, 0, 921600),
            ("explicit object and baud macros", ["ESPLINK_DEFAULT_SERIAL=Selected", "ESPLINK_DEFAULT_BAUD=115200", "HAVE_HWSERIAL6"], 42, 0, 115200),
            ("STM32 constructed UART", ["ARDUINO_ARCH_STM32", "HAL_UART_MODULE_ENABLED"], 6, 1, 921600),
            ("STM32 existing UART", ["ARDUINO_ARCH_STM32", "HAL_UART_MODULE_ENABLED", "HAVE_HWSERIAL6"], 6, 0, 921600),
            ("STM32 skips TX-only UART", ["ARDUINO_ARCH_STM32", "HAL_UART_MODULE_ENABLED", "TEST_UART6_TX_ONLY"], 2, 1, 921600),
            ("STM32 no routed UART", ["ARDUINO_ARCH_STM32", "HAL_UART_MODULE_ENABLED", "TEST_NO_UART_PINS"], -1, 0, 921600),
            ("STM32 HAL UART disabled", ["ARDUINO_ARCH_STM32"], -1, 0, 921600),
            ("STM32 override bypasses detection", ["ARDUINO_ARCH_STM32", "ESPLINK_DEFAULT_SERIAL=Selected"], 42, 0, 921600),
        ]
        with tempfile.TemporaryDirectory(prefix="esplink-default-") as directory:
            build = Path(directory)
            (build / "Serial.h").write_text("#pragma once\nusing Uart = FakeSerial;\n", encoding="utf-8")
            (build / "HardwareSerial.h").write_text("#pragma once\nusing HardwareSerial = FakeSerial;\n", encoding="utf-8")
            (build / "pinmap.h").write_text(PINMAP, encoding="utf-8")
            (build / "PeripheralPins.h").write_text(PERIPHERALS, encoding="utf-8")
            source = build / "selector.cpp"
            source.write_text(STUB, encoding="utf-8")
            # Run the same contracts for STM32duino 3.x Uart and 2.x
            # HardwareSerial, selected by the core's actual header layout.
            for core in (3, 2):
                if core == 2:
                    (build / "Serial.h").unlink()
                for index, (name, defines, port, constructed, baud) in enumerate(cases):
                    if core == 2 and "ARDUINO_ARCH_STM32" not in defines:
                        continue
                    with self.subTest(core=core, scenario=name):
                        binary = build / f"selector-{core}-{index}"
                        flags = [f"-D{value}" for value in defines]
                        flags += [f"-DEXPECT_PORT={port}", f"-DEXPECT_CONSTRUCTED={constructed}", f"-DEXPECT_BAUD={baud}"]
                        command = [os.environ.get("CXX", "g++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                                   "-I", str(build), "-I", str(SOURCE), *flags, str(source), "-o", str(binary)]
                        subprocess.run(command, check=True, capture_output=True, text=True)
                        subprocess.run([str(binary)], check=True)
                        print(f"PASS: core {core}: {name}")


if __name__ == "__main__":
    unittest.main()
