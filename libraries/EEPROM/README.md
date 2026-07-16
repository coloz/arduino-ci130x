# EEPROM for CI13XX

This library emulates Arduino EEPROM with one CI13XX NVDM item. `begin()` waits
for the SDK to initialize the firmware partition table, reads the item into
RAM, and `commit()` persists changed bytes through the SDK wear-managed NVDM
layer.

The default `USE_NULL` profile has a 256-byte NVDM I/O buffer and a 16-byte
item header, so the supported EEPROM size is 1 through 240 bytes. The library
reserves NVDM item ID `0x60454550`. Call `commit()` only when persistence is
needed; every commit is a flash update. This API is not thread-safe.

