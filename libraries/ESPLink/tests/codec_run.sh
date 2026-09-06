#!/bin/sh
set -eu
cd "$(dirname "$0")"
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
# Explicit overrides cover both implementations with the full wire fault suite.
for mode in 0 1; do
  g++ -std=c++17 -Wall -Wextra -Werror -pedantic -O1 -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -DC3_CRC32C_USE_TABLE="$mode" -DEXPECT_C3_CRC32C_USE_TABLE="$mode" \
    codec_test.cpp -o "$out/codec_$mode"
  "$out/codec_$mode"
done
# Prove defaults and the CI opt-out at compile time without depending on
# Arduino.h or ESPLinkConfig.h inclusion.
g++ -std=c++17 -Wall -Wextra -Werror -fsyntax-only \
  -DEXPECT_C3_CRC32C_USE_TABLE=0 codec_test.cpp
g++ -std=c++17 -Wall -Wextra -Werror -fsyntax-only \
  -DARDUINO_ARCH_CI13XX -DEXPECT_C3_CRC32C_USE_TABLE=1 codec_test.cpp
g++ -std=c++17 -Wall -Wextra -Werror -fsyntax-only \
  -DARDUINO_ARCH_CI13XX -DC3_CRC32C_USE_TABLE=0 \
  -DEXPECT_C3_CRC32C_USE_TABLE=0 codec_test.cpp
if [ "${1:-}" = "--benchmark" ]; then
  g++ --version | head -n 1
  uname -m
  for mode in 0 1; do
    g++ -std=c++17 -Wall -Wextra -Werror -pedantic -O2 \
      -DC3_CRC32C_USE_TABLE="$mode" codec_benchmark.cpp -o "$out/benchmark_$mode"
    "$out/benchmark_$mode"
  done
fi