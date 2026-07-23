# Wire for CI13XX

This implementation exposes the CI13XX `IIC0` controller through the Arduino
Wire API. It supports polling controller/master transfers, interrupt-driven
peripheral/slave callbacks, repeated-start register reads, and recoverable
transfer timeouts.

| Chip/profile | SDA | SCL | Shared peripheral |
| --- | --- | --- | --- |
| CI1302 / CI-D02GS02S | pin 2 / PA2 | pin 3 / PA3 | `Serial1` |
| CI1303 / CI-D03GS02S | pin 2 / PA2 | pin 3 / PA3 | `Serial1` |
| CI1306 / CI-D06GT01D | pin 15 / PB7 | pin 16 / PC0 | `Serial1` |

`Wire.begin()` atomically acquires IIC0 and both pads. If `Serial1` already
owns them, it returns `false` without changing any mux register. Call
`Wire.end()` or `Serial1.end()` before switching functions. External I2C
pull-up resistors are still required; the core also enables the weak internal
pull-ups and open-drain mode.

## Controller mode

The common repeated-start register read is supported:

```cpp
Wire.beginTransmission(address);
Wire.write(reg);
Wire.endTransmission(false);
Wire.requestFrom(address, count);
```

The no-STOP write is deferred and executed together with the following read.
`Wire.probe(address)` and an empty `endTransmission()` send only START and the
7-bit address, then STOP; no dummy register byte is written.

The transfer timeout defaults to 25 ms and follows the Arduino Wire timeout
API:

```cpp
Wire.setWireTimeout(3000, true);
Wire.clearWireTimeoutFlag();
if (Wire.endTransmission() == 5 && Wire.getWireTimeoutFlag()) {
  // The controller was reset and can be used again.
}
```

A zero timeout disables the software deadline. With reset enabled, a timeout
issues STOP, resets IIC0, restores its pins and clock, and latches the timeout
flag. `requestFrom(..., false)` is accepted for source compatibility, but the
current CI130X controller wrapper always releases the bus after a read.

## Peripheral mode

```cpp
void receiveEvent(int count) {
  while (count-- && Wire.available()) {
    consume(Wire.read());
  }
}

void requestEvent() {
  Wire.write(responseByte);
}

void setup() {
  Wire.onReceive(receiveEvent);
  Wire.onRequest(requestEvent);
  Wire.begin(0x42);
}
```

Callbacks run in the IIC interrupt context. Keep them short: do not delay,
print, allocate memory, access SD, or call blocking communication APIs. A write
followed by a repeated START is finalized before `onRequest()` runs.

Transfers use a 64-byte buffer by default. Define `I2C_BUFFER_LENGTH` before
including `Wire.h` to choose another size. Clocks from 10 kHz through 400 kHz
are accepted.

Examples:

- `MasterWrite`: register/value write and status handling.
- `RegisterRead`: write followed by a repeated-start read.
- `Scanner`: non-destructive 7-bit address scan.
- `PeripheralCallbacks`: interrupt-driven peripheral receive/request handling.
