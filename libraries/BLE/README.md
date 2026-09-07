# BLE over ESPLink

`BLE` is an Arduino library for using an external ESP32-C3 BLE controller over a
reliable UART shared with WiFi. It preserves the ArduinoBLE Host API, with the
application, GAP, GATT, ATT and pairing logic running on the Arduino host.
The companion `wifi_c3` firmware runs the controller and WiFi/network stack.

The package is called **BLE** and sketches include **`<BLE.h>`**. It vendors
ArduinoBLE 2.1.0; [UPSTREAM.md](UPSTREAM.md) records the exact revision and local
changes. This is an independent port, not an official Arduino release.
`<ArduinoBLE.h>` remains a compatibility forwarder. Do not install another
ArduinoBLE Host alongside this library in the same sketch: they define the same
public classes and globals.

## Host portability

The same ESPLink HCI transport and Host validation guards are selected on all
Arduino architectures, including STM32 and CI1306. ESPLink selects the default
UART; BLE does not touch board-specific reset pins or select an onboard
NINA/AT/Cordio controller. `architectures=*` permits other Arduino cores
to build it; it does not establish that every board has enough RAM or has passed
hardware tests. Target 32-bit hosts with sufficient free RAM for the BLE Host,
ESPLink buffers, sketch and any WiFi clients. Small AVR boards are not a validated
target.

Install the sibling **ESPLink** library as well. `BLE.h` includes both
`ESPLink.h` and `BLEESPLink.h`, so sketches need only include `BLE.h`.
`BLE.begin()` automatically starts the board's last available hardware UART at
**921600 baud**, or reuses the existing ESPLink binding. CI1306 uses `Serial2`
(TX=PB1, RX=PB2); STM32 selects the highest-numbered UART with usable board
TX/RX mappings. Connect host TX to C3 RX, host RX to C3 TX, and common GND.
Both ends must use the same baud. Keep debug output on a separate console.

```cpp
#include <BLE.h>

void setup() {
  if (!BLE.begin()) return;
  // Register services/characteristics and call BLE.advertise() or BLE.scan().
}
void loop() {
  BLE.poll();
  delay(1);
}
```

To change the UART or baud, call `ESPLink.begin(Serial2, 115200)` before
`BLE.begin()` or `WiFi.begin()`. The `Stream` overload uses an already configured
stream and preserves application pin/UART configuration. Build-wide
`ESPLINK_DEFAULT_SERIAL` and `ESPLINK_DEFAULT_BAUD` overrides and UART selection
rules are described in the [ESPLink README](../ESPLink/README.md).

See [ESPLinkPeripheral](examples/ESPLinkPeripheral/ESPLinkPeripheral.ino) and
[ESPLinkCentral](examples/ESPLinkCentral/ESPLinkCentral.ino) for paired examples.
[STM32Peripheral](examples/STM32Peripheral/STM32Peripheral.ino) uses the STM32
board's automatically selected UART and its default pins. On NUCLEO-F411RE
(STM32duino 3.0.0), this is USART6 with TX=PA11 and RX=PA12. Other boards follow
their UART pin maps. The four `CI1306*` examples remain useful board-specific
variants; see [CI1306 notes](README-CI13XX.md).

## Scheduling and recovery

Call BLE APIs from one application task. The ArduinoBLE Host is not reentrant.
`BLE.poll()` also services the cooperative ESPLink transport, so a separate
`ESPLink.poll()` call is unnecessary while BLE is running. Keep BLE polling
frequent even on CI1306, whose ESPLink backend has a FreeRTOS I/O task. Application
callbacks execute in the task polling the BLE Host, never on the C3. Long WiFi
requests, blocking callbacks and long sketch delays postpone BLE processing.
Keep read/write callbacks short and avoid nested BLE/network operations that wait
for another controller response.

`BLEESPLink.healthy()` and `BLEESPLink.lastError()` (included by `<BLE.h>`) expose
transport health. A session change, uncertain HCI write, malformed reply or C3
queue overflow makes the transport fail until explicitly restarted. Polling the
Host clears stale peers. After restoring the UART, call `BLE.end()`, reconnect
ESPLink if needed, call `BLE.begin()`, and register the GATT database and
advertising/scanning again. Release remote objects after disconnect/reset.
`BLE.end()` closes only BLE in the matching session, leaving WiFi available.

## Features and limits

| Capability | Current behavior |
| --- | --- |
| Central/peripheral and GATT | Native ArduinoBLE scan, connect, discovery, local services and descriptors |
| Read/write callbacks | Execute on the Arduino host; on-read handlers may prepare the local value before its ATT response |
| Notifications/indications | Subscription APIs; bounded indication waits and write-failure propagation |
| Connections | One peer, because upstream CCCD, prepared-write and SMP state is shared |
| MTU | Starts at 23; this port caps ATT MTU at 242 to match Host packet bounds |
| Pairing/encryption | Upstream LE Secure Connections and local encrypted-attribute guards; interoperability and a full security audit remain pending |
| Persistent bonding | Upstream IRK/LTK callback hooks; this package does not install persistent storage or advertise bonding without all required callbacks |
| WiFi coexistence | One physical ESPLink UART and one RPC at a time; long DNS/TLS/network calls can delay BLE |
| Advanced BLE 5 features | Extended/periodic advertising, coded PHY controls, mesh and CoC are not exposed by this Host API |
| Classic Bluetooth | No SPP, BluetoothSerial, A2DP or HFP on ESP32-C3 |

H4 commands and ACL packets are submitted whole; reads are a buffered byte stream,
so packets may cross RPC boundaries. ESPLink owns CRC, retries and deduplication.
HCI/ATT completion is separate from UART delivery; a successful notification
submission does not prove the remote application consumed it.

## Validation and license

[Repeatable native tests](tests/README.md) exercise transport behavior.
UART throughput, runtime memory headroom, phone interoperability and
simultaneous-radio stability remain to be verified on physical hardware.

Original Arduino copyright notices and [LICENSE](LICENSE) are retained. The Host
and ESPLink HCI adaptation are LGPL-2.1-or-later; the unused upstream Nina SPI
transport retains its MPL-2.0 notice. The historical official README is retained
as [README-UPSTREAM.md](README-UPSTREAM.md), not as a support claim for this port.
