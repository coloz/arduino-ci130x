#!/bin/sh
set -eu
cd "$(dirname "$0")"
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
g++ -std=c++17 -Wall -Wextra -Werror -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Icooperative_fakes -I../src cooperative_test.cpp ../src/ESPLink.cpp \
  -o "$out/cooperative_test"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$out/cooperative_test"
