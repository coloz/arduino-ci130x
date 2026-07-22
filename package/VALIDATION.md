# Boards Manager validation

Validation date: 2026-07-20
Host: Windows x64
Arduino IDE: 2.3.7  
Arduino CLI: 1.3.1 (bundled with the installed IDE)

The package was tested with a new Arduino data, downloads and user directory.
No source-tree hardware link, `platform.local.txt`, existing Arduino15 core or
preinstalled ChipIntelli tool was available to the test instance.

Validated flow:

1. Serve the release-candidate assets and a local-URL copy of
   `package_chipintelli_index.json` over HTTP.
2. Download and parse `package_chipintelli_index.json`.
3. Discover `chipintelli:ci13xx@1.0.0`.
4. Download and checksum the 133.32 MiB `chipintelli:riscv-gcc@9.2.0` tool.
5. Download and checksum the `chipintelli:ci13xx@1.0.0` platform.
6. Install both packages and resolve the CI1302, CI1303 and CI1306 FQBNs.
7. Compile, link and run the CI13XX dual-image post-build step for all 16
   installed platform and library examples on CI1306.
8. Compile the installed `CI13XXSmoke` example for CI1302 and CI1303.

Result: 18 compiled, 18 passed, 0 failed.

The comprehensive `CI13XXSmoke` result was 137,083 bytes of program storage and
119,024 bytes of reported dynamic memory. Vendor SDK LTO objects emit existing
type-mismatch warnings when all warnings are enabled; the final link and
`user_code.bin` generation complete successfully.

The validation found and fixed three package-only issues: UTF-8 BOM rejection
in the JSON index, the tool archive wrapper directory being removed during
installation, and the legacy GCC failing when its C++ header path reaches about
210 characters. The Boards Manager tool ID is intentionally the shorter
`riscv-gcc` while the compiler executable prefix remains
`riscv-nuclei-elf-`.

This validation covers Boards Manager installation and compilation. It does not
claim physical-board upload or runtime hardware validation.

## citool-cli integration

The platform consumes independently released `citool-cli@1.0.1` as a Windows
x64 Boards Manager tool dependency; its source is not stored or built in the
Arduino repository. A pre-build hook supplies only missing sketch-local
resource partitions. Post-build processing creates `user_code.bin`, invokes
`citool-cli compose --chip <board>`, and validates the complete V2 firmware
with `inspect`. The CI130X FW_V2 Bootloader is embedded in `citool-cli`; the
platform archive no longer contains or requires `Firmware_V2.0.0.bin`.
The upload and programmer recipes then invoke `citool-cli flash` on that complete
image. Cargo unit tests and package/index checks cover this integration. The
CI1303 physical-board upload and I2C runtime path is validated below; CI1302,
CI1306, audio and offline-ASR hardware regression remain outstanding.

Before compact automatic partition layout was enabled, the updated Arduino
recipe was exercised with `CI13XXSmoke` for CI1302, CI1303 and CI1306. All three
builds copied the default resources on first use, produced a 212,992-byte
`user_code.bin`, composed a 1,848,855-byte complete firmware using the former
fixed User reservation, and passed strict V2 table and per-partition CRC
inspection. CI1302
placed NV data at `0x1FC000`; CI1303 and CI1306 placed it at `0x3FC000`.

The packaged `ci13xx@1.0.0`, `riscv-gcc@9.2.0` and `citool-cli@1.0.0`
archives were also installed into a fresh, short-path Arduino data directory.
Installed-platform CI1302, CI1303 and CI1306 builds completed the resource-copy,
template-free compose and inspect flow with no local source-tree platform or tool
fallback. The installed platform was checked to contain the four default
partition resources and no complete firmware template. A short data directory
remains necessary because of the legacy GCC path-length limitation described
above.

For the CI1302 smoke-test inputs, the former fixed-reservation template-free
compose produced the same 1,848,855-byte image and SHA-256 as the previous
validated template-based flow.

## Source SDK and variant validation

On 2026-07-21, the source-enabled development platform was tested with
`BlinkPA5` for CI1302, CI1303 and CI1306. Each FQBN compiled all 138 packaged
SDK translation units into non-LTO objects, linked them directly, and completed
the dual-image post-build and firmware compose steps. The generated board
dependencies selected `CI-D02GS02S.c`, `CI-D03GS02S.c` and `CI-D06GT01D.c`,
respectively.

All three link maps contain zero references to `libci13xx_sdk.a`, and all 138
source-built objects per variant contain no `.gnu.lto_*` sections. Components
for which the vendor SDK contains no source remain linked from 12 original GCC
9.2.0 archives. This validation used an isolated manual hardware platform; it
does not claim physical-board runtime validation.

## Internal-RC clock and user-code container validation

On 2026-07-22, the CI1302 source platform was rebuilt with the default
`Clock=internal` board option. Both the 138 SDK translation units and Arduino
translation units received `USE_EXTERNAL_CRYSTAL_OSC=0`; the expanded board
properties reported a 200 MHz CPU clock. The CI1302/CI1303 external-crystal
option reported `USE_EXTERNAL_CRYSTAL_OSC=1` and 246 MHz, while CI1306 retained
its 246 MHz external-crystal profile.

The CI1302 build generated a 167,664-byte `user_code.bin` containing file IDs
0 and 1, then composed and inspected a 1,848,855-byte complete V2 firmware using
the former fixed User reservation.
The independently built `citool-cli` validation rejects a raw `[0]code.bin` as
a user-code partition, preventing the packaging mistake reproduced during the
hardware diagnosis.

## Automatic partition layout validation

On 2026-07-22, the release `citool-cli` was rebuilt with compact automatic
layout enabled. A 208,368-byte Arduino `user_code.bin` and the packaged default
resources produced and passed `inspect` as a 1,598,999-byte FW_V2 image for both
CI1302 and CI1303. The calculated offsets were User `0x4000`, ASR `0x37000`,
DNN `0x3C000`, Voice `0x80000` and UserFile `0xFE000`; NV remained at
`0x1FC000` for CI1302 and `0x3FC000` for CI1303.

With the same CI1302 resources, a 1,410,376-byte User input aligned to
`0x159000` and was rejected before output because the dynamic User Flash layout
limit was `0xA8000`.

## Restored Arduino User/SRAM limit

On 2026-07-22, the chip vendor confirmed that the complete dual-core
`user_code.bin` must not exceed the SDK's SRAM-loading limit. Arduino therefore
retains the original 448 KiB (`0x70000`, 458,752-byte) hard limit. The board
metadata keeps the corresponding conservative host-program limit, and
`postbuild.ps1` checks the exact merged container size before calling
`citool-cli compose`.

The earlier larger-container experiment is not part of the supported platform.
The same 462,688-byte test container is now rejected before complete firmware is
generated, while the normal PA4/UART sketches for CI1302, CI1303 and CI1306
continue through merge, dynamic Flash layout and strict inspection. Dynamic
4 KiB Flash address calculation remains enabled only within the fixed User
limit.

The dynamic-layout uploader was versioned as `citool-cli@1.0.1` so Boards
Manager cannot reuse the older fixed-capacity `1.0.0` installation. Its locked
test and clippy run passed all 22 tests. The clean GitHub Actions Windows x64
release archive is 435,506 bytes with SHA-256
`434bdcf9369aedbf19c6fe60a002df636a751c71148db63ddcd49378c661db0c`.
The regenerated Arduino `1.0.2` index depends on that exact tool version and
archive.

## CI1303 physical-board upload and SSD1306 validation

On 2026-07-22, a CI1303 on COM31 was built from the workspace `arduino-ci130x`
platform with the internal-RC profile and flashed with the workspace
`citool-cli@1.0.1`. The tool connected to MaskROM, loaded the embedded CI130X
update agent, erased and wrote the 1,545,751-byte complete firmware, verified
its CRC and reset the device successfully.

The sketch initialized UART0 at 921600 baud and IIC0 on PA2/PA3, required an
SSD1306 ACK at `0x3C` or `0x3D`, initialized U8g2 and refreshed a 128x64 text
screen once per second. After reset, COM31 returned consecutive `OLED frame`
messages from 14 through 20. Since the loop is entered only after the SSD1306
address probe and U8g2 initialization succeed, this validates the CI1303
Arduino startup, full-firmware flash, UART0, Wire address probe and SSD1306
refresh path. Display contents were `CI1303 OLED`, `Hello, U8g2!`, the detected
address and a live counter.
