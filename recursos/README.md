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
