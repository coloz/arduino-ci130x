# Boards Manager validation

Validation date: 2026-07-16  
Host: Windows x64  
Arduino IDE: 2.3.7  
Arduino CLI: 1.3.1 (bundled with the installed IDE)

The package was tested with a new Arduino data, downloads and user directory.
No source-tree hardware link, `platform.local.txt`, existing Arduino15 core or
preinstalled ChipIntelli tool was available to the test instance.

Validated flow:

1. Serve `package/` on `http://127.0.0.1:8765/`.
2. Download and parse `package_chipintelli_index.json`.
3. Discover `chipintelli:ci13xx@0.1.0`.
4. Download and checksum the 133.32 MiB `chipintelli:riscv-gcc@9.2.0` tool.
5. Download and checksum the 14.43 MiB `chipintelli:ci13xx@0.1.0` platform.
6. Install both packages and resolve `chipintelli:ci13xx:ci_d06gt01d`.
7. Compile, link and run the CI13XX dual-image post-build step for all 14
   installed platform and library examples.

Result: 14 compiled, 14 passed, 0 failed.

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
