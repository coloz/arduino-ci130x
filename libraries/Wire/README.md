# Wire for CI13XX

This implementation exposes the CI13XX `IIC0` controller through the Arduino
Wire API. Controller/master transfers use an interrupt-driven state machine;
synchronous calls sleep on task notifications, while asynchronous transfers
complete through the Arduino event dispatcher. Peripheral/slave callbacks,
repeated starts, bus hold and recoverable timeouts are supported as well.

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

The no-STOP write is transmitted immediately and leaves the bus owned by
IIC0. The following read emits a repeated START without an intervening STOP.
`Wire.probe(address)` and an empty `endTransmission()` send only START and the
7-bit address, then STOP; no dummy register byte is written.

For non-blocking controller transfers, transmit data is copied into Wire's
internal buffer and receive data is copied to the caller buffer before the
callback runs:

```cpp
uint8_t reg = 0x00;
uint8_t value[2];

void transferDone(uint8_t status, size_t transferred, void *) {
  if (status == 0 && transferred == sizeof(value)) consume(value);
}

Wire.transferAsync(0x40, &reg, 1, value, sizeof(value), true,
                   transferDone);
```

The callback runs in Arduino task context, never in `IIC0_IRQHandler()`.
Only one controller transaction may be active at a time; `transferBusy()` and
`cancelTransfer()` expose that state.

The transfer timeout defaults to 25 ms and follows the Arduino Wire timeout
API, with a non-disableable 25 ms hardware fail-safe:

```cpp
Wire.setWireTimeout(3000, true);
Wire.clearWireTimeoutFlag();
if (Wire.endTransmission() == 5 && Wire.getWireTimeoutFlag()) {
  // The controller was reset and can be used again.
}
```

A zero timeout disables the user deadline, but the hardware fail-safe remains
active so a missing ready/transfer bit cannot hang the task forever. With reset
enabled, a timeout issues STOP and performs recovery: IIC0 is detached, SCL is
pulsed up to nine times, an explicit STOP is generated, then the pins,
controller, clock and IRQ mask are restored. `recoveryCount()` reports recovery
attempts. `requestFrom(..., false)` intentionally keeps the bus held for the
next transaction.

Controller status values are: 0 success, 1 buffer overflow, 2 address NACK,
3 data NACK, 4 other/bus error, 5 timeout, 6 busy, 7 arbitration lost,
8 recovery failed and 9 deferred-callback queue full.

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

`onReceive()` is deferred to Arduino task context; received bytes are copied to
a separate buffer before the ISR returns. `onReceiveISR()` is the explicit
advanced form and must not delay, print, allocate, access SD, or call blocking
APIs. Because a peripheral must produce the first response byte immediately,
`onRequest()` and the explicit alias `onRequestISR()` both run in the IIC ISR;
prepare or copy only a short response there. A write followed by a repeated
START is finalized before the request callback runs. `callbackDrops()` reports
receive callbacks lost when the dispatcher or callback buffer is full.

Transfers use a 64-byte buffer by default. Define `I2C_BUFFER_LENGTH` before
including `Wire.h` to choose another size. Clocks from 10 kHz through 400 kHz
are accepted.

Examples:

- `MasterWrite`: register/value write and status handling.
- `RegisterRead`: write followed by a repeated-start read.
- `AsyncTransfer`: non-blocking register read with a deferred completion callback.
- `Scanner`: non-destructive 7-bit address scan.
- `PeripheralCallbacks`: interrupt-driven peripheral receive/request handling.
