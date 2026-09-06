// SPDX-License-Identifier: MIT
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <functional>
#include <atomic>
#include <deque>
#include <mutex>
#include <condition_variable>
unsigned long millis();
unsigned long micros();
void yield();
enum class HardwareSerialStartError { None, Busy, Timeout, ResourceBusy };
constexpr uint32_t SERIAL_8N1 = 0;
class Stream {
public:
  virtual ~Stream() = default;
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t*, size_t) = 0;
};
class HardwareSerial : public Stream {
public:
  std::function<void(const uint8_t*, size_t)> onWrite;
  std::atomic<uint32_t> stallWaitMs{0};
  std::atomic<bool> stallEntered{false}, stallExited{false};
  std::atomic<bool> dmaRequestFails{false}, drainNotificationAfterWrite{false};
  std::atomic<unsigned> drainedWriteNotifications{0};
  std::atomic<unsigned> bulkReads{0}, byteReads{0}, bulkBytes{0}, maxBulkBytes{0};
  std::atomic<unsigned> dmaEnableCalls{0}, dmaWrites{0}, irqWrites{0};
  std::atomic<unsigned> waits{0}, waiting{0}, shortWaits{0}, maxWaitMs{0}, rxNotifies{0};
  std::atomic<size_t> configuredRxBufferSize{0}, dmaThreshold{0};
  void begin(uint32_t, uint32_t = SERIAL_8N1) { active = true; error = HardwareSerialStartError::None; }
  void end() { active = false; dmaEnabled = false; }
  explicit operator bool() const { return active; }
  bool setRxBuffer(uint8_t* buffer, size_t size) {
    if (active) return false;
    configuredRxBufferSize = buffer ? size : 0;
    return true;
  }
  HardwareSerialStartError lastError() const { return error; }
  bool enableTxDMA(bool enabled = true, size_t threshold = 64) {
    if (!enabled) { dmaEnabled = false; return true; }
    ++dmaEnableCalls;
    if (dmaRequestFails) { error = HardwareSerialStartError::ResourceBusy; return false; }
    dmaEnabled = true; dmaThreshold = threshold; return true;
  }
  bool txDMAEnabled() const { return dmaEnabled; }
  int available() override;
  int read() override;
  int peek() override;
  size_t readAvailable(uint8_t*, size_t);
  size_t write(uint8_t byte) override { return write(&byte, 1, 1000); }
  size_t write(const uint8_t* data, size_t size) override { return write(data, size, 1000); }
  size_t write(const uint8_t*, size_t, uint32_t);
  bool waitReadable(uint32_t);
  void inject(const uint8_t*, size_t);
  bool injectWhenWaiting(const uint8_t*, size_t, uint32_t timeoutMs);
private:
  std::atomic<bool> active{false}, dmaEnabled{false};
  std::atomic<HardwareSerialStartError> error{HardwareSerialStartError::None};
  std::mutex mutex;
  void* rxWaiter = nullptr;
  std::deque<uint8_t> rx;
};
