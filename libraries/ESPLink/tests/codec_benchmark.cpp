// SPDX-License-Identifier: MIT
// Native CPU microbenchmark, not CI1306 hardware or end-to-end UART throughput.
#include "../src/C3Protocol.h"
#include <algorithm>
#include <chrono>
#include <cstdio>

#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif
static NOINLINE uint32_t calculate(const uint8_t* data, size_t size) {
  return c3::crc32c(data, size);
}
static volatile uint32_t sink;
int main() {
  uint8_t data[c3::HeaderSize + c3::MaxPayload];
  uint32_t random = 0x1306c3U;
  for (size_t i = 0; i < sizeof(data); ++i) {
    random ^= random << 13; random ^= random >> 17; random ^= random << 5;
    data[i] = uint8_t(random);
  }
  std::printf("CRC32C mode=%s; native CPU; median of 7 rounds\n",
              C3_CRC32C_USE_TABLE ? "table" : "bitwise");
  const size_t lengths[] = {32, 256, 1024, 1044};
  for (size_t length : lengths) {
    const size_t iterations = (32U * 1024U * 1024U) / length;
    double ns[7];
    uint32_t checksum = 2166136261U;
    for (unsigned round = 0; round < 7; ++round) {
      const auto start = std::chrono::steady_clock::now();
      for (size_t i = 0; i < iterations; ++i) {
        // Change each input before calling a non-inlined function, preventing
        // constant folding or lifting identical CRC calls out of the loop.
        data[0] = uint8_t(i);
        checksum = (checksum ^ calculate(data, length)) * 16777619U;
      }
      const auto stop = std::chrono::steady_clock::now();
      ns[round] = std::chrono::duration<double, std::nano>(stop - start).count() / iterations;
      sink = checksum;
    }
    std::sort(ns, ns + 7);
    std::printf("bytes=%4zu iterations=%zu median_ns=%.2f MiB/s=%.2f checksum=%08x\n",
                length, iterations, ns[3], length * 1e9 / ns[3] / (1024.0 * 1024.0), checksum);
  }
  return 0;
}