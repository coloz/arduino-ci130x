> The portable package and entry point are documented in [README.md](README.md).

# CI1306 wiring and examples for BLE

This port uses the real ArduinoBLE 2.1.0 Host, pinned in [UPSTREAM.md](UPSTREAM.md).
It is not a BLE UART-service-only wrapper. Central and peripheral GATT logic,
service discovery, characteristic read/write, descriptors, notifications,
indications and pairing processing come from ArduinoBLE. The CI1306 runs that
Host; the ESP32-C3 runs the controller using the companion `wifi_c3` firmware.

## Connect and start

By default, connect CI1306 Serial2 TX (PB1) to C3 UART RX, Serial2 RX (PB2) to C3
UART TX, and GND to GND. The link uses 921600 baud.
Supply the C3 separately as required by its board. Use the configured pins in
the companion firmware. The communication path uses only TX/RX/GND.
Do not write logs or application bytes directly to the selected link UART.

```cpp
#include <BLE.h>

void setup() {
  Serial.begin(115200); // diagnostic UART
  if (!BLE.begin()) return;
  // Define services, add characteristics, then BLE.advertise() or BLE.scan().
}
void loop() {
  BLE.poll();
  delay(1);
}
```

`BLE.begin()` starts Serial2 at 921600 automatically. `BLE.h` includes both
`ESPLink.h` and `BLEESPLink.h`. To change the link configuration, call
`ESPLink.begin(Serial2, 115200)` before `BLE.begin()` or `WiFi.begin()` and use
matching C3 firmware. A configured ESPLink is reused, including its CI1306
UART/DMA and FreeRTOS optimizations. WiFi and BLE share the same RPC link.

Use the four CI1306 examples supplied here. `CI1306Peripheral` and
`CI1306Central` form a counter/notification pair.
`CI1306ReadCallback` demonstrates a value prepared by CI1306 on an ATT read.
`CI1306EncryptedValue` is a pairing/encryption interoperability test.

## Scheduling and recovery

Call ArduinoBLE APIs from **one application task**. ESPLink's I/O task and
RPC serialization do not make the ArduinoBLE Host reentrant. Keep `BLE.poll()`
frequent; the Host does not run in ESPLink's I/O task. Long WiFi requests,
user callbacks and blocking sketch work delay BLE Host processing. Measure
these delays with the intended voice-processing workload.

Read/write callbacks execute synchronously while the Host processes packets.
Keep them short. An on-read callback can update a value locally before the
response is constructed; avoid nested BLE/WiFi calls that wait for another
controller transaction. Using `writeValue()` on a read-only characteristic
to update its local value is supported, as in the read-callback example.

`BLEESPLink.healthy()` checks controller transport/session health.
`BLEESPLink.lastError()` reports a negative ESPLink status or one of the
`BLE_ESPLINK_*` errors. The first failed HCI read/write or changed session
makes the transport fail until it is explicitly restarted. The Host clears
stale peers when it next polls; a normal absence of a BLE connection does not
make the transport unhealthy.

After restoring the UART link, use `BLE.end()`, reinitialize ESPLink if needed,
then `BLE.begin()` and rebuild/re-add the GATT database and advertising/scanning
configuration. Release/recreate remote service/characteristic objects after a
disconnect or controller reset. No replay of an uncertain HCI write occurs in
this transport: ESPLink owns transaction retries and deduplication.

`BLE.end()` shuts down the BLE controller only in the matching ESPLink session;
it does not close WiFi. Do not call `BLE.begin()` repeatedly without a matching
`BLE.end()`.

## Features and boundaries

| Capability | Implementation / boundary |
| --- | --- |
| Central/peripheral | Native ArduinoBLE scan/connect/discover and local GATT APIs |
| Notify / indicate | Native subscription support; ESPLink port bounds indication waits by BLE timeout |
| Read/write / descriptors | Native APIs; same UUID/property semantics as ArduinoBLE |
| On-read callback | Runs on CI1306 before sending ATT response; requires timely polling |
| WiFi + BLE | Shared RPC allows both; throughput and voice-task latency require hardware testing |
| MTU | Starts at 23. Upstream Host uses small HCI/ATT buffers; this port caps its ATT MTU at 242. No 512-byte MTU claim |
| Multiple peers | This ESPLink port accepts one peer. Upstream CCCD, SMP and prepared-write buffers share state; simultaneous peers require a later per-peer Host refactor |
| Pairing/encryption | Upstream LE Secure Connections and encrypted-attribute code included; phone interoperability/security tests remain required |
| Persistent bonding | Upstream IRK/LTK storage callbacks exist. This port does not install a persistent key store or advertise bonding without all callbacks |
| Numeric comparison | Upstream display/confirmation callback APIs included; test with real user confirmation. No automatic acceptance example |
| Classic Bluetooth | Not supported by ESP32-C3: no BluetoothSerial/SPP/A2DP/HFP |
| BLE5 advanced features | Controller capability does not imply ArduinoBLE API support. Extended/periodic advertising, coded PHY controls, mesh and CoC are not exposed by this initial port |
| Espressif BLEDevice API | Different from ArduinoBLE; not provided as a misleading name alias |

The ESPLink port checks encryption for characteristic read/write, read-by-type,
prepared writes and notification/indication delivery. An unencrypted ATT
request receives an error and must be retried after pairing; it is not stored
for an unsolicited response later. These guards are not a complete SMP audit.

The encrypted example uses a four-byte value and no persistent keys. A
NoInputNoOutput device cannot establish MITM protection merely by requesting
encryption. The upstream pairing Host is not being presented as a fully audited
security product. Production credentials/authorization require independent
pairing, reconnect, bond-storage and failure-path validation.

## Transport details

HCI commands/ACL data are sent as complete H4 packets with `HciWrite`.
`HciRead` pulls at most 256 raw H4 bytes per RPC; packets can cross RPC boundaries
and several packets may share one reply. Replies are buffered locally. A status
request checks the controller enabled/fault/drop counters at startup and
periodically. Any controller queue overflow is treated as a fatal HCI-session
error, not as a harmless dropped notification.

The C3 protocol framing, CRC, bounded payload, retries and request deduplication
are implemented by ESPLink. HCI command completion and ATT responses
are separate from serial delivery. A successful notification write does not mean
the remote application consumed the value.

## Validation

See [tests](tests/README.md) for repeatable native transport tests. Hardware
validation remains required: run the peripheral and central examples, then check
notifications, read callbacks, pairing, WiFi concurrency, UART faults and
independent chip resets with the intended CI1306 voice workload.
