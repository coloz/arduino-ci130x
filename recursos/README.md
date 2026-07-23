# Default firmware resources

This directory is shipped in the Arduino platform package. Before compilation,
`tools/prepare_resources.ps1` creates the sketch-local `recursos/` directory and
copies only missing `asr.bin`, `dnn.bin`, `voice.bin` and `user_file.bin` files.
Sketch-local files are never overwritten.

`citool-cli` embeds the validated 8 KiB CI130X FW_V2 Bootloader and generates
the metadata and partition table itself. The installed Arduino platform does
not require or ship a complete `Firmware_V2.0.0.bin` template.

The four default partition files are reproducibly extracted from the SDK's TTS reference
firmware by running `tools/generate_package_resources.ps1`. The script verifies
the source partition-table checksum and every extracted partition CRC16.

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

The Arduino platform reserves ID `50000` for the optional ChipIntelli IR air-
conditioner database. ID `0` remains the default TTS dictionary. During
`ChipIntelliIR.beginAirConditioner()`, only the calling task temporarily maps
the vendor library's fixed logical ID `0` lookup to physical ID `50000`; other
SDK tasks continue to see the original ID layout.
