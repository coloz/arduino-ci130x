#!/bin/sh
set -eu
cd "$(dirname "$0")/../../.."
repo=$(pwd)
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
ble="$repo/libraries/BLE"
g++ -std=c++17 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -fno-omit-frame-pointer -I"$ble/tests/lifecycle_fakes" -I"$ble/src" \
  -include "$ble/tests/lifecycle_fakes/HostStubs.h" \
  "$ble/tests/lifecycle_test.cpp" "$ble/src/utility/GATT.cpp" \
  "$ble/src/utility/BLEUuid.cpp" \
  "$ble/src/local/BLELocalAttribute.cpp" "$ble/src/local/BLELocalService.cpp" \
  "$ble/src/local/BLELocalCharacteristic.cpp" "$ble/src/local/BLELocalDescriptor.cpp" \
  "$ble/src/BLEService.cpp" "$ble/src/BLECharacteristic.cpp" "$ble/src/BLEDescriptor.cpp" \
  "$ble/src/remote/BLERemoteAttribute.cpp" "$ble/src/remote/BLERemoteService.cpp" \
  "$ble/src/remote/BLERemoteCharacteristic.cpp" "$ble/src/remote/BLERemoteDescriptor.cpp" \
  -o "$out/lifecycle_test"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$out/lifecycle_test"
