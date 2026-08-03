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
CI1303 physical-board upload, I2C and audio runtime paths are validated below;
CI1302, CI1306 and controlled offline-ASR hardware regression remain outstanding.

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
`citool-cli` treats the User partition as opaque and therefore does not reject
a raw `[0]code.bin` or a malformed inner dual-core directory. The Arduino
post-build step must generate and validate that inner container before calling
`compose`.

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

## Arduino 1.0.3 and citool-cli 1.0.2 release validation

On 2026-07-23, `citool-cli@1.0.2` passed all 28 locked Rust tests, Clippy with
warnings denied, and the GitHub Actions Windows x64 release build. The published
458,353-byte archive has SHA-256
`b0cd5b6dc5348a5d5af70f920899f533b942a0a5347cee3385aa13fce2d4fcaa`.
This uploader adds validated MP3 insertion into `voice.bin` while retaining the
existing compose, inspect and flash behavior.
The exact uploader archive is mirrored in the public Arduino `v1.0.3` Release,
and the Boards Manager index references that public asset rather than the
private uploader repository.

The Arduino `1.0.3` package was installed through its generated Boards Manager
index into a new Arduino CLI 1.5.0 data directory. This exposed and fixed a GCC
9.2.0 C++ multilib include lookup failure under deep Windows installation paths;
the platform now supplies the normalized target include directory explicitly.

From that clean installation, 18 CI1306 sketches covering the core, Serial,
resource ownership, ASR, Audio, IR, Timer/Ticker, Watchdog, Preferences, SPI,
Servo and Wire compiled and completed firmware post-processing. The CI1302 and
CI1303 smoke sketches also passed. Result: 20 compiled, 20 passed, 0 failed and
0 compiler warnings. No new physical-board regression claim is added by this
release.

## Standard-ASR and CWSL profile build validation

On 2026-07-26, the development platform added an `Algorithm` board menu with
standard offline ASR (`USE_NULL=1`, `USE_CWSL=0`) and command-word
self-learning (`USE_NULL=0`, `USE_CWSL=1`) profiles. The selected property is
used by compilation, the profile-specific linker script, post-processing and
the second-core image selection.

`ChipIntelliCWSL/SerialLearning` was compiled with Arduino CLI 1.5.0 for CI1302,
CI1303 and CI1306 under both profiles. All 6 clean builds passed SDK source
compilation, Arduino compilation, linking, dual-core merge, complete-firmware
`compose` and an independent strict `inspect`. The final CWSL bridge source also
passed a separate `-Wall -Wextra -Werror` compile.

| Chip | Profile | Sketch / host limit | `user_code.bin` / layout limit | Final firmware |
| --- | --- | ---: | ---: | ---: |
| CI1302 | Standard ASR | 69,873 / 382,577 | 146,048 / 458,752 | 1,537,559 |
| CI1302 | CWSL | 155,393 / 157,233 | 231,360 / 233,472 | 2,076,705 |
| CI1303 | Standard ASR | 69,873 / 382,577 | 146,048 / 458,752 | 1,537,559 |
| CI1303 | CWSL | 155,393 / 382,513 | 231,360 / 458,752 | 2,076,705 |
| CI1306 | Standard ASR | 66,379 / 382,577 | 142,560 / 458,752 | 1,533,463 |
| CI1306 | CWSL | 151,947 / 382,513 | 227,920 / 458,752 | 2,072,609 |

The three standard builds embedded the exact 76,128-byte NULL second-core image
and their link maps selected `SDK_ALG_PRO_SRAM_HOST_NULL_END_ADDR`. The three
CWSL builds embedded the exact 76,192-byte CWSL second-core image and selected
`SDK_ALG_PRO_SRAM_HOST_CWSL_END_ADDR`. The largest generated user-code container
was 231,360 bytes.

The CWSL resource set was then replaced with the complete, single-source output
of the V2.7.14 `offline_asr_alg_pro_sample`: ASR 20,038 bytes, DNN 1,410,376
bytes, Voice 379,741 bytes and UserFile 12,321 bytes. The generation script
reproduced all four expected SHA-256 values, and profile preparation copied the
standard and CWSL sets to separate sketch-local paths without cross-profile
reuse. Direct `compose` and strict `inspect` passed for CI1302, CI1303 and
CI1306. All 12 resource payloads were also sliced back out of the three final
CWSL firmware images at their inspected partition offsets; every payload hash
matched its packaged source file byte for byte.

CI1302's 2 MB boundary was checked with padded user-code inputs. Exactly
`0x39000` (233,472) bytes composed and inspected with UserFile ending before
NV data at `0x1FC000`; 233,473 bytes was rejected because 4 KiB alignment would
require `0x3A000`, above the computed `0x39000` layout maximum. The CI1302 CWSL
menu therefore enforces `build.user_code_max=233472` and the conservative host
program limit 157,233. CI1303 and CI1306 CWSL retain the 458,752-byte user-code
limit. This remains build-path validation only; learning capture, template
persistence and learned-word recognition require physical-board testing.

## Cross-host Nuclei GCC validation

On 2026-07-26, the ChipIntelli Linux `riscv-gcc-9.2.0.tar.gz` archive was
downloaded from the vendor API and pinned at SHA-256
`0ee91c983f2cf3eaa26b444eb553847a7dda34f3fb5d97c34b977ca43e593ca5`.
It reported GCC 9.2.0, target `riscv-nuclei-elf`, and the required
`rv32imafc/ilp32f` multilib.

Directly linking the vendor's binary-only archives with a 64-bit Linux process
failed because their GCC LTO bytecode was produced by the validated 32-bit
Windows compiler. All 91 members of the 12 retained vendor archives were
therefore materialized once as ordinary RISC-V ELF relocatable objects. The
generated `tools/sdk/lib-cross-host/BUILD-MANIFEST.txt` pins every input and
output SHA-256, and all output members were checked to contain no `.gnu.lto_*`
sections.

A real CI1306 standard-ASR firmware link then passed with both the pinned
ChipIntelli Linux package and a compatibility probe built from Nuclei's
`v9.2RC` tag. That tag's GCC submodule actually reports 8.3.0, so the probe is
not used or published as a GCC 9.2.0 compiler. The probe compiled the 138
source SDK units plus 16 Arduino/C++ objects and linked the actual linker
script and vendor archives. Loadable `.text` grew from 56,186 bytes with the
original Windows LTO libraries to 62,744 bytes with the cross-host archives.
This validates target link compatibility, not device runtime behavior.

The package builder also passed archive/index generation with Windows, Linux
and macOS compiler host records, plus Windows, Linux, Intel macOS and Apple
Silicon `citool-cli` host records. That earlier packaging-only macOS fixture
reused the Linux archive solely to exercise archive routing and was never a
publishable macOS compiler.

On 2026-07-28, the publishable macOS compiler was built on a physical Apple M4
running macOS 15.7.1 from Nuclei's official `nuclei_9.2_fixjalr_forhw` source.
The build pins top-level commit
`b709d98b514136ab73118998518caa09aa9ddf22`, GCC commit
`b2354399bb7175a7cefb86ed0ba870584ec0324f`, binutils commit
`dbfcea998ba6f592566eda9f9288690d7a060c8f`, and newlib commit
`b8d32e85025f863db1df73ce625c6fddeadb7c17`. The resulting Apple Silicon
archive is pinned at SHA-256
`30e8bb5fcb17066cfc3308774ebb880091de015fa7027ca4c74701d8e4daf4a5`.
It reports GCC 9.2.0 and target `riscv-nuclei-elf`; all 20 multilib lines match
the Windows compiler exactly. After extraction, both C and C++ linked
`rv32imafc/ilp32f` nano-spec RISC-V ELFs under
`env -i PATH=/usr/bin:/bin`. All 50 packaged Mach-O files were arm64, passed
ad-hoc signature verification, and referenced no Homebrew or `/usr/local`
absolute dependency.

## Arduino 1.0.4 Standard-resource and tone failure-path validation

On 2026-07-28, the Standard resource generator stopped extracting the compact
partitions from the SDK TTS reference image. Those files were byte-identical in
V2.7.12 and V2.7.14 but left the current offline-ASR SDK in STARTING state on a
CI1303. Standard and CWSL resources are now independently reproduced from the
V2.7.14 `offline_asr_alg_pro_sample` raw inputs. Both regeneration paths matched
their manifests byte for byte: ASR 20,038 bytes, DNN 1,410,376 bytes, Voice
379,741 bytes and UserFile 12,321 bytes. The package builder now rejects either
profile when its manifest source, profile, size or SHA-256 does not match.

The resource-preparation migration was exercised with three isolated sketches:
a new sketch received the current files and managed manifest; the exact legacy
four-file Standard set was upgraded; and a deliberately modified resource was
preserved while only missing files were filled. The custom/mixed case did not
receive a managed marker and emitted a compatibility warning.

Warning-enabled clean builds of `CI13XXSmoke` under Standard and
`ChipIntelliCWSL/SerialLearning` under CWSL completed SDK compilation, Arduino
compilation, linking, dual-core merge, `compose` and strict `inspect` for
CI1302, CI1303 and CI1306. No compiler diagnostics were emitted. The largest
new Standard image was the CI1302/CI1303 2,056,225-byte firmware; CI1302 stayed
within its 2 MB Flash/NV layout.

The CI1303 on COM31 then passed CRC-verified uploads with both profiles. The
Standard hardware diagnostic reached SDK READY and passed GPIO, PWM endpoints,
finite tone release, Wire/Serial1 resource exclusion, Timer, Ticker, Watchdog
control, EEPROM, Preferences, audio initialization, volume/mute and voice-ID 1
completion. The CWSL example reported `templates=0 remaining=16 max=16`.

Finally, a fault-injection sketch allocated FreeRTOS heap until
`xPortGetFreeHeapSize()` returned zero, forcing `tone()` software-timer
allocation to fail. Servo immediately claimed the same PWM pin and reported
`resource_released=PASS`, verifying that a finite tone now fails closed instead
of running indefinitely. The board was restored to the fixed Standard
diagnostic firmware after testing.

## Apple Silicon full-build validation

On 2026-08-03, the Python build hooks and cross-host libraries were exercised
on an Apple Silicon host running macOS 15.7.1 with Arduino CLI 1.5.1 and the
published arm64 GCC 9.2.0 toolchain. The previous cross-host archives had
materialized each vendor LTO member into monolithic `.text`/`.data` sections;
that made disabled CWSL/TTS callbacks mandatory and broke the final link.

The archives were regenerated with `-ffunction-sections -fdata-sections` and
the platform code-generation flags. All 91 vendor members contain no GCC LTO
sections; every code-bearing member retains named function sections. Clean,
warning-enabled Standard builds of `CI13XXSmoke` passed for CI1302, CI1303 and
CI1306. CWSL builds passed with the size-constrained `BasicLearning` example on
CI1302 and the full `SerialLearning` example on CI1303 and CI1306. Every build
completed SDK compilation, Arduino compilation, final link, dual-core merge,
firmware `compose`, and strict `inspect` without compiler diagnostics.

The first macOS runtime image exposed a post-build defect that compile and
outer-firmware inspection could not detect: the no-Wine fallback concatenated
the Host and BNPU payloads but omitted the vendor's 32-byte two-file directory.
The Host core could print diagnostics, while the BNPU image was not loaded and
startup waited indefinitely in `mailboxboot_sync()`. The Python implementation
now emits the actual little-endian `<H` file count plus packed `<HII>`
file-ID/offset/size records, with vendor-compatible 16-byte `0xff` alignment.
Its output matched 152 retained `ci-tool-kit merge user-file` results byte for
byte, including aligned and unaligned CI1302, CI1303 and CI1306 Host images.
Post-build now rejects any generated or vendor-tool result that differs from
that exact construction.

The corrected installed package was then tested on the same Apple Silicon Mac
and a physical CI1303 connected through a CH343 USB serial adapter. A clean
Standard build used `Clock=internal`, produced a 217,392-byte dual-core
`user_code.bin` and a 2,064,417-byte complete firmware, and flashed successfully
with CRC verification. At 115200 baud the board reported `Playing voice ID 1`,
`Playing voice ID 2`, `Playing voice ID 3`, and
`All three test voices completed`; all three prompts were audible. This covers
macOS-native compilation with the cross-host archives, Python post-build,
complete-image composition, CH343 upload, dual-core startup and audio runtime.
