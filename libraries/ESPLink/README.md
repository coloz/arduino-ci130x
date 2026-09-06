# ESPLink

A shared Arduino transport for the WiFi and BLE libraries, using an external
ESP32-C3 over TX/RX/GND. The wire protocol is independent of the host MCU.
CI1306 and STM32 are the first integration targets; another Arduino core needs
a compatible serial implementation and sufficient RAM. `architectures=*`
allows integration across cores; it does not mean every board has been tested.

The companion firmware is in `../wifi_c3` relative to the Arduino platform
root. [PROTOCOL.md](PROTOCOL.md) specifies the service layout.

## Select the serial port

Include `<ESPLink.h>` and explicitly choose the port connected to the C3.
For a board that provides `Serial1`:

```cpp
#include <ESPLink.h>

void setup() {
  Serial.begin(115200);  // Logs use a different port from the C3 link.
  if (!ESPLink.begin(Serial1, 115200, 10000)) {
    Serial.println(ESPLink.lastError());
    return;
  }
  Serial.println(ESPLink.capabilities().firmwareVersion);
  Serial.println(ESPLink.ping() ? "C3 ready" : "C3 request failed");
}

void loop() {
  ESPLink.poll();
  delay(1);
}
```

Replace `Serial1` with the port and pins available on your board. The
[LinkDiagnostics example](examples/LinkDiagnostics/LinkDiagnostics.ino)
selects CI1306 `Serial2`, STM32duino PA10 RX / PA9 TX, or another board's
`Serial1`. STM32duino 3.x uses concrete `Uart` from `<Serial.h>`; the example falls back to `HardwareSerial` for 2.x. Check pin routing and UART voltage compatibility before connecting.
The baud must match the firmware build; there is no automatic baud detection
or switching.

Two binding modes are available:

| Call | Serial ownership |
| --- | --- |
| `ESPLink.begin(port, baud, timeoutMs)` | Starts the serial object with `begin(baud)` and calls its `end()` when releasing the binding. |
| `ESPLink.begin(static_cast<Stream&>(port), timeoutMs)` | Borrows an already configured Stream; never calls that object's `begin()` or `end()`. |
| `ESPLink.begin()` | Reuses the previous explicit binding with a 10-second startup budget; fails if none has been configured. |

Use borrowed mode when the application needs to configure pins, receive
buffers or other driver options itself:

```cpp
port.begin(115200);  // Apply any core-specific configuration first.
ESPLink.begin(static_cast<Stream&>(port), 10000);
```

There is no implicit `Serial`, `Serial1` or `Serial2` binding. Keep ESPLink alive until all calls have completed; its selected serial object
must outlive the ESPLink object, including destruction. Use
the shared global `ESPLink` instance for WiFi and BLE; do not read, write or
reconfigure its serial port while a binding is active. Borrowed mode leaves
the driver running after `ESPLink.end()`; its owner closes it when appropriate.

## Scheduling and buffers

The generic backend uses Arduino `Stream`, `millis()` and `yield()` and has no
FreeRTOS dependency. Call `ESPLink.poll()` frequently from `loop()` to receive
frames, retry requests and maintain heartbeats. Synchronous `begin()` and
`request()` also pump the transport while waiting. Call `WiFi.poll()` and
`BLE.poll()` when using those libraries so their host-side events and state
machines run. Long application delays can overflow the serial driver's RX
buffer or expire the session.

Generic access is limited to one application context. Do not call ESPLink,
WiFi or BLE from an interrupt, multiple tasks, or a nested callback that starts
another operation on the same library. Reentrancy checks are not task mutexes.
The generic backend does not resize the core's UART RX buffer. Configure that
buffer for the chosen baud and the longest interval between servicing it;
protocol framing cannot recover bytes after the UART driver has dropped them.

CI13XX selects an optional FreeRTOS backend with a separate I/O task. It pumps
the serial stream automatically and never invokes user callbacks. Its managed
HardwareSerial binding installs a 4096-byte RX buffer and restores the UART's
default storage on release. A borrowed Stream retains its existing driver
configuration. Initialize before other tasks use ESPLink and serialize
`begin()`/`end()` in the application. The CI backend serializes requests;
WiFi objects and the BLE Host still need application-level serialization.

Shared state remains allocated after `end()` for reconnects and so queued CI requests cannot
refer to freed locks or buffers. A CI worker stop timeout retains the active
binding until a later `begin()` or `end()` can finish stopping it; do not
reclaim or reconfigure the serial object in that interval. `end()` does not
free the retained protocol state on either backend. Destruction stops the
worker and releases the state; destroy an ESPLink object only after all callers
have finished, never from an interrupt, callback or its own I/O task. A managed
serial object must remain alive until shutdown and destruction finish.

For preliminary RAM budgeting, allow roughly 7–8 KiB for the generic state,
or roughly 12 KiB for CI13XX state plus its 8 KiB I/O task stack. These are
estimates, not measured runtime heap guarantees. Requests and handshakes now use
the retained frame directly; the encoder still needs about 1 KiB of local stack
space. The CI table adds 1 KiB of read-only data (SRAM with the current linker),
and its bulk RX scratch defaults to 64 bytes. UART driver storage,
application stacks, network buffers and the BLE Host need additional RAM.
Measure heap and stack headroom with the actual application workload.

## CI1306 performance configuration

On CI1306, use the concrete HardwareSerial binding, for example
`ESPLink.begin(Serial2, 115200)`, to enable the UART optimizations. Borrowing it as
a `Stream` preserves its driver configuration and does not automatically select
bulk reads, resize RX storage or enable DMA. Match the baud to the C3 firmware.

Defaults and limits are defined in [ESPLinkConfig.h](src/ESPLinkConfig.h) and
[C3Protocol.h](src/C3Protocol.h):

| Macro | Default | Purpose |
| --- | --- | --- |
| `ESPLINK_CI13XX_RTOS` | `1` on CI13XX; otherwise `0` | Background I/O task; disabling it requires frequent `poll()`. |
| `ESPLINK_CI13XX_UART_BULK_RX`, `ESPLINK_CI13XX_TX_DMA`, `ESPLINK_CI13XX_TASK_NOTIFY` | Each `1` | Bounded bulk reads, opportunistic TX DMA and task notification wakeups. Set individually to `0` to disable. |
| `ESPLINK_CI13XX_RX_BUFFER_SIZE` | `4096` bytes | Managed UART ring; power of two from 2048 to 32768. |
| `ESPLINK_CI13XX_RX_CHUNK_SIZE` | `64` bytes | Maximum batch read; range 1 to 64 to bound interrupt masking. |
| `ESPLINK_CI13XX_DMA_THRESHOLD` | `64` bytes | Minimum encoded write length eligible for DMA; must be positive. |
| `ESPLINK_CI13XX_TASK_STACK_WORDS`, `ESPLINK_CI13XX_TASK_PRIORITY` | `2048` words; `3` | About 8 KiB of worker stack on CI1306; measure stack headroom and audio-task latency before changing. Priority must be below `configMAX_PRIORITIES`. |
| `ESPLINK_CI13XX_IDLE_WAIT_MS`, `ESPLINK_CI13XX_POLL_MS` | `1000`; `2` ms | Maximum notified idle wait and fallback polling interval; each 1 to 1000. New requests and RX wake the notified worker immediately. |
| `ESPLINK_RX_PUMP_BUDGET` | `4096` bytes | Per-pass receive budget on both backends; range 64 to 65536. |
| `C3_CRC32C_USE_TABLE` | `1` on CI13XX; otherwise `0` | Table CRC32C requires C++17 and adds 1 KiB of read-only data, loaded into SRAM by the current CI linker. `0` uses the equivalent bitwise CRC. |

The CI-specific UART/task settings apply only with `ESPLINK_CI13XX_RTOS=1`.
DMA acquisition uses `PeripheralManager`; if its channel is unavailable, TX
falls back to interrupts. The managed UART releases its DMA resource on shutdown.

Pass overrides to **all C++ translation units**, including the library sources.
A `#define` in the sketch alone does not configure separately compiled files and
can produce inconsistent header definitions. For Arduino CLI, append to the
platform's global `compiler.cpp.extra_flags`, preserving any existing flags:

```text
--build-property "compiler.cpp.extra_flags=-DESPLINK_CI13XX_TX_DMA=0 -DC3_CRC32C_USE_TABLE=0"
```

Perform a clean rebuild after changing these flags. Keep protocol framing, CRC
and retry handling enabled. Use [LinkDiagnostics](examples/LinkDiagnostics/LinkDiagnostics.ino)
to compare Echo payload throughput, round-trip latency, retries, UART overruns
and worker wakeups under the actual WiFi/BLE and ASR/AEC workload. Native tests
and successful compilation do not establish peak throughput or interrupt latency
on physical CI1306 hardware.

## Delivery semantics and timeouts

COBS framing and CRC32C detect corrupt frames and restore packet alignment.
One application RPC is in flight at a time. Retries reuse the request ID;
the firmware replays a cached result instead of repeating an operation.
Receiving a transport ACK does not mean that the operation completed.

`request()` returns a `c3::Status`. Its response-size reference is input
capacity and output length. `BufferTooSmall` reports the required response
size; the operation has already run and must not be blindly repeated.
`OutcomeUnknown` means transmission was attempted but its result is uncertain.
The session becomes invalid. Reconnect and recover application state rather
than replaying the entire operation. TCP/TLS writes can also return an accepted
prefix; interpret both the count and error before deciding how to continue.

Partial writes and retries share the RPC's remaining timeout; they do not
restart the total deadline. Arduino Stream has no universal timed-write API,
so an individual `Stream.write()` must itself return within a finite bound.
ESPLink cannot interrupt a custom Stream that blocks inside `write()`;
`Stream.setTimeout()` alone does not guarantee a write deadline on every core.
The managed CI13XX binding uses its driver's explicit timed-write API.

Session changes invalidate socket, certificate and BLE handles. The firmware
does not persist request results across power loss. CRC is not authentication
or encryption; UART bytes include application data and WiFi credentials.

Long synchronous network operations occupy the shared RPC slot and delay HCI
reads. PING/PONG liveness remains independent on C3, but it cannot substitute
for timely BLE Host processing. Async network operations and reserved HCI
capacity are future work.

## Diagnostics and tests

`ready()`, `session()`, `lastError()`, `capabilities()` and `stats()` expose link
state, negotiated limits, and CRC/retry/timeout counters. `ping()` is a complete
Echo RPC rather than only a byte-level heartbeat.

Native codec and lifecycle tests are in [tests](tests/README.md). They run
production source with a native compiler. Radio behavior, UART overruns,
runtime memory headroom and real scheduling remain to be verified on hardware.
