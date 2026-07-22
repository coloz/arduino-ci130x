# Boards Manager package

## Published Arduino IDE installation

Add the following stable index URL to **Arduino IDE > File > Preferences >
Additional boards manager URLs**, then install **ChipIntelli CI130X Arduino**
from Boards Manager:

```text
https://raw.githubusercontent.com/coloz/arduino-ci130x/main/package/package_chipintelli_index.json
```

The version-pinned `v1.0.1` index is also published as a GitHub Release asset:

```text
https://github.com/coloz/arduino-ci130x/releases/download/v1.0.1/package_chipintelli_index.json
```

The current compiler and `citool-cli` uploader packages support Windows x64 only.

## Local Arduino IDE installation

The generated package is a self-contained local Boards Manager repository for
Windows x64. First build the independently located `citool-cli` sibling project,
then build the Arduino package from the platform root:

```powershell
..\citool-cli\package\build_release.ps1

.\package\build_package.ps1 `
  -ToolchainRoot C:\path\to\riscv-nuclei-elf-gcc-9.2.0
```

The script validates the official GCC 9.2.0 executable against the SDK build
manifest, consumes the prebuilt sibling
`..\citool-cli\dist\citool-cli-1.0.1-windows-x86_64.zip`, creates the platform,
toolchain and uploader ZIP files under `package/dist/`, and writes
`package/package_chipintelli_index.json` with their exact sizes and SHA-256
checksums. Pass `-CitoolCliArchive` to consume a release ZIP from another
location. The Arduino source tree never compiles or packages `citool-cli`
source code.

Start the local repository:

```powershell
.\package\serve_package.ps1
```

Add this URL to **Arduino IDE > File > Preferences > Additional boards manager
URLs**, then install **ChipIntelli CI130X Arduino** from Boards Manager:

```text
http://127.0.0.1:8765/package_chipintelli_index.json
```

The server must remain running during installation. The installed core and
compiler continue to work after the server is stopped.

## HTTPS publishing

Upload the three immutable ZIP files and index to an HTTPS host, then regenerate
the index with the public base URL:

```powershell
.\package\build_package.ps1 `
  -ToolchainRoot C:\path\to\riscv-nuclei-elf-gcc-9.2.0 `
  -BaseUrl https://downloads.example.com/chipintelli-arduino
```

For GitHub Release assets, which are stored directly under the tag URL rather
than a `dist/` subdirectory, add `-FlatAssetUrls`:

```powershell
.\package\build_package.ps1 `
  -ToolchainRoot C:\path\to\riscv-nuclei-elf-gcc-9.2.0 `
  -Version 1.0.1 `
  -BaseUrl https://github.com/OWNER/arduino-ci130x/releases/download/v1.0.1 `
  -CitoolCliBaseUrl https://github.com/OWNER/citool-cli/releases/download/v1.0.1 `
  -FlatAssetUrls
```

The official compiler source is documented at
<https://document.chipintelli.com/en/%E8%BD%AF%E4%BB%B6%E5%BC%80%E5%8F%91/SDK/CI130X%E8%8A%AF%E7%89%87SDK/CI-SDK-Offline/CI130X_SDK_ASR_Offline_V2.2.0/%E8%B5%84%E6%BA%90/gcc/>.
Confirm redistribution permission for the SDK, vendor libraries, tools and
compiler before public hosting.

## Publishing template

`package_chipintelli_index.template.json` is intentionally not a live package
index. Replace every `__...__` value after release artifacts are hosted and
redistribution permission has been confirmed.

The platform archive must have exactly one top-level directory, for example
`arduino-ci130x-1.0.1/`. Put the contents of `arduino-ci130x`
directly inside that directory (including `boards.txt`, `platform.txt`,
`cores/` and the generated `tools/sdk/`); do not add another architecture
directory. Arduino's package manager ignores files placed directly at the ZIP
root and does not extract RAR archives.

The `citool-cli` archive must contain one top-level `citool-cli/` directory with
`citool-cli.exe` directly inside it. The platform declares `citool-cli@1.0.1`
as a tool dependency. Each build composes the generated `user_code.bin` and the
sketch's four resource partitions into a complete firmware image; normal Arduino
upload then uses `citool-cli flash` to write that verified image from Flash
address 0.

After permission has been obtained, repack the official compiler as ZIP
without changing its contents. The ZIP must contain the single top-level
directory `gcc_fix_raissrc/`, with the compiler at
`gcc_fix_raissrc/bin/riscv-nuclei-elf-gcc.exe`.

Before publishing:

1. obtain permission to redistribute the SDK binaries and vendor tools;
2. generate `tools/sdk` with `tools/rebuild_sdk.ps1`; this packages the 138
   source-available SDK translation units and retains archives only for
   components whose source is not supplied;
3. compile and package the included Arduino examples;
4. publish the independently tested `citool-cli` release and provide its ZIP to the package build;
5. host immutable HTTPS release artifacts;
6. replace the placeholders, save the publishable copy as
   `package_chipintelli_index.json` (the `_index.json` suffix is required), and
   validate it with Arduino CLI on Windows x64.

The uploader embeds the validated CI130X FW_V2 Bootloader; the platform archive
contains only the four default resource partitions under `recursos/`. Arduino
does not impose a fixed `0x70000` user-code-container limit. The linker shares
the SRAM remaining after the vendor 3 KiB stack and 100 KiB FreeRTOS heap
between the actual static image, BSS/no-init and a selectable 16/32/64 KiB
minimum C/newlib heap. Post-build processing verifies that layout from ELF
symbols. `citool-cli compose` then lays out User, ASR, DNN, Voice and UserFile
from their final bin sizes, rounding each up to 4 KiB, and rejects a User image
only when it exceeds the remaining Flash layout limit. It generates the V2
metadata and partition table for the selected 2 MB or 4 MB Flash and uploads
the resulting complete image. Before release, validate
this exact Bootloader/resource set and full-flash flow on CI1302, CI1303 and
CI1306 hardware. Redistribution permission is still required for every vendor
binary included in the platform archive. This change applies to the complete
firmware flash flow; the vendor SDK's legacy OTA header still declares a
448 KiB User limit and must be validated separately before that OTA path is
enabled for larger containers.
