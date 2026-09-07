# Upstream and local changes

This directory vendors Arduino's **ArduinoBLE 2.1.0** release at commit
`281377b3588814e4c174c08ec711e10e35b1c9f9` (2026-06-22):
https://github.com/arduino-libraries/ArduinoBLE/tree/281377b3588814e4c174c08ec711e10e35b1c9f9

`src/`, `LICENSE`, `README-UPSTREAM.md`, `CHANGELOG`, and `keywords.txt` originate from
that revision. Original copyright and license notices are retained. Most source
files use LGPL-2.1-or-later; the unused Nina SPI transport carries its original
MPL-2.0 notice. New ESPLink transport files use LGPL-2.1-or-later as marked in
their headers. `README.md` describes this portable port; the upstream README is
retained as historical documentation, not a claim that all upstream boards or
features are supported by this package.

The adaptation preserves ArduinoBLE's Host API and implementation. HCI uses the
project's reliable ESPLink RPC to an ESP32-C3 controller. The Arduino host runs GAP, GATT,
ATT and security processing; ESP32-C3 runs its controller beside the WiFi stack.

Local changes:

- `library.properties`: independent BLE package identity, ESPLink dependency and
  architecture-independent discovery; not an official Arduino-maintained package.
- Canonical `BLE.h`, compatibility `ArduinoBLE.h` forwarder, and `BLEConfig.h`: all
  architectures select the external ESPLink controller. Upstream board-specific
  transport and reset handling are retained but disabled. `BLE.h` also exports
  ESPLink and BLEESPLink so sketches use one include and automatic link startup.
- `BLEESPLink.h` and `utility/HCIESPLinkTransport.*`: reliable HCI transport,
  bounded reads/writes, diagnostics and controller-session invalidation.
- ESPLink HCI/ATT lifecycle and timeout guards: bounded credit and
  indication waits, write-error propagation, stale peer cleanup.
- ESPLink HCI/L2CAP packet bounds and 242-byte ATT MTU cap.
- HCI connection handles use the 0xffff invalid sentinel instead of a peer-array
  size check; ESPLink localAuthreq returns its computed bonding configuration.
- ESPLink single-peer limit because upstream CCCD, prepared-write and SMP buffers
  are shared; encrypted read/write requests fail until the peer retries on an
  encrypted connection, without delayed cross-session replay.
- Encryption gates on notifications, indications, read-by-type and prepared
  writes; check HCI encryption-change status before marking a link encrypted.
- ATT attribute-handle/CCCD bounds, minimum MTU and packet-length checks,
  excess-peer disconnect and prepared-write allocation failure guards.
- Generic and STM32 examples, retained CI1306 variants, architecture-neutral
  transport fault tests and port documentation.
- Cooperative ESPLink polling in HCI transport session checks.
- Shared bounded BLE address formatting without variadic `sprintf`, avoiding the
  CI1306 vendor ROM wrapper's argument clobber and the resulting 18-byte buffer
  overflow. Both local and discovered-device addresses use the same formatter.

Compare with the pinned upstream when updating. This port is not an Arduino
upstream release and the Arduino-host/ESP32-C3 combination has not been Bluetooth SIG
certified by this project.
