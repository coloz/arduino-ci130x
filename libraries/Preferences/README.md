# Preferences

`Preferences` provides an Arduino-compatible typed key-value API backed by the
official CI130X SDK NVDM component. Values are persisted immediately, so no
`commit()` call is needed.

```cpp
#include <Preferences.h>

Preferences preferences;

void setup() {
  Serial.begin(115200);
  if (!preferences.begin("app")) {
    return;
  }

  uint32_t starts = preferences.getUInt("starts", 0) + 1;
  preferences.putUInt("starts", starts);
  preferences.putString("name", "CI130X");
}

void loop() {}
```

## Compatibility and limits

- Namespace and key names are 1 to 15 bytes and are case-sensitive.
- Each namespace can contain up to 16 entries and occupies one NVDM item.
- The complete namespace record is at most 240 bytes. Its 24-byte header and
  each entry's 4-byte header plus key consume part of that capacity.
- Strings include their terminating null byte. Blobs, including `float` and
  `double`, must fit in the remaining namespace capacity.
- Writes are rejected in read-only mode. `begin(name, true)` succeeds only if
  the namespace already exists.
- The optional partition label accepts the portable `"nvs"` name and the
  native `"ci-nvdm"` name. A non-null empty label is rejected, matching the
  Arduino-ESP32 API. CI130X exposes one NVDM partition.
- Access through multiple objects and FreeRTOS tasks is serialized internally.
  Each operation waits for and holds the same mutex while reloading the current
  record, so a complete read-modify-write transaction cannot silently overwrite
  another task's update. Call the API only from task context; it intentionally
  rejects interrupt-context access. Avoid high-frequency writes to reduce flash
  wear.

The library reserves NVDM IDs `0xE0000000` through `0xEFFFFFFF`. Namespace IDs
are derived with FNV-1a and 16 deterministic probe positions. `begin()` scans
all positions for the exact namespace before using the first empty one, so a
deleted collision record cannot hide a later namespace. Every stored namespace
must map back to its physical NVDM ID; duplicate namespace records and duplicate
keys are rejected rather than guessed. Applications using the raw
`cinv_item_*` API should not allocate IDs in this range. This range does not
overlap the fixed volume, CWSL, BLE, IR-state, OTA, or VPR IDs shipped in
official SDK V2.7.14.

NVDM IDs are unrelated to firmware user-file IDs. In particular, keeping the
IR database at user-file ID 0 does not conflict with Preferences storage.

`putString()` returns the character count without the terminating null byte.
`getStringLength()` and the buffer form of `getString()` return the stored
length including that byte, matching Arduino-ESP32 Preferences. Calling
`getBytes()` with a null destination or zero destination length returns the
required blob size. Buffer getters otherwise return `0` without copying when
the destination cannot hold the complete value. Scalar getters return their
supplied default when a key is missing or has a different type. The public
`PreferenceType` values match Arduino-ESP32; the private on-flash type codes
remain compatible with records written by this library's version 1 format.
Version 1 does not add another CRC layer; record integrity ultimately relies on
the SDK NVDM checksum in addition to the structural and ID/name checks above.
`freeEntries()` reports remaining entry slots, not remaining byte capacity.
