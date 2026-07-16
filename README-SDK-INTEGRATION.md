# CI13XX SDK integration

This platform deliberately keeps the validated boot and speech stack from
`CI13XX_SDK_ASR_ALG_V2.7.12`. It archives the official
`offline_asr_alg_pro_sample` objects, then links the Arduino core and sketch on
top. The SDK's startup code, hardware initialization, FreeRTOS scheduler, ASR
tasks and vendor libraries are not reimplemented.

The 138 open SDK objects are rebuilt without `-flto`. This is intentional:
GCC 9.2 resolves calls between LTO units before GNU ld applies `--wrap`, which
would otherwise bypass the Arduino scheduler and ASR-result integration hooks.
The proprietary vendor archives retain their original LTO/ABI payload.

## Supported baseline

- Windows 10/11 x64 host (`ci-tool-kit.exe` is a 64-bit executable)
- CI-D06GT01D board, CI1306, 4 MB flash
- `USE_NULL=1`: the SDK's basic offline-ASR profile
- Nuclei RISC-V GCC 9.2.0 (`riscv-nuclei-elf-*`)
- user-code serial update through the vendor `code_program.exe`

Other CI13XX boards and algorithm profiles need a matching board configuration,
preprocessor definition set, linker script and second-core image. Selecting a
different library alone is unsafe, so this first package does not expose those
combinations.

## Generate the vendored SDK payload

From PowerShell:

```powershell
.\tools\rebuild_sdk.ps1 `
  -SdkPath ..\CI13XX_SDK_ASR_ALG_V2.7.12 `
  -ToolchainBin C:\path\to\riscv-nuclei-elf-gcc-9.2.0\gcc_fix_raissrc\bin
```

The script builds the official sample and generates `tools/sdk/` containing:

- `lib/libci13xx_sdk.a`, made from the sample's 138 compiled objects;
- the exact vendor link libraries used by the profile;
- `ld/ci130x_alg_pro_null.lds` and `ld/common.lds`;
- SDK headers, preserving the original tree and providing a flat compatibility
  include directory;
- the second-core image, `ci-tool-kit.exe` and `code_program.exe`.

Use `-SkipBuild` only after the SDK sample has already built successfully with
the same compiler. Set `CHIPINTELLI_GCC_BIN` instead of passing
`-ToolchainBin` if preferred.

## Image and upload boundary

The sketch ELF is converted to `[0]code.bin`. The profile-specific BNPU image is
copied as `[1]code.bin`, and `ci-tool-kit merge user-file` creates the final
`user_code.bin` container. Arduino's `.bin` output is this container; it is not
a raw image for flash address zero.

Upload updates only the `user_code` partition. The upstream packer sizes that
partition from the merged container (the sample is about `0x26000`), which is
too small for this Arduino platform. This development-preview policy reserves
448 KiB (`0x70000`), taking the value from V2.7.12's `USERCODE_MAX_SIZE`, and
post-build rejects larger containers. The vendor packer does not enforce that
constant, so the size remains a baseline choice pending GUI and board validation,
not a confirmed silicon/tool maximum.

After the upstream ASR, DNN, voice and user-file partition merge has produced
its four `.bin` files, generate a hash/reservation manifest and optionally open
the official GUI:

```powershell
.\tools\prepare_provisioning.ps1 `
  -FirmwareDirectory ..\CI13XX_SDK_ASR_ALG_V2.7.12\projects\offline_asr_alg_pro_sample\firmware `
  -UserCode C:\path\to\CI13XXSmoke.ino.bin `
  -OutputDirectory ..\ci13xx-provisioning `
  -LaunchTool
```

In `PACK_UPDATE_TOOL.exe`, select CI130X series, CI1306, FW_V2, 4 MB, Code2
disabled, every manifest metadata value (including IDs, versions and 16 KiB NV
data), the five manifest inputs, and a fixed User reservation of `0x70000`.
Enter every file partition's manifest `reservedSize`; if the GUI reports an
address conflict, adjust the non-User reservations and re-check that all inputs fit.
Package and install that full image once from the GUI firmware-upgrade page.
The preparation script does not package or flash. This is intentional: the
V2.7.12 command-line `ci-tool-kit make-firmware` rejects both CI1306 and CI130X,
and mapping the board to CI1303 without vendor confirmation would be unsafe.
Only a board provisioned with the fixed layout can subsequently use Arduino
user-code upload. A public release must publish the exact tested baseline image,
hash and first-flash procedure; none is claimed hardware-tested here.
Keep the manifest and full image outside the platform directory so release ZIPs
cannot accidentally absorb a licensed or stale provisioning artifact.

The linker places code, constants, data, BSS, stacks and the 100 KiB FreeRTOS
heap in one 532480-byte host SRAM region. Arduino CLI's program and dynamic-RAM
figures overlap at `.data`; they are not independent capacities. The linker is
the authoritative combined SRAM overflow check.

## Redistribution

The SDK README restricts use and modification without permission and the SDK
does not include a top-level open-source license. Before publishing a Boards
Manager archive, obtain ChipIntelli permission to redistribute the compiled SDK,
proprietary libraries, firmware image and Windows tools, and include all
required third-party notices. The package-index file is therefore a publishing
template, not a live download index.

The LGPL 2.1 text for inherited Arduino compatibility sources is included as
`LICENSE-ARDUINO-CORE.md`; see `THIRD_PARTY_NOTICES.md` for the remaining
release-status caveats.
