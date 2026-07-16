# SPI for CI13XX

CI1306 does not expose a general-purpose hardware SPI controller. `QSPI0` is
the boot/model/user Flash interface and must not be repurposed. This library is
therefore an explicit GPIO software SPI master, not a wrapper around the
on-chip Flash bus.

The default CI-D06GT01D route is:

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
CI1306 is resetting; using it as master MOSI with a normal slave input is safe
after startup.

`SPI.begin(sck, miso, mosi, ss)` can select other GPIO pins. Passing `-1` omits
an optional MISO, MOSI, or SS signal. Chip select is configured high by
`begin()`, but—as with the standard Arduino transaction API—the sketch must
drive it low/high around each device transfer.

Implemented APIs include `SPISettings`, modes 0–3, MSB/LSB order,
`beginTransaction()` / `endTransaction()`, byte/16-bit/32-bit transfers,
in-place buffers, separate TX/RX buffers, and write helpers. Requested clocks
above 500 kHz are capped. GPIO call overhead makes the real clock lower than
the requested value, and FreeRTOS/interrupt activity can add jitter. There is
no DMA, hardware chip select, slave mode, or multi-task arbitration.
