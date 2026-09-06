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
```

Linux/WSL with GCC and address, undefined-behavior and leak sanitizers:

```sh
sh libraries/ESPLink/tests/runtime_run.sh optimized
sh libraries/ESPLink/tests/runtime_run.sh fallback
sh libraries/ESPLink/tests/runtime_run.sh configured
```

`optimized` uses the default CI settings. `fallback` disables bulk RX, DMA and
task notifications; `configured` changes the RX buffer, batch size, DMA threshold,
task stack and priority. The tests exercise retries and duplicate suppression,
session invalidation, response bounds, concurrent shutdown, task creation and
stop failures, serial ownership, notification wakeups and IRQ fallback.
`runtime_test.cpp` includes the production implementation; do not compile
`ESPLink.cpp` a second time when building that test directly.

## Cooperative Stream backend

From the repository root in an MSVC developer shell:

```powershell
& libraries/ESPLink/tests/cooperative_run.ps1
```

Alternatively, from this tests directory in Linux/WSL:

```sh
c++ -std=c++17 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -fno-omit-frame-pointer -Icooperative_fakes -I../src \
  cooperative_test.cpp ../src/ESPLink.cpp -o cooperative_test
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ./cooperative_test
```

The fake Arduino core provides no RTOS, HardwareSerial or global UART objects.
Cases cover explicit managed and borrowed bindings, reconnects and destruction,
partial writes, retries, uncertain delivery, stale sessions, bounded deadlines,
reentrant calls, polling heartbeats and clock rollover. A custom `Stream.write()`
must itself return within a bounded time; ESPLink cannot preempt a blocking
driver implementation.

The PowerShell runners accept `-BuildDirectory` to select an output directory.
For the UART bulk-read implementation, run
`python3 tools/tests/test_hardware_serial_rx.py`
from the repository root in Linux/WSL with GCC. MCU example builds use
`tools/test_wireless.ps1`; physical board checks remain separate.
