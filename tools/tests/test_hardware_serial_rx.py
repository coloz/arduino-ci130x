#!/usr/bin/env python3
"""Exercise the production HardwareSerial::readAvailable method on the host.

Only this method is extracted; CI1306 SDK registers are not simulated. Run with
Python 3 and g++ (including under WSL). Target builds verify the complete core.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "cores/chipintelli/HardwareSerial.cpp").read_text(encoding="utf-8")
header = (root / "cores/chipintelli/HardwareSerial.h").read_text(encoding="utf-8")
method = re.search(r"size_t HardwareSerial::readAvailable\(uint8_t \*buffer, size_t size\) \{.*?^\}", source, re.M | re.S)
limit = re.search(r"static constexpr size_t RxReadChunkSize = (\d+)U;", header)
if not method or not limit:
    raise RuntimeError("Production batch read declaration changed; update test harness")

harness = r'''
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <random>
#include <vector>

static uint64_t checks;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); std::abort(); } } while (false)
static unsigned depth, entries, copyBytes;
static std::function<void()> beforeEnter, afterExit, duringCopy;
static void enterCritical() {
    if (beforeEnter) { auto hook = beforeEnter; beforeEnter = {}; hook(); }
    CHECK(depth == 0); ++depth; ++entries; copyBytes = 0;
}
static void exitCritical() {
    CHECK(depth == 1); --depth;
    if (afterExit) { auto hook = afterExit; afterExit = {}; hook(); }
}
static void* checkedMemcpy(void* destination, const void* source, size_t count) {
    CHECK(depth == 1);
    copyBytes += static_cast<unsigned>(count);
    CHECK(copyBytes <= @LIMIT@);
    if (duringCopy) { auto hook = duringCopy; duringCopy = {}; hook(); }
    return std::memcpy(destination, source, count);
}
#define taskENTER_CRITICAL() enterCritical()
#define taskEXIT_CRITICAL() exitCritical()
#define memcpy checkedMemcpy
class HardwareSerial {
public:
    static constexpr size_t RxReadChunkSize = @LIMIT@U;
    bool _started = true;
    uint16_t _rxHead = 0, _rxTail = 0, _rxMask = 0, _rxBufferSize = 0;
    uint8_t* _rxBuffer = nullptr;
    size_t readAvailable(uint8_t*, size_t);
};
@METHOD@
#undef memcpy

static void oneCase(uint16_t capacity, uint16_t tail, uint16_t buffered, size_t requested) {
    std::vector<uint8_t> ring(capacity);
    for (unsigned i = 0; i < capacity; ++i) ring[i] = static_cast<uint8_t>(i * 37U + i / 256U);
    HardwareSerial serial;
    serial._rxBuffer = ring.data(); serial._rxBufferSize = capacity;
    serial._rxMask = static_cast<uint16_t>(capacity - 1U); serial._rxTail = tail;
    serial._rxHead = static_cast<uint16_t>((tail + buffered) & serial._rxMask);
    const auto head = serial._rxHead;
    std::array<uint8_t, 66> output; output.fill(0xA5);
    entries = 0;
    const auto count = serial.readAvailable(output.data() + 1, requested);
    const auto expected = std::min({requested, static_cast<size_t>(buffered), HardwareSerial::RxReadChunkSize});
    CHECK(count == expected);
    CHECK(entries == (requested ? 1U : 0U));
    CHECK(depth == 0);
    CHECK(serial._rxHead == head);
    CHECK(serial._rxTail == ((tail + count) & serial._rxMask));
    CHECK(output.front() == 0xA5);
    for (size_t i = 0; i < count; ++i) CHECK(output[i + 1] == ring[(tail + i) & serial._rxMask]);
    for (size_t i = count + 1; i < output.size(); ++i) CHECK(output[i] == 0xA5);
}
int main() {
    const size_t requests[] = {0, 1, 2, 31, 63, 64, 65, 1024, std::numeric_limits<size_t>::max()};
    // Exhaust every ring position/count for small rings, including wrap and full.
    for (uint16_t capacity = 2; capacity <= 128; capacity *= 2)
        for (uint16_t tail = 0; tail < capacity; ++tail)
            for (uint16_t count = 0; count < capacity; ++count)
                for (size_t request : requests) oneCase(capacity, tail, count, request);
    std::mt19937 random(0x1306);
    for (unsigned i = 0; i < 5000; ++i) {
        const auto capacity = static_cast<uint16_t>(256U << (random() % 8U));
        oneCase(capacity, static_cast<uint16_t>(random() % capacity),
                static_cast<uint16_t>(random() % capacity), requests[random() % 9U]);
    }
    HardwareSerial serial;
    uint8_t output[64]{};
    CHECK(serial.readAvailable(nullptr, sizeof(output)) == 0);
    CHECK(serial.readAvailable(output, 0) == 0);
    // end() winning immediately before the critical section must prevent any
    // dereference of the caller-owned buffer after its lifetime has ended.
    beforeEnter = [&] { serial._started = false; serial._rxBuffer = nullptr; };
    CHECK(serial.readAvailable(output, sizeof(output)) == 0);
    std::array<uint8_t, 128> ring{};
    serial._started = true; serial._rxBuffer = ring.data();
    serial._rxMask = 127; serial._rxBufferSize = 128;
    serial._rxHead = 32; serial._rxTail = 0;
    std::fill(ring.begin(), ring.begin() + 32, 0x51);
    // A producer attempting to run during memcpy is deferred until the lock
    // exits. Its new bytes remain queued and cannot be consumed by this snapshot.
    duringCopy = [&] {
        CHECK(depth == 1);
        afterExit = [&] {
            CHECK(depth == 0);
            std::fill(ring.begin() + 32, ring.begin() + 40, 0x72);
            serial._rxHead = 40;
        };
    };
    CHECK(serial.readAvailable(output, 64) == 32);
    CHECK(serial._rxTail == 32 && serial._rxHead == 40);
    CHECK(std::all_of(output, output + 32, [](uint8_t b) { return b == 0x51; }));
    CHECK(serial.readAvailable(output, 64) == 8);
    CHECK(std::all_of(output, output + 8, [](uint8_t b) { return b == 0x72; }));
    serial._rxTail = 0; serial._rxHead = 32;
    duringCopy = [&] { afterExit = [&] { serial._started = false; serial._rxBuffer = nullptr; }; };
    CHECK(serial.readAvailable(output, 64) == 32);
    CHECK(serial.readAvailable(output, 64) == 0);
    std::printf("HardwareSerial batch RX: %llu checks passed\n", static_cast<unsigned long long>(checks));
}
'''
harness = harness.replace("@LIMIT@", limit[1]).replace("@METHOD@", method[0])
with tempfile.TemporaryDirectory(prefix="ci1306-uart-rx-") as directory:
    directory = Path(directory)
    translation_unit = directory / "hardware_serial_rx_test.cpp"
    binary = directory / "hardware_serial_rx_test"
    translation_unit.write_text(harness, encoding="utf-8")
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer", str(translation_unit), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)