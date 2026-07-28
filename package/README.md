# Boards Manager package

## Published Arduino IDE installation

Add the following stable index URL to **Arduino IDE > File > Preferences >
Additional boards manager URLs**, then install **ChipIntelli CI130X Arduino**
from Boards Manager:

```text
https://raw.githubusercontent.com/coloz/arduino-ci130x/main/package/package_chipintelli_index.json
```

The version-pinned `v1.0.4` index is also published as a GitHub Release asset:

```text
https://github.com/coloz/arduino-ci130x/releases/download/v1.0.4/package_chipintelli_index.json
```

The release pipeline mirrors `citool-cli` for Windows x64, macOS Universal
(Intel and Apple Silicon), and Linux x86_64. It also publishes Nuclei GCC 9.2.0
for Windows x64, Linux x86_64, and macOS x86_64. Arduino's supported host
fallback lets Apple Silicon select the Intel macOS compiler archive.

`.github/workflows/deploy-release.yml` downloads the three immutable archives
from `coloz/citool-cli@v1.1.1`, verifies their publisher-provided SHA-256 files,
downloads the three compiler archives from the dedicated toolchain release,
and republishes all six as `arduino-ci130x` Release assets. It generates a
Boards Manager index whose host records, sizes and hashes point at that exact
release. If `citool-cli` is
private, configure the Arduino repository Actions secret `CITOOL_CLI_TOKEN`
with read access to its Releases; public repositories fall back to the workflow
token. `.github/workflows/toolchain-release.yml` validates the existing Windows
compiler, downloads the SHA-pinned ChipIntelli Linux compiler, and builds the
macOS compiler from the official Nuclei `v9.2RC` source. Every job verifies the
target, version, required `rv32imafc/ilp32f` multilib and a linked ELF before an
optional `nuclei-gcc-v9.2.0-host1` release is published.

The compiler/linker side is now packaged for all three desktop OS families.
The SDK pre-build PowerShell scripts and proprietary `ci-tool-kit.exe`
post-processing are still Windows-only, so this does not yet claim a complete
Arduino firmware build on Linux or macOS.

## Local Arduino IDE installation

The generated package is a self-contained local Boards Manager repository.
Build or download the three `citool-cli` archives and download the Linux/macOS
Nuclei GCC archives from `nuclei-gcc-v9.2.0-host1`, then build it on Windows
with the validated Windows toolchain root:

```powershell
..\citool-cli\package\build_release.ps1

.\package\build_package.ps1 `
  -ToolchainRoot C:\path\to\riscv-nuclei-elf-gcc-9.2.0 `
  -ToolchainArchives @(
    'C:\downloads\riscv-nuclei-elf-gcc-9.2.0-linux-x86_64.tar.gz',
    'C:\downloads\riscv-nuclei-elf-gcc-9.2.0-macos-x86_64.tar.gz'
  ) `
  -RequireAllHostTools
```

The script validates the official GCC 9.2.0 executable against the SDK build
manifest, consumes these prebuilt sibling artifacts:

```text
..\citool-cli\dist\citool-cli-1.1.1-windows-x86_64.zip
..\citool-cli\dist\citool-cli-1.1.1-macos-universal.tar.gz
..\citool-cli\dist\citool-cli-1.1.1-linux-x86_64.tar.gz
C:\downloads\riscv-nuclei-elf-gcc-9.2.0-linux-x86_64.tar.gz
C:\downloads\riscv-nuclei-elf-gcc-9.2.0-macos-x86_64.tar.gz
```

It creates the platform, toolchain and uploader archives under `package/dist/`, and writes
`package/package_chipintelli_index.json` with their exact sizes and SHA-256
checksums. Pass all three paths with `-CitoolCliArchives` to consume release
artifacts from another location. `-CitoolCliArchive` remains accepted as a
single-path compatibility parameter, but a publishable package requires all
three host archives. Pass the Linux and macOS GCC paths with
`-ToolchainArchives`; `-RequireAllHostTools` rejects a publishable build when
either one is missing. The Arduino source tree never compiles `citool-cli`
source.

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

Upload the platform ZIP, three compiler archives, three immutable uploader
archives and index to an HTTPS host, then regenerate the index with the public
base URL:

```powershell
.\package\build_package.ps1 `
  -ToolchainRoot C:\path\to\riscv-nuclei-elf-gcc-9.2.0 `
  -ToolchainArchives $downloadedHostToolchains `
  -RequireAllHostTools `
  -BaseUrl https://downloads.example.com/chipintelli-arduino
```

For GitHub Release assets, which are stored directly under the tag URL rather
than a `dist/` subdirectory, add `-FlatAssetUrls`:

```powershell
.\package\build_package.ps1 `
  -ToolchainRoot C:\path\to\riscv-nuclei-elf-gcc-9.2.0 `
  -ToolchainArchives $downloadedHostToolchains `
  -RequireAllHostTools `
  -Version 1.0.4 `
  -BaseUrl https://github.com/OWNER/arduino-ci130x/releases/download/v1.0.4 `
  -FlatAssetUrls
```

Upload the generated platform, three toolchain and three `citool-cli` archives
together with `package_chipintelli_index.json` to the same public Arduino
GitHub Release.
The package builder always points the uploader dependency at that Arduino
release; do not reference the private `citool-cli` repository from a public
Boards Manager index.

The official compiler source is documented at
<https://document.chipintelli.com/en/%E8%BD%AF%E4%BB%B6%E5%BC%80%E5%8F%91/SDK/CI130X%E8%8A%AF%E7%89%87SDK/CI-SDK-Offline/CI130X_SDK_ASR_Offline_V2.2.0/%E8%B5%84%E6%BA%90/gcc/>.
Confirm redistribution permission for the SDK, vendor libraries, tools and
compiler before public hosting.

## Publishing template

`package_chipintelli_index.template.json` is intentionally not a live package
index. Replace every `__...__` value after release artifacts are hosted and
redistribution permission has been confirmed.

The platform archive must have exactly one top-level directory, for example
`arduino-ci130x-1.0.4/`. Put the contents of `arduino-ci130x`
directly inside that directory (including `boards.txt`, `platform.txt`,
`cores/` and the generated `tools/sdk/`); do not add another architecture
directory. Arduino's package manager ignores files placed directly at the ZIP
root and does not extract RAR archives.

Each `citool-cli` archive must contain one top-level `citool-cli/` directory.
The Windows ZIP contains `citool-cli.exe`; the macOS/Linux tar.gz archives
contain executable `citool-cli`. The macOS archive is universal and is mapped
to both `x86_64-apple-darwin` and `arm64-apple-darwin`; Linux maps to
`x86_64-pc-linux-gnu`. The platform declares `citool-cli@1.1.1`
as a tool dependency. Each build composes the generated `user_code.bin` and the
sketch's four resource partitions into a complete firmware image; normal Arduino
upload then uses `citool-cli flash` to write that verified image from Flash
address 0.

After permission has been obtained, the Windows ZIP must contain the single
top-level directory `gcc_fix_raissrc/`, with the compiler at
`gcc_fix_raissrc/bin/riscv-nuclei-elf-gcc.exe`. The Linux and macOS tarballs
contain `riscv-gcc/bin/riscv-nuclei-elf-gcc`. Linux is the ChipIntelli archive
published as `riscv-gcc-9.2.0.tar.gz` (pinned SHA-256
`0ee91c983f2cf3eaa26b444eb553847a7dda34f3fb5d97c34b977ca43e593ca5`).
macOS x86_64 is built from the official Nuclei `v9.2RC` source because neither
ChipIntelli nor Nuclei publishes a matching macOS binary.

The binary-only SDK archives contain pure GCC LTO generated by a 32-bit Windows
host compiler. GCC 9.2.0 Linux/macOS processes cannot deserialize that stream,
even when the target and multilib match. `tools/sdk/lib-cross-host` therefore
contains the same archive members materialized as ordinary RISC-V ELF objects.
`platform.txt` selects those archives only on Linux/macOS; Windows retains the
original LTO libraries. Regenerate and hash-check them on Windows with:

```powershell
.\tools\materialize_vendor_libraries.ps1 `
  -ToolchainRoot C:\path\to\riscv-nuclei-elf-gcc-9.2.0 `
  -Force
```

Materialization preserves archive-member selection and link compatibility, but
removes cross-module optimization inside the vendor binaries. In the CI1306
link probe, loadable `.text` increased from 56,186 to 62,744 bytes; release
validation must continue checking each board's Flash/SRAM limits.

Before publishing:

1. obtain permission to redistribute the SDK binaries and vendor tools;
2. generate `tools/sdk` with `tools/rebuild_sdk.ps1`; this packages the 138
   source-available SDK translation units and retains archives only for
   components whose source is not supplied;
3. regenerate `tools/sdk/lib-cross-host` and verify its manifest;
4. run `toolchain-release.yml`, including its macOS source build and smoke link;
5. compile and package the included Arduino examples;
6. publish the independently tested three-platform `citool-cli` release and provide its archives to the package build;
7. host immutable HTTPS release artifacts;
8. replace the placeholders, save the publishable copy as
   `package_chipintelli_index.json` (the `_index.json` suffix is required), and
   validate host selection with Arduino CLI on Windows x64, Linux x86_64, and
   macOS x86_64/Apple Silicon fallback.

The uploader embeds the validated CI130X FW_V2 Bootloader; the platform archive
contains independently generated Standard and CWSL copies of the official
V2.7.14 `offline_asr_alg_pro_sample` resources under `recursos/` and
`recursos/cwsl/`. Arduino
retains the vendor-confirmed 448 KiB (`0x70000`) SRAM upper limit for the final
`user_code.bin`. CI1302 with the official CWSL resource set has a tighter
2 MB Flash-layout limit of `0x39000` (233,472 bytes), while CI1303 and CI1306
CWSL retain `0x70000`. Board-menu properties pass the applicable limit to
post-build processing, which rejects a larger container before composition.
Within that limit, `citool-cli compose` lays out User, ASR, DNN, Voice and UserFile from
their final bin sizes, rounding each up to 4 KiB, and rejects any layout that
would cross the NV/Flash boundary. It generates the V2 metadata and partition
table for the selected 2 MB or 4 MB Flash and uploads the resulting complete
image. Before release, validate
this exact Bootloader/resource set and full-flash flow on CI1302, CI1303 and
CI1306 hardware. Redistribution permission is still required for every vendor
binary included in the platform archive.
