// SPDX-License-Identifier: MIT
// Runs the real ESPLink worker and transaction code with serial and RTOS fakes.
#define ESPLINK_CI13XX_RTOS 1
#define ARDUINO_ARCH_CI13XX 1
#include <Arduino.h>
HardwareSerial Serial2;
#include <C3Protocol.h>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>
#include <cstring>
#include <cstdlib>
extern "C" {
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
}
#include "../src/ESPLink.cpp"

static const auto epoch = std::chrono::steady_clock::now();
unsigned long millis() { return (unsigned long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - epoch).count(); }
unsigned long micros() { return (unsigned long)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - epoch).count(); }
void yield() { std::this_thread::yield(); }
static std::atomic<unsigned> checks{0}, deadSemaphoreUses{0}, deadTaskNotifyUses{0};
static std::atomic<unsigned> taskNotifications{0}, notificationWakeups{0}, notificationTimeouts{0};
static std::atomic<bool> failNextTask{false};
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << ": " #x "\n"; std::abort(); } } while (0)

struct FakeSemaphore {
  std::mutex mutex; std::condition_variable cv;
  bool token = false, deleted = false;
};
struct FakeTask {
  std::thread thread; std::mutex mutex; std::condition_variable cv;
  uint32_t notifications = 0; bool finished = false;
};
static std::mutex allocationMutex;
static std::vector<std::unique_ptr<FakeSemaphore>> semaphores;
static std::vector<std::unique_ptr<FakeTask>> tasks;
static thread_local TaskHandle_t thisTask = nullptr;
static void joinTasks() { for (auto& t : tasks) if (t->thread.joinable()) t->thread.join(); }
static SemaphoreHandle_t makeSemaphore(bool token) {
  auto s = std::make_unique<FakeSemaphore>(); s->token = token;
  auto raw = s.get(); std::lock_guard<std::mutex> lock(allocationMutex);
  semaphores.push_back(std::move(s)); return raw;
}
extern "C" {
BaseType_t xTaskGetSchedulerState(void) { return taskSCHEDULER_RUNNING; }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return thisTask ? thisTask : reinterpret_cast<TaskHandle_t>(uintptr_t(1)); }
BaseType_t xTaskCreate(TaskFunction_t fn, const char*, uint16_t depth, void* arg,
                       UBaseType_t priority, TaskHandle_t* handle) {
  CHECK(depth == ESPLINK_CI13XX_TASK_STACK_WORDS);
  CHECK(priority == ESPLINK_CI13XX_TASK_PRIORITY);
  if (failNextTask.exchange(false)) return pdFALSE;
  auto t = std::make_unique<FakeTask>(); auto raw = t.get(); *handle = raw;
  t->thread = std::thread([raw, fn, arg] {
    thisTask = raw; fn(arg);
    { std::lock_guard<std::mutex> lock(raw->mutex); raw->finished = true; }
    thisTask = nullptr;
  });
  std::lock_guard<std::mutex> lock(allocationMutex);
  tasks.push_back(std::move(t)); return pdPASS;
}
BaseType_t xTaskNotifyGive(TaskHandle_t handle) {
  auto task = static_cast<FakeTask*>(handle); CHECK(task != nullptr);
  {
    std::lock_guard<std::mutex> lock(task->mutex);
    if (task->finished) { ++deadTaskNotifyUses; return pdFALSE; }
    ++task->notifications; ++taskNotifications;
  }
  task->cv.notify_one(); return pdPASS;
}
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t timeout) {
  auto task = static_cast<FakeTask*>(thisTask); CHECK(task != nullptr);
  std::unique_lock<std::mutex> lock(task->mutex);
  const auto ready = [task] { return task->notifications > 0; };
  if (timeout == portMAX_DELAY) task->cv.wait(lock, ready);
  else if (!task->cv.wait_for(lock, std::chrono::milliseconds(uint64_t(timeout) * portTICK_PERIOD_MS), ready)) {
    ++notificationTimeouts; return 0;
  }
  const uint32_t result = task->notifications;
  if (clear) task->notifications = 0; else --task->notifications;
  ++notificationWakeups; return result;
}
void vTaskDelete(TaskHandle_t) {}
void vTaskDelay(TickType_t ticks) { std::this_thread::sleep_for(std::chrono::milliseconds(uint64_t(ticks) * portTICK_PERIOD_MS)); }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return makeSemaphore(true); }
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return makeSemaphore(false); }
BaseType_t xSemaphoreTake(SemaphoreHandle_t handle, TickType_t timeout) {
  auto s = static_cast<FakeSemaphore*>(handle); if (!s) std::abort();
  std::unique_lock<std::mutex> lock(s->mutex);
  if (s->deleted) { ++deadSemaphoreUses; return pdFALSE; }
  const auto ready = [s] { return s->token || s->deleted; };
  if (timeout == portMAX_DELAY) s->cv.wait(lock, ready);
  else if (!s->cv.wait_for(lock, std::chrono::milliseconds(uint64_t(timeout) * portTICK_PERIOD_MS), ready)) return pdFALSE;
  if (s->deleted) { ++deadSemaphoreUses; return pdFALSE; }
  s->token = false; return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t handle) {
  auto s = static_cast<FakeSemaphore*>(handle); if (!s) std::abort();
  { std::lock_guard<std::mutex> lock(s->mutex);
    if (s->deleted) { ++deadSemaphoreUses; return pdFALSE; }
    s->token = true; }
  s->cv.notify_one(); return pdTRUE;
}
void vSemaphoreDelete(SemaphoreHandle_t handle) {
  auto s = static_cast<FakeSemaphore*>(handle);
  { std::lock_guard<std::mutex> lock(s->mutex); s->deleted = true; }
  s->cv.notify_all(); // release backing storage only after every worker is joined
}
}
int HardwareSerial::available() { std::lock_guard<std::mutex> lock(mutex); return active ? int(rx.size()) : 0; }
int HardwareSerial::peek() { std::lock_guard<std::mutex> lock(mutex); return !active || rx.empty() ? -1 : rx.front(); }
int HardwareSerial::read() {
  ++byteReads;
  std::lock_guard<std::mutex> lock(mutex);
  if (!active || rx.empty()) return -1;
  int n = rx.front(); rx.pop_front(); return n;
}
size_t HardwareSerial::readAvailable(uint8_t* buffer, size_t maximum) {
  ++bulkReads;
  std::lock_guard<std::mutex> lock(mutex);
  if (!active || !buffer) return 0;
  const auto count = std::min(std::min(maximum, size_t(64)), rx.size());
  for (size_t i = 0; i < count; ++i) { buffer[i] = rx.front(); rx.pop_front(); }
  bulkBytes += unsigned(count);
  maxBulkBytes = std::max(maxBulkBytes.load(), unsigned(count));
  return count;
}
bool HardwareSerial::flush(uint32_t timeout) {
  if (++flushCalls == 1) firstFlushBudget = timeout;
  const bool complete = !onFlush || onFlush(timeout);
  if (complete) pendingTx = false;
  else error = HardwareSerialStartError::Timeout;
  return complete;
}
size_t HardwareSerial::write(const uint8_t* data, size_t size, uint32_t timeout) {
  if (!active) return 0;
  if (++writeCalls == 1) firstWriteBudget = timeout;
  const uint32_t stall = writeWaitMs;
  if (stall) {
    std::this_thread::sleep_for(std::chrono::milliseconds(std::min(stall, timeout)));
    if (stall >= timeout) { error = HardwareSerialStartError::Timeout; return 0; }
  }
  error = HardwareSerialStartError::None;
  if (dmaEnabled && size >= dmaThreshold) ++dmaWrites; else ++irqWrites;
  if (requireDrain && pendingTx.exchange(true)) { ++undrainedWrites; return size; }
  writtenBytes += unsigned(size);
  if (onWrite) onWrite(data, size);
  // Core DMA/TX waits share this task notification slot with request wakes.
  if (drainNotificationAfterWrite) drainedWriteNotifications += ulTaskNotifyTake(pdTRUE, 0);
  return size;
}
bool HardwareSerial::waitReadable(uint32_t ms) {
  ++waits;
  if (ms <= 2) ++shortWaits;
  maxWaitMs = std::max(maxWaitMs.load(), ms);
  const uint32_t stall = stallWaitMs.exchange(0);
  if (stall) { stallEntered = true; std::this_thread::sleep_for(std::chrono::milliseconds(stall)); stallExited = true; }
  const auto current = xTaskGetCurrentTaskHandle();
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (!active || !rx.empty()) return !rx.empty();
    CHECK(rxWaiter == nullptr || rxWaiter == current);
    rxWaiter = current;
  }
  ++waiting;
  (void)ulTaskNotifyTake(pdTRUE, TickType_t((uint64_t(ms) + portTICK_PERIOD_MS - 1) / portTICK_PERIOD_MS));
  --waiting;
  std::lock_guard<std::mutex> lock(mutex);
  if (rxWaiter == current) rxWaiter = nullptr;
  return active && !rx.empty();
}
void HardwareSerial::inject(const uint8_t* data, size_t size) {
  std::lock_guard<std::mutex> lock(mutex);
  const bool wasEmpty = rx.empty();
  rx.insert(rx.end(), data, data + size);
  // The real ISR only notifies when the RX ring changes from empty to nonempty.
  if (wasEmpty && size && rxWaiter) { ++rxNotifies; xTaskNotifyGive(rxWaiter); }
}
bool HardwareSerial::injectWhenWaiting(const uint8_t* data, size_t size, uint32_t timeoutMs) {
  const uint32_t began = millis();
  do {
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (rxWaiter && rx.empty()) {
        rx.insert(rx.end(), data, data + size);
        ++rxNotifies; xTaskNotifyGive(rxWaiter);
        return true;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  } while (uint32_t(millis() - began) < timeoutMs);
  return false;
}
struct Controller {
  HardwareSerial& serial;
  c3::Decoder decoder;
  uint32_t session = 1234;
  unsigned hellos = 0, requests = 0, executions = 0, acks = 0;
  bool dropFirst = false, dropAll = false, malformedHello = false, zeroSession = false;
  bool wrongBeforeCorrect = false, invalidOnly = false;
  c3::Frame cached, stale, deferred;
  std::mutex deferredMutex;
  std::atomic<bool> deferReplies{false}, deferredReady{false};
  std::atomic<bool> blockPing{false}, pingBlocked{false}, releasePing{false};
  Controller(HardwareSerial& s) : serial(s) {
    serial.onWrite = [this](const uint8_t* bytes, size_t size) {
      c3::Frame f;
      for (size_t i = 0; i < size; ++i) if (decoder.feed(bytes[i], f)) receive(f);
    };
  }
  void inject(const c3::Frame& f) {
    uint8_t wire[c3::MaxWire];
    const size_t n = c3::encode(f, wire, sizeof(wire)); CHECK(n != 0);
    serial.inject(wire, n);
  }
  void releaseDeferred() {
    std::lock_guard<std::mutex> lock(deferredMutex);
    CHECK(deferredReady); deferReplies = false; deferredReady = false;
    uint8_t wire[c3::MaxWire]; const auto size = c3::encode(deferred, wire, sizeof(wire));
    CHECK(size > 0); CHECK(serial.injectWhenWaiting(wire, size, 500));
  }
  void receive(const c3::Frame& f) {
    if (f.type == c3::Type::Hello) {
      ++hellos; ++session;
      c3::Frame reply; reply.type = c3::Type::HelloReply; reply.id = f.id;
      reply.session = zeroSession ? 0 : session;
      c3::Writer w(reply.payload, sizeof(reply.payload));
      w.u16(c3::MaxPayload); w.u32(c3::WiFi | c3::HCI); w.u32(88);
      w.u8(4); w.u8(1); w.u16(512); w.string("test-c3");
      reply.size = uint16_t(w.size() - (malformedHello ? 1 : 0));
      if (wrongBeforeCorrect || invalidOnly) {
        c3::Frame wrong = reply; wrong.type = c3::Type::Response; inject(wrong);
        wrong = reply; ++wrong.id; inject(wrong);
        wrong = reply; wrong.status = c3::ProtocolError;
        if (invalidOnly) { inject(wrong); return; }
      }
      inject(reply);
    } else if (f.type == c3::Type::Request) {
      ++requests;
      if (f.session != cached.session || f.id != cached.id) {
        ++executions; cached = f; cached.type = c3::Type::Response;
      } else {
        CHECK(f.opcode == cached.opcode && f.size == cached.size);
        CHECK(std::memcmp(f.payload, cached.payload, f.size) == 0);
      }
      if (deferReplies) {
        std::lock_guard<std::mutex> lock(deferredMutex); deferred = cached; deferredReady = true; return;
      }
      if (dropAll || (dropFirst && requests == 1)) { stale = cached; return; }
      if (wrongBeforeCorrect) {
        c3::Frame wrong = cached; ++wrong.opcode; inject(wrong);
        wrong = cached; --wrong.session; inject(wrong);
      }
      inject(cached);
    } else if (f.type == c3::Type::Ack) ++acks;
    else if (f.type == c3::Type::Ping) {
      if (blockPing) {
        pingBlocked = true;
        while (!releasePing) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return; // also simulate a lost Pong, so RX cannot rescue a lost request wake
      }
      c3::Frame pong = f; pong.type = c3::Type::Pong; inject(pong);
    }
  }
};

static void echo(ESPLinkClass& link, uint32_t timeout = 1000) {
  const uint8_t data[] = {0, 1, 2, 0, 254, 255};
  uint8_t reply[16]; size_t size = sizeof(reply);
  CHECK(link.request(c3::Opcode::Echo, data, sizeof(data), reply, size, timeout) == c3::Ok);
  CHECK(size == sizeof(data) && !std::memcmp(data, reply, size));
}
static void stop(ESPLinkClass& link) { link.end(); joinTasks(); CHECK(!link.ready()); }

static void testHandshakeAndMatching() {
  HardwareSerial serial; Controller c(serial); ESPLinkClass link;
  c.wrongBeforeCorrect = true;
  CHECK(link.begin(serial, 115200, 500));
  CHECK(link.session() == c.session && link.capabilities().maxBLEConnections == 1);
  const unsigned prior = c.hellos;
  CHECK(link.begin(serial, 115200, 500) && c.hellos == prior);
  echo(link);
  CHECK(link.stats().ignored >= 4);
  stop(link);
  CHECK(link.begin(serial, 115200, 500)); echo(link); stop(link);
}
static void testRejectedHellos() {
  for (unsigned mode = 0; mode < 3; ++mode) {
    HardwareSerial serial; Controller c(serial); ESPLinkClass link;
    c.malformedHello = mode == 0; c.zeroSession = mode == 1; c.invalidOnly = mode == 2;
    CHECK(!link.begin(serial, 115200, 200));
    CHECK(!link.ready() && link.session() == 0 && link.lastError() == c3::ProtocolError);
    stop(link);
  }
}
static void testReliableRetry() {
  HardwareSerial serial; Controller c(serial); ESPLinkClass link;
  c.dropFirst = true;
  CHECK(link.begin(serial, 115200, 500));
  echo(link, 900);
  CHECK(c.requests >= 2 && c.executions == 1 && link.stats().retries >= 1);
  stop(link);
}
static void testUnknownAndRecovery() {
  HardwareSerial serial; Controller c(serial); ESPLinkClass link;
  CHECK(link.begin(serial, 115200, 500));
  const uint32_t firstSession = link.session();
  c.dropAll = true;
  const uint8_t byte = 42; uint8_t reply[4]; size_t size = sizeof(reply);
  CHECK(link.request(c3::Opcode::SocketWrite, &byte, 1, reply, size, 650) == c3::OutcomeUnknown);
  CHECK(size == 0 && !link.ready() && link.session() == 0 && c.executions == 1);
  CHECK(link.request(c3::Opcode::Echo, &byte, 1, reply, size, 50) == c3::LinkLost);
  c.dropAll = false;
  CHECK(link.begin(serial, 115200, 500));
  CHECK(link.session() != firstSession);
  c.inject(c.stale);
  echo(link);
  CHECK(c.executions == 2);
  stop(link);
}
static void testReplyCapacity() {
  HardwareSerial serial; Controller c(serial); ESPLinkClass link;
  CHECK(link.begin(serial, 115200, 500));
  const uint8_t bytes[] = {1, 2, 3, 4}; uint8_t reply[2] = {55, 66}; size_t size = sizeof(reply);
  CHECK(link.request(c3::Opcode::Echo, bytes, sizeof(bytes), reply, size, 500) == c3::BufferTooSmall);
  CHECK(size == 4 && reply[0] == 55 && reply[1] == 66 && link.ready());
  size = sizeof(reply);
  CHECK(link.request(c3::Opcode::Echo, nullptr, 1, reply, size, 500) == c3::InvalidArgument && size == 0);
  stop(link);
}

static void testStopTimeoutRecovery() {
  HardwareSerial serial; Controller c(serial); ESPLinkClass link;
  CHECK(link.begin(serial, 115200, 500));
  serial.stallWaitMs = 2300;
  while (!serial.stallEntered) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  const size_t taskCount = tasks.size();
  link.end();
  CHECK(!link.ready() && link.lastError() == c3::Timeout);
  CHECK(bool(serial)); // timeout must retain ownership until the worker stops
  CHECK(link.begin(serial, 115200, 1000));
  CHECK(tasks.size() == taskCount + 1);
  echo(link);
  stop(link);
}
static void testConcurrentEndAndReaders() {
  HardwareSerial serial; Controller c(serial); ESPLinkClass link;
  CHECK(link.begin(serial, 115200, 500));
  c.dropAll = true;
  std::atomic<bool> observing{true};
  std::atomic<int32_t> first{99}, queued{99};
  std::thread reader([&] {
    while (observing) {
      (void)link.ready(); (void)link.session(); (void)link.stats(); (void)link.capabilities();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });
  std::thread pending([&] {
    uint8_t response[1]; size_t size = sizeof(response);
    first = link.request(c3::Opcode::Echo, nullptr, 0, response, size, 350);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  std::thread waiting([&] {
    uint8_t response[1]; size_t size = sizeof(response);
    queued = link.request(c3::Opcode::Echo, nullptr, 0, response, size, 1500);
  });
  link.end();
  pending.join(); waiting.join();
  observing = false; reader.join(); joinTasks();
  CHECK(first == c3::OutcomeUnknown && queued == c3::LinkLost);
  CHECK(!link.ready() && deadSemaphoreUses == 0);
}
static void testReconfigureAndCreationFailure() {
  HardwareSerial first, second;
  Controller a(first), b(second); ESPLinkClass link;
  failNextTask = true;
  CHECK(!link.begin(first, 115200, 500));
  CHECK(!bool(first) && link.lastError() == c3::NoMemory);
  CHECK(link.begin(first, 115200, 500));
  echo(link);
  CHECK(link.begin(second, 460800, 500));
  CHECK(!bool(first) && bool(second) && a.hellos == 1 && b.hellos == 1);
  echo(link);
  stop(link);
  link.end(); // idempotent with retained State and no worker
  CHECK(!bool(second) && deadSemaphoreUses == 0);
}

static void testExplicitAndBorrowedBindings() {
  HardwareSerial serial; Controller c(serial); ESPLinkClass link;
  serial.begin(115200);
  Stream& borrowed = serial;
  CHECK(link.begin(borrowed, 500)); echo(link);
  stop(link);
  CHECK(bool(serial)); // end() must not close a caller-managed Stream
  CHECK(link.begin()); echo(link); stop(link);
  CHECK(bool(serial)); serial.end();
  CHECK(serial.flushCalls == 0); // borrowed streams keep their existing write policy

  // A custom Arduino serial type takes the generic managed-serial template,
  // even when the surrounding build enables the RTOS scheduling backend.
  struct CustomSerial : HardwareSerial {};
  CustomSerial managed; Controller remote(managed); ESPLinkClass custom;
  CHECK(custom.begin(managed, 460800, 500)); echo(custom);
  stop(custom); CHECK(!bool(managed));
  CHECK(custom.begin()); echo(custom); stop(custom); CHECK(!bool(managed));
  CHECK(managed.flushCalls == 0); // generic serial templates do not require CI flush()
}
static void testDefaultPortAndExplicitOverride() {
  Controller defaultController(Serial2);
  HardwareSerial selected; Controller selectedController(selected);
  ESPLinkClass link;
  CHECK(link.begin());
  CHECK(Serial2.configuredBaud == ESPLINK_DEFAULT_BAUD);
  CHECK(ESPLINK_DEFAULT_BAUD == 921600 && c3::DefaultBaud == 921600);
  CHECK(Serial2.configuredRxBufferSize == ESPLINK_CI13XX_RX_BUFFER_SIZE);
#if ESPLINK_CI13XX_TX_DMA
  CHECK(Serial2.dmaEnableCalls > 0 && Serial2.txDMAEnabled());
#endif
  echo(link);
#if ESPLINK_CI13XX_UART_BULK_RX
  CHECK(Serial2.bulkReads > 0 && Serial2.byteReads == 0);
#endif
  const unsigned hellos = defaultController.hellos;
  CHECK(link.begin() && defaultController.hellos == hellos);
  stop(link);
  CHECK(!bool(Serial2) && Serial2.configuredRxBufferSize == 0);
  CHECK(link.begin() && defaultController.hellos == hellos + 1);
  CHECK(link.begin(selected, 115200, 500));
  CHECK(!bool(Serial2) && selected.configuredBaud == 115200);
  stop(link);
  CHECK(link.begin() && selected.configuredBaud == 115200);
  CHECK(!bool(Serial2) && defaultController.hellos == hellos + 1);
  echo(link); stop(link);

  // Even a failed explicit handshake is remembered; reconnect must not return
  // to the automatic default port/baud behind the user's back.
  selectedController.malformedHello = true;
  CHECK(!link.begin(selected, 230400, 100));
  selectedController.malformedHello = false;
  CHECK(link.begin() && selected.configuredBaud == 230400);
  CHECK(!bool(Serial2) && defaultController.hellos == hellos + 1);
  stop(link);
  Serial2.onWrite = nullptr;
}
static size_t deletedSemaphores() {
  size_t count = 0;
  for (const auto& semaphore : semaphores) {
    std::lock_guard<std::mutex> lock(semaphore->mutex);
    if (semaphore->deleted) ++count;
  }
  return count;
}
static void testScopedDestruction() {
  HardwareSerial serial; Controller c(serial);
  const auto before = deletedSemaphores();
  {
    ESPLinkClass link;
    CHECK(link.begin(serial, 115200, 500)); echo(link);
    link.end();
    CHECK(deletedSemaphores() == before); // end retains locks for queued users/reconnect
    CHECK(link.begin());
    serial.stallWaitMs = 60;
    while (!serial.stallEntered) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    // Destruction must wait for this worker to leave its borrowed state.
  }
  CHECK(serial.stallExited && !bool(serial));
  joinTasks();
  CHECK(deletedSemaphores() == before + 4);
  CHECK(deadSemaphoreUses == 0);

  serial.begin(115200);
  {
    ESPLinkClass borrowed;
    CHECK(borrowed.begin(static_cast<Stream&>(serial), 500)); echo(borrowed);
  }
  joinTasks();
  CHECK(bool(serial)); // borrowed streams remain under caller ownership at destruction
  CHECK(deletedSemaphores() == before + 8);
  serial.end();
}
template<class Predicate>
static void awaitCondition(Predicate ready, uint32_t timeout = 1500) {
  const uint32_t started = millis();
  while (!ready() && uint32_t(millis() - started) < timeout)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  CHECK(ready());
}
static void testFrameDrain() {
  HardwareSerial serial; Controller c(serial); ESPLinkClass link;
#if ESPLINK_CI13XX_TX_DRAIN
  // Model a TX path that accepts a subsequent write but loses it unless the
  // preceding frame was drained. ACKs and requests share that same path.
  serial.requireDrain = true;
#endif
  CHECK(link.begin(serial, 115200, 500));
  echo(link); echo(link);
  CHECK(link.stats().retries == 0);
  stop(link);
  CHECK(c.hellos == 1 && c.requests == 2 && c.executions == 2);
  CHECK(serial.undrainedWrites == 0);
#if ESPLINK_CI13XX_TX_DRAIN
  CHECK(serial.flushCalls == serial.writeCalls && serial.flushCalls >= 3);
#else
  CHECK(serial.flushCalls == 0 && serial.writeCalls >= 3);
#endif
}
static void testDrainFailureAndDeadline() {
#if ESPLINK_CI13XX_TX_DRAIN
  for (unsigned mode = 0; mode < 3; ++mode) {
    HardwareSerial serial; Controller c(serial); ESPLinkClass link;
    serial.onFlush = [mode](uint32_t timeout) {
      if (mode == 0) return false;
      // Also cover a successful drain that consumed the entire allowance.
      const uint32_t delay = mode == 1 ? timeout : 60;
      std::this_thread::sleep_for(std::chrono::milliseconds(delay));
      return true;
    };
    if (mode == 2) serial.writeWaitMs = 400;
    const uint32_t started = millis();
    CHECK(!link.begin(serial, 115200, 200));
    CHECK(link.lastError() == c3::Timeout);
    stop(link);
    const uint32_t elapsed = millis() - started;
    CHECK(serial.flushCalls > 0 && serial.firstFlushBudget <= 200);
    CHECK(serial.writtenBytes == 0 && c.hellos == 0);
    CHECK(elapsed >= 190 && elapsed < 400);
    if (mode < 2) {
      // Neither drain failure nor an exhausted budget may start a new frame.
      CHECK(serial.writeCalls == 0 && serial.firstWriteBudget == 0);
    } else {
      CHECK(serial.writeCalls == 1 && serial.firstWriteBudget > 0);
      CHECK(serial.firstFlushBudget > 60);
      CHECK(serial.firstWriteBudget <= serial.firstFlushBudget - 60);
    }
  }
#endif
}
static void testBulkReceiveAndDmaFallback() {
  for (unsigned fail = 0; fail < 2; ++fail) {
    HardwareSerial serial; serial.dmaRequestFails = fail != 0;
    Controller c(serial); ESPLinkClass link;
    CHECK(link.begin(serial, 115200, 500));
    CHECK(serial.configuredRxBufferSize == ESPLINK_CI13XX_RX_BUFFER_SIZE);
#if ESPLINK_CI13XX_TX_DMA
    CHECK(serial.dmaEnableCalls == 1);
    CHECK(serial.txDMAEnabled() == (fail == 0));
    if (!fail) CHECK(serial.dmaThreshold == ESPLINK_CI13XX_DMA_THRESHOLD);
#else
    CHECK(serial.dmaEnableCalls == 0 && !serial.txDMAEnabled());
#endif
    uint8_t payload[c3::MaxPayload], reply[c3::MaxPayload];
    for (size_t i = 0; i < sizeof(payload); ++i) payload[i] = uint8_t((i * 37) & 255);
    size_t size = sizeof(reply);
    CHECK(link.request(c3::Opcode::Echo, payload, sizeof(payload), reply, size, 1000) == c3::Ok);
    CHECK(size == sizeof(payload) && std::memcmp(payload, reply, size) == 0);
    awaitCondition([&] { return link.stats().rxBytes >= sizeof(payload); });
    const auto stats = link.stats();
    CHECK(stats.rxBytes >= sizeof(payload) && stats.rxBatches > 0);
#if ESPLINK_CI13XX_UART_BULK_RX
    CHECK(serial.byteReads == 0 && serial.bulkReads > 1);
    CHECK(serial.maxBulkBytes <= ESPLINK_CI13XX_RX_CHUNK_SIZE && serial.maxBulkBytes <= 64);
    CHECK(stats.rxBatches < stats.rxBytes);
#else
    CHECK(serial.bulkReads == 0 && serial.byteReads >= sizeof(payload));
    CHECK(stats.rxBatches == stats.rxBytes);
#endif
#if ESPLINK_CI13XX_TX_DMA
    if (!fail) CHECK(serial.dmaWrites > 0);
    else CHECK(serial.dmaWrites == 0 && serial.irqWrites > 0);
#else
    CHECK(serial.dmaWrites == 0 && serial.irqWrites > 0);
#endif
    echo(link); // shorter frames after a full payload must not expose stale tail bytes
    stop(link); CHECK(!serial.txDMAEnabled());
  }
}
static void testNotifiedIdleAndRx() {
  HardwareSerial serial; Controller c(serial); ESPLinkClass link;
  CHECK(link.begin(serial, 115200, 500));
#if ESPLINK_CI13XX_TASK_NOTIFY
  awaitCondition([&] { return serial.waiting > 0 && serial.maxWaitMs >= 500; });
  const auto waits = serial.waits.load();
  const auto wakeups = link.stats().workerWakeups;
  std::this_thread::sleep_for(std::chrono::milliseconds(40));
  // Idle does not churn through a two-millisecond polling loop.
  CHECK(serial.waits <= waits + 1);
  CHECK(link.stats().workerWakeups <= wakeups + 1);
  const auto notified = taskNotifications.load();
  echo(link);
  CHECK(taskNotifications > notified && notificationWakeups > 0);
#else
  awaitCondition([&] { return serial.waits > 1; });
  CHECK(serial.maxWaitMs <= ESPLINK_CI13XX_POLL_MS);
  echo(link);
#endif
  c.deferReplies = true;
  std::atomic<int32_t> result{c3::Busy};
  std::thread requester([&] {
    const uint8_t input = 73; uint8_t output = 0; size_t size = 1;
    result = link.request(c3::Opcode::Echo, &input, 1, &output, size, 1000);
    CHECK(result == c3::Ok && output == input && size == 1);
  });
  awaitCondition([&] { return c.deferredReady.load() && serial.waiting > 0; });
  const auto rxNotifies = serial.rxNotifies.load();
  c.releaseDeferred(); requester.join();
  CHECK(serial.rxNotifies > rxNotifies); // empty RX ring -> ISR notification -> RPC completion
#if ESPLINK_CI13XX_TASK_NOTIFY
  awaitCondition([&] { return serial.waiting > 0; });
  const auto beforeStop = taskNotifications.load();
  stop(link);
  CHECK(taskNotifications > beforeStop);
#else
  stop(link);
#endif
}
static void testNoNotificationToExitedWorker() {
  HardwareSerial serial; Controller c(serial);
  {
    ESPLinkClass link; CHECK(link.begin(serial, 115200, 500));
    serial.stallWaitMs = 2200;
    awaitCondition([&] { return serial.stallEntered.load(); });
    link.end(); CHECK(link.lastError() == c3::Timeout);
    joinTasks(); // worker has self-deleted, but end() retained its old handle
    link.end(); // must consume stopped without notifying that obsolete handle
    CHECK(!bool(serial) && !link.ready());
  }
  CHECK(deadTaskNotifyUses == 0 && deadSemaphoreUses == 0);
}
static void testRequestSurvivesConsumedTxNotification() {
#if ESPLINK_CI13XX_TASK_NOTIFY
  HardwareSerial serial; Controller c(serial); ESPLinkClass link;
  CHECK(link.begin(serial, 115200, 500));
  c.blockPing = true; serial.drainNotificationAfterWrite = true;
  awaitCondition([&] { return c.pingBlocked.load(); }, 1800);
  const auto gives = taskNotifications.load(), drained = serial.drainedWriteNotifications.load();
  std::atomic<int32_t> result{c3::Busy};
  std::thread requester([&] {
    const uint8_t input = 26; uint8_t output = 0; size_t size = 1;
    result = link.request(c3::Opcode::Echo, &input, 1, &output, size, 200);
    CHECK(result == c3::Ok && output == input && size == 1);
  });
  awaitCondition([&] { return taskNotifications > gives; });
  c.releasePing = true;
  requester.join();
  CHECK(serial.drainedWriteNotifications > drained);
  // The UART consumed the notification, so the worker had to recheck pending
  // state after write() rather than sleep for its long idle interval.
  stop(link);
#endif
}
int main() {
  testHandshakeAndMatching(); testRejectedHellos(); testReliableRetry();
  testUnknownAndRecovery(); testReplyCapacity();
  testStopTimeoutRecovery(); testConcurrentEndAndReaders(); testReconfigureAndCreationFailure();
  testExplicitAndBorrowedBindings(); testScopedDestruction(); testDefaultPortAndExplicitOverride();
  testFrameDrain(); testDrainFailureAndDeadline();
  testBulkReceiveAndDmaFallback(); testNotifiedIdleAndRx(); testNoNotificationToExitedWorker(); testRequestSurvivesConsumedTxNotification();
  CHECK(deadSemaphoreUses == 0 && deadTaskNotifyUses == 0);
  std::cout << "ESPLink actual runtime: " << checks << " assertions passed\n";
}
