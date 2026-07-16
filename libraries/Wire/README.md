# Wire for CI13XX

This implementation exposes the CI13XX `IIC0` peripheral as an
Arduino-compatible, polling master. The selected variant supplies the only
supported route:

| Chip/profile | SDA | SCL | Shared peripheral |
| --- | --- | --- | --- |
| CI1302 / CI-D02GS02S | pin 2 / PA2 | pin 3 / PA3 | `Serial1` |
| CI1303 / CI-D03GS02S | pin 2 / PA2 | pin 3 / PA3 | `Serial1` |
| CI1306 / CI-D06GT01D | pin 15 / PB7 | pin 16 / PC0 | `Serial1` |

A sketch must choose one peripheral at a time and provide external I2C pull-up
resistors.

The common register-read sequence is supported:

```cpp
Wire.beginTransmission(address);
Wire.write(reg);
Wire.endTransmission(false);
Wire.requestFrom(address, count);
```

The no-STOP write is deferred and submitted with the read through the SDK's
`iic_master_multi_transmission()` API. The SDK cannot safely issue a standalone
zero-byte polling write, so address-only `endTransmission()` (the pattern used
by many I2C scanner sketches) returns status 4. The SDK polling receiver also
always sends STOP after a read; `requestFrom(..., false)` cannot retain the bus.

Transfers are buffered to 32 bytes and clocks from 10 kHz through 400 kHz are
accepted. Wire does not arbitrate IIC0 access with SDK components configured to
use an external I2C codec.

Examples:

- `MasterWrite` sends a register/value pair and reports the Arduino Wire status.
- `RegisterRead` demonstrates a write followed by a repeated START read.

An I2C scanner example is intentionally not included: the vendor polling API
does not terminate an address-only write, while substituting a dummy data byte
could modify an unknown device. Use a known address/register from the device
data sheet instead.
