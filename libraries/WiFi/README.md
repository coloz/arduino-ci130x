# WiFi

Arduino networking through an ESP32-C3 serial coprocessor and `ESPLink`. Include `WiFi.h` for the familiar Arduino API. TCP/IP, DNS and TLS run on the C3; the host board owns the Arduino objects, MQTT/HTTP protocols and callbacks. The library is portable across Arduino cores, including CI1306 and STM32, and requires matching C3 bridge firmware.

`architectures=*` allows Arduino to select this library on different cores; it does not guarantee that every board has sufficient resources. Use a 32-bit MCU with a hardware UART, working C++11 standard library containers, and enough free RAM for ESPLink, per-socket buffers, the BLE Host (if used), and the application. Small AVR boards such as UNO R3 are not validated or recommended for this combined stack. Copy/install the `ESPLink`, `WiFi` and optional `BLE` directories as ordinary Arduino libraries; no CI1306 core patch or FreeRTOS dependency is required on another core.

## Connection and startup

Cross TX/RX and connect GND. Use 3.3 V UART signals at the C3 (add level translation for a 5 V host). Select the UART explicitly before using WiFi: `ESPLink.begin(yourSerial, 115200)`. There is no library-wide default `Serial2`. You may also configure a custom serial stream yourself and bind it using `ESPLink.begin(static_cast<Stream &>(yourSerial))`. The no-argument `ESPLink.begin()` only reconnects an already bound transport. Both ends must use the same baud; changing the host rate does not reconfigure the C3. TX/RX/GND provides no hardware reset or flow-control signal.

```cpp
#include <WiFi.h>

void setup() {
  Serial.begin(115200);
  // Example board UART; choose the available hardware UART and pins on your board.
  if (!ESPLink.begin(Serial1, 115200)) return;
  WiFi.begin("SSID", "PASSWORD");
  if (WiFi.waitForConnectResult(20000) == WL_CONNECTED)
    Serial.println(WiFi.localIP());
}

void loop() {
  WiFi.poll();
  delay(1);
}
```

Call `WiFi.poll()` regularly in `loop()`; it also advances the cooperative ESPLink transport and dispatches events on the calling application context. On CI1306 the existing Arduino loop additionally invokes a platform-specific poll hook; portable sketches should still poll explicitly. Network calls pump ESPLink while waiting for responses. With BLE, call `BLE.poll()` as well. Serialize library calls in one application context on cooperative platforms, and keep long application work divided into bounded steps. See `examples/PortableWiFi` for explicit STM32 UART pin selection.

## Implemented API

- `WiFiClass`: STA, AP and AP+STA modes; connect, disconnect, reconnect and auto-reconnect; status, RSSI, BSSID, MAC, channel and addresses; hostname; IPv4 DHCP/static configuration and DNS; AP configuration/status; synchronous and asynchronous scan; sleep and transmit-power options; time configuration and retrieval; polling-based events.
- `WiFiClient`: TCP connections by hostname or `IPAddress`, buffered reads and peek, partial writes, connection status, remote/local port, no-delay and keepalive options.
- `WiFiServer`: IPv4 wildcard listening, accept, available and writes to clients retained by `available()`.
- `WiFiUDP`: datagram send/receive, remote endpoint and multicast binding. Each datagram is staged separately and limited to 1472 bytes.
- `WiFiClientSecure` and `WiFiSSLClient`: C3 TLS validation using built-in roots or a supplied CA; client certificate/private key; explicit insecure mode; bounded handshake timeout. Certificates are uploaded in chunks and referenced by remote handles.
- `Network`, `NetworkClient`, `NetworkClientSecure`, `NetworkServer` and `NetworkUDP` are aliases for these implementations.

HTTP, MQTT and other stream protocols can use Arduino libraries that accept `Client` or `UDP`. The API covers a useful Arduino-compatible subset; it does not reproduce every ESP32 Arduino extension. Unsupported operations return failure and set `lastError()` rather than reporting success.

## Data and ownership semantics

Copies of a client, server or UDP object share the same connection/listener state. Reading through one copy consumes data seen by the others. Calling `stop()` closes the shared connection. Destruction of the last owner releases remote resources. A changed ESPLink session invalidates existing handles; create or reconnect objects after the link recovers.

`write()` returns the number of bytes confirmed accepted by the C3. Check this return value: a short write does not mean the entire buffer was sent. If the link reports `OutcomeUnknown`, delivery of the last unconfirmed chunk is unknown; application protocols must decide how to recover. The library never automatically retransmits an unconfirmed application write in a new session.

Client endpoint queries use the socket addresses reported by the C3, including the local address for IPv6 or an AP-side accepted connection. TCP reads use a shared 768-byte local cache. `flush()` does not discard received TCP data; `clear()` explicitly discards input. `connected()` remains true while buffered received bytes are still readable after remote EOF. TCP acceptance by the C3 does not establish delivery to the remote application.

UDP sends are atomic at `endPacket()`. A write that exceeds the 1472-byte datagram limit fails the entire pending packet; `endPacket()` then sends nothing. Calling `parsePacket()` discards the unread remainder of the previous datagram. UDP `flush()` discards the unread current packet.

Server `accept()` hands a client to the caller. Server `available()` retains clients so server `write()` can broadcast to them; its return value is the smallest accepted byte count among those clients. A client obtained with `accept()` is not part of that broadcast set.

Application code must serialize mutations and copies of the same network objects. The CI1306 transport serializes RPC calls; portable cooperative mode expects one application caller. Neither mode makes Arduino network object mutations safe for concurrent application tasks.

## TLS and time

`setCACert()`, `setCertificate()` and `setPrivateKey()` retain pointers. Keep each null-terminated PEM string alive until the connection attempt has completed. `loadCACert()`, `loadCertificate()` and `loadPrivateKey()` copy data from a stream into owned buffers. Each credential is limited to 8192 bytes. Copied secure clients share credential configuration as well as an existing connection.

The default uses the C3 built-in CA store. `setInsecure()` explicitly disables certificate verification; it should only be used when that behavior is intended. Peer hostname validation uses the hostname passed to `connect()`. Use a matching hostname for certificates that do not contain an IP address subject alternative name.

TLS validation needs a valid C3 clock. The firmware starts SNTP by default; `WiFi.configTime()` can select time servers and offsets, and `WiFi.getTime()` returns the C3 Unix time. The TLS example waits for a plausible timestamp before connecting. Time acquisition or certificate failures are surfaced as errors rather than bypassing verification.

DNS resolution has a 10-second service deadline. TCP connection timeout defaults to 10 seconds, and TLS handshake timeout defaults to 15 seconds; each of these last two stages is independently limited to 30 seconds. The host RPC budget includes DNS + TCP + TLS handshake (when used) + 3 seconds for the link, so defaults are 23 seconds for TCP and 38 seconds for TLS, with maxima of 43 and 73 seconds. These are RPC budgets, not typical operation durations. `hostByName()` defaults to a 13-second RPC deadline, and UDP destination resolution reserves 13 seconds for the 10-second DNS stage plus link margin. An explicit shorter `hostByName()` timeout is honored; if it expires after sending the request, `OutcomeUnknown` invalidates the link session as for any other RPC deadline. A timed-out C3 DNS job can finish in its dedicated worker; new DNS requests return `Busy` until that worker is free. A long DNS/connect/TLS request occupies the shared RPC channel; concurrent BLE traffic can accumulate until its bounded queue fills. Continuous BLE operation during long TLS handshakes still requires hardware validation and further scheduling work.

## Events and limits

WiFi events are derived from periodic state differences, not pushed by the C3. Polling can miss transient state changes between samples. Disconnect reason codes and per-station AP MAC addresses are unavailable in this protocol revision; unknown fields remain zero. `reasonCode()` reports unsupported. Reading `status()` before `poll()` does not consume pending state-difference events.

The scan cache holds 32 networks by default. Define `ESPLINK_WIFI_MAX_SCAN_RESULTS` when building the library to change this bound. Results beyond the cache capacity are not exposed and `lastError()` reports `BufferTooSmall`. `getAutoReconnect()` returns the last setting successfully applied through this host object; sleep and transmit-power getters query the C3.

IPv4 works with the standard Arduino `IPAddress` class. `WiFiPlatform.h` detects IPv6 and zone support from the host type: cores supporting both retain the full address and zone; IPv6-capable cores without zones reject scoped addresses; IPv4-only cores request IPv4 DNS and reject IPv6 results with `c3::Unsupported`. Addresses are never truncated to four bytes. An outgoing TCP/TLS connection whose endpoint cannot be represented by the host is closed and reports `Unsupported`. DNS and outgoing TCP/TLS accept supported address literals and hostnames. Interface IPv6 address enumeration through `WiFi.localIPv6()` is not available. Static interface configuration, AP configuration and server listening are IPv4 only. A non-wildcard server bind address is unsupported.

On hosts whose `IPAddress` supports the returned addresses, IPv6 UDP is supported through IPv6 multicast binding or an ephemeral local port (`begin(0)`), where the C3 can select the socket family for the destination. Sending IPv6 through a socket already bound to a fixed IPv4 local port returns unsupported. Binding UDP to a specific local interface address is unsupported. `localPort()` reports the requested port, so it returns zero for an ephemeral bind rather than the actual allocated port.

ESP-NOW, enterprise WiFi authentication, WPS, SmartConfig, promiscuous packet capture, FTM, mDNS service management, TLS servers, ALPN, PSK and OTA are not exposed by this revision. Use only methods declared by the supplied headers. The hardware is ESP32-C3: WiFi is 2.4 GHz and Bluetooth is BLE, without Bluetooth Classic.

## Examples and validation

- `PortableWiFi`: explicit UART selection for STM32, CI1306 and other 32-bit Arduino cores, station events and a TCP HTTP request.
- `WiFiBasics`: CI1306 wiring example for a WiFi station and TCP HTTP request.
- `WiFiTLSTest`: explicit time synchronization and HTTPS with certificate validation.
- `WiFiUDP`: UDP datagram echo.
- `MQTTClient`: MQTT using the official ArduinoMqttClient library, installed separately.
- `MQTTBLE`: MQTT callbacks and `BLE.h` GATT processing on the host (CI1306 wiring example); establish the network before starting BLE.
- [PortableMQTTBLE](examples/PortableMQTTBLE/PortableMQTTBLE.ino): the same MQTT/GATT interaction with explicit STM32, CI1306 or other Arduino UART selection; uses `esplink/output` and `esplink/ble-value` topics.

The `tests` directory contains a fake-modem suite for shared ownership, buffered reads, exact partial-write counts, session invalidation, UDP packet boundaries, credential cleanup and polling events. Run `sh libraries/WiFi/tests/run.sh` with Linux/WSL and GCC. It builds the real WiFi source with IPv4-only, IPv6 without zones, and IPv6 with zones Arduino interfaces, using AddressSanitizer and UndefinedBehaviorSanitizer. MCU example builds use `tools/test_wireless.ps1` from the repository root.

Physical UART reliability, runtime memory headroom, WiFi interoperability and TLS/BLE coexistence remain to be verified on hardware.
