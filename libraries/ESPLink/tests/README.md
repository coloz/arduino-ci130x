# ESPLink native tests

The suites compile the production protocol and transport source with native
serial, clock and FreeRTOS replacements. They verify framing and lifecycle
behavior; they do not emulate physical UART timing, radio interoperability,
DMA hardware, target stack headroom or actual FreeRTOS scheduling.

## Codec and CRC

From the repository root in Linux/WSL with GCC:

```sh
sh libraries/ESPLink/tests/codec_run.sh
sh libraries/ESPLink/tests/codec_run.sh --benchmark
```

The script checks bitwise and table CRC32C, their architecture defaults, all
payload lengths through 1024 bytes, malformed frames, bit faults, dropped or
inserted bytes, overflow recovery and bounded readers/writers. It enables
AddressSanitizer and UndefinedBehaviorSanitizer. The optional benchmark measures
the native CPU, not CI1306 or end-to-end wireless throughput.

## CI1306 RTOS backend

From the repository root on Windows; the script detects MSVC Build Tools:

```powershell
& libraries/ESPLink/tests/runtime_run.ps1 -Variant optimized
& libraries/ESPLink/tests/runtime_run.ps1 -Variant fallback
& libraries/ESPLink/tests/runtime_run.ps1 -Variant configured
& libraries/ESPLink/tests/runtime_run.ps1 -Variant drain-disabled
```

Linux/WSL with GCC and address, undefined-behavior and leak sanitizers:

```sh
sh libraries/ESPLink/tests/runtime_run.sh optimized
sh libraries/ESPLink/tests/runtime_run.sh fallback
sh libraries/ESPLink/tests/runtime_run.sh configured
sh libraries/ESPLink/tests/runtime_run.sh drain-disabled
```

`optimized` uses the default CI settings. `fallback` disables bulk RX, DMA and
task notifications; `configured` changes the RX buffer, batch size, DMA threshold,
task stack and priority. `drain-disabled` checks the explicit frame-drain override.
The tests exercise retries and duplicate suppression,
session invalidation, response bounds, concurrent shutdown, task creation and
stop failures, serial ownership, notification wakeups and IRQ fallback.
Automatic Serial2 initialization also checks the 921600 default, retained custom
bindings after reconnect/failure, and the RX/DMA/bulk-read optimized backend.
The CI TX fake models lost consecutive writes without a drain; tests cover
consecutive requests/ACKs, drain failure, an exhausted drain budget, and bounded
total drain/write time. Generic and borrowed bindings must not invoke CI drain.
`runtime_test.cpp` includes the production implementation; do not compile
`ESPLink.cpp` a second time when building that test directly.

## Cooperative Stream backend

From the repository root in an MSVC developer shell:

```powershell
& libraries/ESPLink/tests/cooperative_run.ps1
```

Alternatively, from the repository root in Linux/WSL:

```sh
sh libraries/ESPLink/tests/cooperative_run.sh
```

The fake Arduino core provides no RTOS, HardwareSerial or global UART objects.
Cases cover explicit managed and borrowed bindings, reconnects and destruction,
partial writes, retries, uncertain delivery, stale sessions, bounded deadlines,
reentrant calls, polling heartbeats and clock rollover. A custom `Stream.write()`
must itself return within a bounded time; ESPLink cannot preempt a blocking
driver implementation.

## Default hardware UART selection

With Python 3 and GCC/Clang in Linux/WSL, from the repository root:

```sh
python3 libraries/ESPLink/tests/default_serial_test.py
```

This compiles the production selector against representative core declarations:
CI1306, hardware UART macros and aliases, ESP32 with/without global UART objects,
STM32duino 2.x/3.x existing/constructed ports, missing RX/TX routing, disabled HAL
UART and explicit build overrides. It checks UART selection, default baud and
reuse of the persistent STM32 serial object. Real MCU builds and physical pin
verification remain separate; native fixtures are not a core or radio emulator.

The PowerShell runners accept `-BuildDirectory` to select an output directory.
For the UART bulk-read implementation, run
`python3 tools/tests/test_hardware_serial_rx.py`
from the repository root in Linux/WSL with GCC. MCU example builds use
`tools/test_wireless.ps1`; physical board checks remain separate.
