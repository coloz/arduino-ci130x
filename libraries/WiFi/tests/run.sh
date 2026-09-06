#!/bin/sh
set -eu
cd "$(dirname "$0")/../../.."
repo=$(pwd)
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
for variant in ZONED_IPV6 IPV4_ONLY IPV6_NO_ZONE; do
  g++ -std=c++17 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -D"WIFI_TEST_$variant" \
    -I"$repo/libraries/WiFi/tests/fakes" -I"$repo/libraries/WiFi/src" -I"$repo/libraries/ESPLink/src" \
    "$repo/libraries/WiFi/tests/network_semantics.cpp" "$repo"/libraries/WiFi/src/*.cpp -o "$out/network_semantics"
  printf '%s: ' "$variant"
  "$out/network_semantics"
done
