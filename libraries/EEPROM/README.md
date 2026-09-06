# EEPROM for CI13XX

This library emulates Arduino EEPROM with one official CI130X SDK NVDM item.
`begin()` starts the SDK if needed, waits up to 10 seconds for NVDM readiness,
and loads the item into RAM. Changed data is persisted by `commit()`.

```cpp
#include <EEPROM.h>

void setup() {
  Serial.begin(115200);
  if (!EEPROM.begin(64)) {
    Serial.println("EEPROM initialization failed");
    return;
  }
  EEPROM.update(0, 42);
  if (!EEPROM.end()) {
    // The instance stays open with its buffered changes. Retry commit()/end().
    Serial.println("EEPROM save failed; data is still buffered");
  }
}

void loop() {}
```

The [PersistentCounter example](examples/PersistentCounter/PersistentCounter.ino)
retries failed commits without incrementing the counter again.

## Capacity and compatibility

- `begin(size)` accepts 1 through 240 bytes; the default is 128. The default
  `USE_NULL` SDK profile has a 256-byte NVDM I/O buffer and a 16-byte item header.
  The same conservative limit is used for all profiles.
- `length()` reports the accessible size; every open instance allocates a
  240-byte RAM buffer. Addresses start at zero. New bytes contain `0xff`.
- The item ID remains `0x60454550`. Existing records from this library are read
  using a full 240-byte read request and padded with `0xff`. A changed commit
  writes the larger of the saved length and the largest range opened in the
  current session, preserving the tail without forcing small records to 240
  bytes. There is no bulk erase or ID migration.
- Repeating `begin(size)` on an open instance only changes its accessible range.
  It preserves pending changes and bytes outside the new range, and does not
  reload or commit. Shrinking, writing, closing, and growing preserves the tail.
  An invalid size leaves the current instance unchanged. Expanding without
  changing any byte does not write Flash; the enlarged range is remembered
  only for the current open session until a changed commit persists it.
- All instances refer to the same item. Use the global `EEPROM` object, or one
  custom instance at a time. Multiple open buffers can overwrite each other's
  changes. Copy construction and assignment are disabled to prevent double free.

## API and failure behavior

| API | Behavior |
| --- | --- |
| `begin(size)` | Returns false on invalid size, allocation failure, SDK startup failure, readiness timeout, or an invalid/missing/unreadable record after initialization. Failure leaves a closed instance closed. A missing item is created with `0xff` by the SDK. |
| `read(address)` | Returns the buffered byte; returns `0xff` before begin or outside the accessible range. |
| `write` / `update` | Change a buffered byte only if its value differs. Invalid addresses do nothing. |
| `readBytes` / `writeBytes` | Return the requested size on success, or zero for a null pointer or invalid range. No partial reads/writes. The caller must provide a buffer large enough for the requested size. |
| `get` / `put` | Copy the object's raw bytes using the same range checks. Store plain data, not objects containing owning pointers such as `String`. Invalid ranges leave the destination unchanged. |
| `commit()` | Returns false before begin. If unchanged, returns true without Flash I/O. Otherwise writes the preserved record length and verifies its length and contents by reading it back. Any failure retains buffered changes for retry. |
| `end()` | Returns true if already closed or if commit and close succeed. On failure returns false and keeps the instance open for retry. Existing calls that ignore its new boolean result still compile. |
| `isBegun()` / `length()` | Report whether a buffer is open and its accessible size; length is zero after a successful close. |

A zero-length byte operation does not modify memory. Writes are buffered: call
`commit()` or check `end()` before resetting or removing power. Destruction only
makes a best-effort commit and always frees the buffer; it cannot report failure.
Unlike the previous implementation, resizing an open instance does not implicitly
save, and a failed `end()` does not discard data.

This is a buffered EEPROM-style API, not the complete AVR EEPROM interface:
`operator[]` and iterators are not provided.

## SDK integration and operating limits

NVDM owns Flash allocation, erase management, and recovery. The library never
calls `cinv_init()` or raw Flash erase/write APIs. `begin()` can create a new
Flash record even if the sketch never calls `commit()`.

The SDK reads only the requested item length but checksums the complete stored
record. EEPROM therefore always requests 240 bytes to avoid false checksum
failures when reopening a larger saved record with a smaller accessible size.
A changed commit also reads back the record because some internal SDK item-status
writes do not propagate failures. This adds one full-record read and a 240-byte
local verification buffer per changed commit; it does not prove arbitrary
power-loss or hardware-fault tolerance.

Call the API from a normally scheduled task, such as `setup()`/`loop()`, with
interrupts enabled. Do not call it from static initialization, interrupts,
critical sections, or while the scheduler is suspended. `begin()`, `commit()`,
and closing an open instance reject interrupt/trap context. This API is not
thread-safe: serialize complete read/modify/commit sequences externally. The
SDK's internal mutex only protects individual NVDM calls.

Unchanged writes and clean commits avoid Flash I/O. A changed commit persists
the entire preserved item (not always 240 bytes), so batch updates and avoid
high-frequency commits. There is no automatic background save. Do not use the reserved item ID through raw
NVDM APIs while the EEPROM buffer is open.

The implementation was checked against the local official
`CI130X_SDK_ALG_V2.7.14` source. The vendor's
[NVDM guide](https://document.chipintelli.com/en/软件开发/SDK/CI130X芯片SDK/CI-SDK-Offline/CI130X_SDK_ASR_Offline_V2.2.0/API参考/存储API/nvdata/)
also describes item initialization and application retries after write errors;
that guide is for a different SDK release, so the bundled source controls the
specific behavior described here. See [AUDIT.md](AUDIT.md) for findings and
validation details.

## Host regression tests

Run `powershell -NoProfile -ExecutionPolicy Bypass -File tools/test_eeprom.ps1`
from the repository root. The script uses an installed host C++ compiler
(g++, clang++, or MSVC) and executes the real EEPROM implementation with
fault-injected NVDM/Arduino calls. Outputs go to `.build/eeprom/` by default.
The tests cover buffer ownership, all supported sizes, legacy records,
resizing, write minimization, startup timeouts, and persistence failures.
They do not emulate physical Flash or prove power-loss recovery.
