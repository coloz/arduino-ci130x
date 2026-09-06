# BLE transport tests

These tests compile the real `src/utility/HCIESPLinkTransport.cpp` against an
injected RPC endpoint and clock. They exercise H4 byte stream fragmentation and
coalescing, 256-byte reads, exact write acceptance, begin/end, bounded polling,
clock rollover, malformed replies, controller drops, unavailable links, session
changes and uncertain writes with no Host-side replay.

From this directory with a C++17 compiler:

```sh
c++ -std=c++17 -Ifakes -I../src -I../../ESPLink/src \
  transport_test.cpp ../src/utility/HCIESPLinkTransport.cpp -o transport_test
./transport_test
```

On Windows, use an MSVC developer shell:

```bat
cl /nologo /EHsc /std:c++17 /I fakes /I ../src /I ../../ESPLink/src transport_test.cpp ../src/utility/HCIESPLinkTransport.cpp /Fe:transport_test.exe
transport_test.exe
```

These are transport logic tests, not an HCI-controller simulator or radio/security
qualification. Actual MCU compilation and hardware interoperability remain separate
checks.

No `ARDUINO_ARCH_CI13XX` define is used: the real transport must be selected by
the BLE package on any host architecture. The fake also checks that health
polling services the cooperative ESPLink backend. Rebuild with an unrelated board
macro (for example `ARDUINO_ARCH_STM32`) to check architecture independence.

In an MSVC developer shell, `powershell -File ./run_native.ps1` runs both the
architecture-neutral transport test and the old-backend selection stress build.
The latter compiles all eight old transport sources with conflicting board
selectors and verifies only ESPLink defines the HCI transport.
