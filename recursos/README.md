# Profile-specific firmware resources

This directory is shipped in the Arduino platform package. Before compilation,
`tools/prepare_resources.ps1` creates the sketch-local `recursos/` directory and
copies missing `asr.bin`, `dnn.bin`, `voice.bin` and `user_file.bin` files from
the selected algorithm profile. Standard ASR and AEC use the files directly in
this directory; CWSL and CWSL+AEC use the files under `cwsl/`. Package-managed or exact known-
legacy sets can be upgraded as described below; user-owned files are never
overwritten. Switching the Arduino Algorithm menu never reuses the other
profile's model files.

`citool-cli` embeds the validated 8 KiB CI130X FW_V2 Bootloader and generates
the metadata and partition table itself. The installed Arduino platform does
not require or ship a complete `Firmware_V2.0.0.bin` template.

Both resource profiles reproduce the official V2.7.14
`offline_asr_alg_pro_sample` pipeline. The older compact partitions embedded in
the SDK's TTS reference firmware do not bring up the V2.7.14 offline-ASR
host/algorithm pair and must not be used here. Generate Standard ASR with
`tools/generate_package_resources.ps1` and CWSL with
`tools/generate_cwsl_package_resources.ps1`. Each profile is generated into its
own directory with a source manifest and locked SHA-256 values. The current
vendor sample produces identical resource payloads for both profiles; the
Arduino menu still selects different compile flags, linker scripts and
second-core images. AEC is therefore a build-profile choice and does not need a
separate ASR/DNN/Voice/UserFile resource set.

When the primary Arduino source contains `WAKEWORD<n>`, `COMMAND<n>`, `VOICE<n>`
or `VOICEMP3<n>` definitions, the post-build hook copies the selected profile into
the build staging directory and runs `citool-cli generate`. Generated ASR/TTS
files replace that staging copy before firmware composition; sketch-local files
remain untouched. ASR is requested when `WAKEWORD<n>` or `COMMAND<n>` exists,
and TTS is requested only when `VOICE<n>` exists. Multiple `WAKEWORD<n>` macros
define multiple fixed wake words. Without explicit wake-word macros, a single
`COMMAND<n>` remains a valid legacy wake-word-only configuration. The CLI's verified user cache allows identical
requests to be reused across Arduino build-directory cleanups.

When the package first copies a complete profile set into a sketch, it also
writes `.chipintelli-package-resources.json`. A later package can upgrade that
set only while every file still matches the recorded size and hash. Version
1.0.4 additionally recognizes and upgrades the exact four-file Standard set
copied by version 1.0.3 and earlier. A changed, partial or mixed set is treated
as user-owned and is never overwritten.

## Sketch-local user-file entries

A sketch can add or replace individual entries without replacing the default
TTS `user_file.bin`. Put raw payloads in the sketch's resource directory using
the numeric resource ID as the filename prefix:

```text
recursos/
  user_file.bin
  user_file_entries/
    [50000]ir_data_2024_08_16.bin
```

At post-build time, each `[<id>]*.bin` payload is merged into a temporary
`user_file.bin` before the complete firmware is composed. An ID already present
in the base container is replaced; a new ID is inserted in numeric order. The
base file in the sketch is not modified, and `citool-cli compose` recalculates
the final firmware partition metadata and CRC.

IDs must be in the range 0 through 65535. Two overlay files with the same ID,
an empty payload, or a `.bin` filename without a numeric `[id]` prefix stops the
build with an error.

For CWSL, put the base file and optional overlays under `recursos/cwsl/` instead:

```text
recursos/
  cwsl/
    user_file.bin
    user_file_entries/
      [50000]ir_data_2024_08_16.bin
```

The Arduino platform reserves ID `50000` for the optional ChipIntelli IR air-
conditioner database. ID `0` remains the default TTS dictionary. During
`ChipIntelliIR.beginAirConditioner()`, only the calling task temporarily maps
the vendor library's fixed logical ID `0` lookup to physical ID `50000`; other
SDK tasks continue to see the original ID layout.
