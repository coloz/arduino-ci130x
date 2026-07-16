# Boards Manager validation

Validation date: 2026-07-16  
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
3. Discover `chipintelli:ci13xx@0.0.1`.
4. Download and checksum the 133.32 MiB `chipintelli:riscv-gcc@9.2.0` tool.
5. Download and checksum the 18.88 MiB `chipintelli:ci13xx@0.0.1` platform.
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

## CI1302 and CI1303 variant validation

The installed platform was resolved as `chipintelli:ci13xx:ci1302` and
`chipintelli:ci13xx:ci1303` with Arduino CLI 1.3.1. The installed
`CI13XXSmoke` example compiled, linked and completed the dual-image post-build
step for both FQBNs.

The link maps resolve `libci13xx_sdk.a` from `tools/sdk/lib/ci1302/` and
`tools/sdk/lib/ci1303/`, respectively. The rebuilt Boards Manager platform ZIP
contains both variant directories and both SDK archives under a single top-level
directory; the generated index parses as BOM-free JSON and lists all three
boards. This validation used a fresh package-manager install; it does not claim
physical-board runtime validation for CI1302/CI1303.
