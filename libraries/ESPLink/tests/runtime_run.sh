#!/bin/sh
set -eu
cd "$(dirname "$0")/../../.."
repo=$(pwd)
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
variant=${1:-optimized}
case "$variant" in
  optimized) defines='' ;;
  fallback) defines='-DESPLINK_CI13XX_UART_BULK_RX=0 -DESPLINK_CI13XX_TX_DMA=0 -DESPLINK_CI13XX_TASK_NOTIFY=0' ;;
  configured) defines='-DESPLINK_CI13XX_RX_BUFFER_SIZE=2048 -DESPLINK_CI13XX_RX_CHUNK_SIZE=32 -DESPLINK_CI13XX_DMA_THRESHOLD=128 -DESPLINK_CI13XX_TASK_STACK_WORDS=1536 -DESPLINK_CI13XX_TASK_PRIORITY=2' ;;
  drain-disabled) defines='-DESPLINK_CI13XX_TX_DRAIN=0' ;;
  *) printf 'Unknown variant: %s\n' "$variant" >&2; exit 2 ;;
esac
printf 'ESPLink RTOS variant: %s\n' "$variant"
# defines contains only the controlled literal arguments from the case above.
# shellcheck disable=SC2086
g++ -std=c++17 -Wall -Wextra -Werror -g -pthread $defines \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/libraries/ESPLink/tests/runtime_fakes" -I"$repo/libraries/ESPLink/src" \
  "$repo/libraries/ESPLink/tests/runtime_test.cpp" -o "$out/runtime_test"
"$out/runtime_test"
