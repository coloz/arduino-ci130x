# Boards Manager package

## Local Arduino IDE installation

The generated package is a self-contained local Boards Manager repository for
Windows x64. Build it from the platform root:

```powershell
.\package\build_package.ps1 `
  -ToolchainRoot C:\path\to\riscv-nuclei-elf-gcc-9.2.0
```

The script validates the official GCC 9.2.0 executable against the SDK build
manifest, creates the platform and toolchain ZIP files under `package/dist/`,
and writes `package/package_chipintelli_index.json` with their exact sizes and
SHA-256 checksums.

Start the local repository:

```powershell
.\package\serve_package.ps1
```

Add this URL to **Arduino IDE > File > Preferences > Additional boards manager
URLs**, then install **ChipIntelli CI13XX Arduino** from Boards Manager:

```text
http://127.0.0.1:8765/package_chipintelli_index.json
```

The server must remain running during installation. The installed core and
compiler continue to work after the server is stopped.

## HTTPS publishing

Upload the two immutable ZIP files and index to an HTTPS host, then regenerate
the index with the public base URL:

```powershell
.\package\build_package.ps1 `
  -ToolchainRoot C:\path\to\riscv-nuclei-elf-gcc-9.2.0 `
  -BaseUrl https://downloads.example.com/chipintelli-arduino
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
`arduino-chipintelli-0.1.0/`. Put the contents of `arduino-chipintelli`
directly inside that directory (including `boards.txt`, `platform.txt`,
`cores/` and the generated `tools/sdk/`); do not add another architecture
directory. Arduino's package manager ignores files placed directly at the ZIP
root and does not extract RAR archives.

After permission has been obtained, repack the official compiler as ZIP
without changing its contents. The ZIP must contain the single top-level
directory `gcc_fix_raissrc/`, with the compiler at
`gcc_fix_raissrc/bin/riscv-nuclei-elf-gcc.exe`.

Before publishing:

1. obtain permission to redistribute the SDK binaries and vendor tools;
2. generate `tools/sdk` with `tools/rebuild_sdk.ps1`;
3. compile and package the included Arduino examples;
4. compute archive sizes and SHA-256 checksums;
5. host immutable HTTPS release artifacts;
6. replace the placeholders, save the publishable copy as
   `package_chipintelli_index.json` (the `_index.json` suffix is required), and
   validate it with Arduino CLI on Windows x64.

The release must also publish an exact, hardware-validated CI-D06GT01D full
firmware baseline. The preview policy proposes a 448 KiB (`0x70000`) user-code
partition; confirm it on hardware, then publish the selected layout, its SHA-256,
and the one-time `PACK_UPDATE_TOOL.exe` provisioning instructions. The
platform's normal upload only replaces that partition; it cannot repair a
smaller factory layout. Do not publish the unverified CLI `make-firmware` path:
the V2.7.12 executable rejects CI1306/CI130X.
Keep provisioning manifests and full firmware artifacts outside the platform
archive; publish them separately only after hardware validation and permission.
