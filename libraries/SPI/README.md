# SPI for CI13XX

These CI13XX profiles do not expose a general-purpose hardware SPI controller.
`QSPI0` is the boot/model/user Flash interface and must not be repurposed. This
library is therefore an explicit GPIO software SPI master, not a wrapper around
the on-chip Flash bus.

The default route for all three variants is:

| Signal | Arduino pin | Chip PAD |
| --- | ---: | --- |
| SCK | 5 | PA5 |
| MISO | 2 | PA2 |
| MOSI | 4 | PA4 |
| SS | 3 | PA3 |

These pads share the board's IIS header and are free in the packaged
`USE_NULL=1`, `USE_IIS1_OUT_PRE_RSLT_AUDIO=0` profile. They cannot be shared
with IIS capture/record output if that SDK profile is changed. PA4 is sampled
as `PG_EN` during reset, so an attached peripheral must not drive PA4 while the
CI13XX is resetting; using it as master MOSI with a normal slave input is safe
after startup.

`SPI.begin(sck, miso, mosi, ss)` can select other GPIO pins. Passing `-1` omits
an optional MISO, MOSI, or SS signal. Chip select is configured high by
`begin()`, but—as with the standard Arduino transaction API—the sketch must
drive it low/high around each device transfer.

`begin()` atomically acquires the software-SPI resource and every selected
pad. It returns `false` without changing pin muxes when Wire, a UART, PWM, or
another peripheral owns any of them. `end()` returns all acquired resources.

Implemented APIs include `SPISettings`, modes 0–3, MSB/LSB order,
`beginTransaction()` / `endTransaction()`, byte/16-bit/32-bit transfers,
in-place buffers, separate TX/RX buffers, and write helpers. Each transaction
caches the PL061 masked-data register addresses and masks; the bit loop uses
direct register access and the core timer instead of `digitalWrite()`,
`digitalRead()` and integer-microsecond delays. Requested clocks above 4 MHz
are capped. Instruction and interrupt overhead can still make the measured
clock lower than requested and add jitter.

Buffer transfers stay in the register hot path and cooperatively yield every
64 bytes by default. Define `SPI_COOPERATIVE_CHUNK_BYTES` to another positive
value at build time to tune that interval. SD block transfers use this bulk
path. There is no DMA, hardware chip select, slave mode, or transaction-level
multi-task arbitration.

The packaged CI1302/CI1303/CI1306 routes were audited before this optimization:
no general-purpose hardware SPI controller has both a safe pin route and a
conflict-free resource assignment. QSPI0 remains reserved for boot/model/user
Flash, so no speculative hardware backend is exposed.
