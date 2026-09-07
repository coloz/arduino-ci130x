// SPDX-License-Identifier: MIT
#include "ESPLink.h"
#include <new>
#include "ESPLinkDefaultSerial.h"
#if ESPLINK_CI13XX_RTOS
extern "C" {
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
}
#endif

namespace {
#if ESPLINK_CI13XX_RTOS
#if defined(configMAX_PRIORITIES)
static_assert(ESPLINK_CI13XX_TASK_PRIORITY < configMAX_PRIORITIES, "CI worker priority exceeds core limit");
#endif
using Mutex = SemaphoreHandle_t;
TickType_t ticks(uint32_t ms) {
  if (!ms) return 0;
  const uint64_t n = (uint64_t(ms) + portTICK_PERIOD_MS - 1) / portTICK_PERIOD_MS;
  return static_cast<TickType_t>(n >= portMAX_DELAY ? portMAX_DELAY - 1 : n);
}
bool acquire(Mutex& sem, uint32_t timeout) { return xSemaphoreTake(sem, ticks(timeout)) == pdTRUE; }
void release(Mutex& sem) { xSemaphoreGive(sem); }
class Lock {
 public:
  explicit Lock(Mutex& sem) : sem_(sem) { xSemaphoreTake(sem_, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(sem_); }
 private: Mutex sem_;
};
#else
// No operating-system dependency in the cooperative backend. This rejects
// reentrant calls but is not a substitute for a mutex between application tasks.
struct Mutex { bool held = false; };
bool acquire(Mutex& sem, uint32_t) { if (sem.held) return false; sem.held = true; return true; }
void release(Mutex& sem) { sem.held = false; }
class Lock { public: explicit Lock(Mutex&) {} };
#endif
class RpcRelease {
 public:
  explicit RpcRelease(Mutex& sem) : sem_(sem) {}
  ~RpcRelease() { release(sem_); }
 private: Mutex& sem_;
};
// Small HCI/status messages copy only their initialized payload, not 1 KiB.
void copyFrame(c3::Frame& dst, const c3::Frame& src) {
  if (&dst == &src) return;
  dst.type = src.type; dst.session = src.session; dst.id = src.id;
  dst.opcode = src.opcode; dst.size = src.size; dst.status = src.status;
  if (src.size) memcpy(dst.payload, src.payload, src.size);
}
uint32_t nextNonce() {
  static uint32_t counter = 0x45535000U;
  counter += 0x9e3779b9U;
  const uint32_t n = counter ^ micros(); return n ? n : 1;
}
}

struct ESPLinkClass::State {
  Binding binding;
  Mutex rpcMutex{}, mutex{};
#if ESPLINK_CI13XX_RTOS
  SemaphoreHandle_t done = nullptr, stopped = nullptr;
  TaskHandle_t task = nullptr;
  alignas(4) uint8_t rxStorage[ESPLINK_CI13XX_RX_BUFFER_SIZE] = {};
#if ESPLINK_CI13XX_UART_BULK_RX
  uint8_t rxChunk[ESPLINK_CI13XX_RX_CHUNK_SIZE];
#endif
#endif
  bool running = false, online = false, pending = false, sent = false, complete = false;
  bool pumping = false;
  uint32_t session = 0, nextId = 0, lastRx = 0, lastPing = 0, lastTx = 0;
  uint32_t started = 0, deadlineMs = 0;
  int32_t error = c3::NotConnected, result = c3::NotConnected;
  // Keep frame and UART scratch space off small Arduino application stacks.
  c3::Frame request, response, incoming, outgoing;
  alignas(4) uint8_t wire[c3::MaxWire];
  c3::Decoder decoder;
  ESPLinkCapabilities capabilities;
  ESPLinkStats stats;
};
ESPLinkClass ESPLink;

ESPLinkClass::~ESPLinkClass() {
  // As with Stream itself, destruction requires exclusive ownership: all
  // application calls have returned and the bound UART still exists.
  State* s = state_; if (!s) return;
  {
    Lock lock(s->mutex);
#if ESPLINK_CI13XX_RTOS && ESPLINK_CI13XX_TASK_NOTIFY
    // Notify under the exit-check lock while the worker is known to be alive.
    // A repeated stop may retain an already deleted TaskHandle.
    if (s->running && s->task) xTaskNotifyGive(s->task);
#endif
    s->running = s->online = s->pending = false;
  }
#if ESPLINK_CI13XX_RTOS
  // Unlike end(), destruction cannot return with a worker using this object.
  // A previous timed-out end() leaves stopped/task intact for this wait.
  if (s->task) xSemaphoreTake(s->stopped, portMAX_DELAY);
#endif
  if (s->binding.stop) s->binding.stop(s->binding.serial);
#if ESPLINK_CI13XX_RTOS
  vSemaphoreDelete(s->done); vSemaphoreDelete(s->stopped);
  vSemaphoreDelete(s->rpcMutex); vSemaphoreDelete(s->mutex);
#endif
  delete s;
}

bool ESPLinkClass::begin(Stream& serial, uint32_t timeoutMs) {
  Binding binding; binding.serial = &serial;
  return beginBinding(binding, timeoutMs);
}
bool ESPLinkClass::begin() {
  if (remembered_.serial) return beginBinding(remembered_, 10000);
  return esplink_detail::beginDefaultSerial(*this);
}
#if ESPLINK_CI13XX_RTOS
bool ESPLinkClass::begin(HardwareSerial& serial, uint32_t baud, uint32_t timeoutMs) {
  Binding binding; binding.serial = &serial; binding.baud = baud;
  binding.start = [](Stream* port, uint32_t speed, uint8_t* rx, size_t size) -> bool {
    auto& uart = *static_cast<HardwareSerial*>(port);
    if (uart) uart.end();
    if (!uart.setRxBuffer(rx, size)) return false;
    uart.begin(speed, SERIAL_8N1);
    if (!uart || uart.lastError() != HardwareSerialStartError::None) {
      uart.end(); uart.setRxBuffer(nullptr, 0); return false;
    }
#if ESPLINK_CI13XX_TX_DMA
    // The core arbitrates DMA1; if unavailable, UART interrupts remain usable.
    (void)uart.enableTxDMA(true, ESPLINK_CI13XX_DMA_THRESHOLD);
#endif
    return true;
  };
  binding.stop = [](Stream* port) {
    auto& uart = *static_cast<HardwareSerial*>(port);
    uart.end(); uart.setRxBuffer(nullptr, 0);
  };
  binding.write = [](Stream* port, const uint8_t* data, size_t size, uint32_t timeout) {
    auto& uart = *static_cast<HardwareSerial*>(port);
#if ESPLINK_CI13XX_TX_DRAIN
    // CI1306 hardware tests require a drained TX path between encoded frames.
    // Draining and writing share one budget; never start a frame after it expires.
    const uint32_t started = millis();
    if (!uart.flush(timeout)) return size_t(0);
    const uint32_t elapsed = millis() - started;
    if (elapsed >= timeout) return size_t(0);
    timeout -= elapsed;
#endif
    return uart.write(data, size, timeout);
  };
#if ESPLINK_CI13XX_UART_BULK_RX
  binding.read = [](Stream* port, uint8_t* data, size_t size) {
    return static_cast<HardwareSerial*>(port)->readAvailable(data, size);
  };
#endif
  binding.wait = [](Stream* port, uint32_t timeout) {
    static_cast<HardwareSerial*>(port)->waitReadable(timeout);
  };
  return beginBinding(binding, timeoutMs);
}
#endif

bool ESPLinkClass::beginBinding(const Binding& binding, uint32_t timeoutMs) {
  if (!binding.serial || !timeoutMs || (binding.start && !binding.baud)) return false;
#if ESPLINK_CI13XX_RTOS
  if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) return false;
#endif
  // First initialization precedes access by any other task. Keep State/locks
  // alive after end, allowing queued requests to finish without freed objects.
  if (!state_) {
    State* s = new (std::nothrow) State;
    if (!s) return false;
#if ESPLINK_CI13XX_RTOS
    s->rpcMutex = xSemaphoreCreateMutex(); s->mutex = xSemaphoreCreateMutex();
    s->done = xSemaphoreCreateBinary(); s->stopped = xSemaphoreCreateBinary();
    if (!s->rpcMutex || !s->mutex || !s->done || !s->stopped) {
      if (s->rpcMutex) vSemaphoreDelete(s->rpcMutex);
      if (s->mutex) vSemaphoreDelete(s->mutex);
      if (s->done) vSemaphoreDelete(s->done);
      if (s->stopped) vSemaphoreDelete(s->stopped);
      delete s; return false;
    }
#endif
    state_ = s;
  }
  State* s = state_;
#if !ESPLINK_CI13XX_RTOS
  if (s->pumping) { s->error = c3::Busy; return false; }
#endif
  const uint32_t began = millis();
  const auto remaining = [began, timeoutMs]() {
    const uint32_t elapsed = millis() - began;
    return elapsed < timeoutMs ? timeoutMs - elapsed : uint32_t(0);
  };
  if (!acquire(s->rpcMutex, timeoutMs)) { Lock lock(s->mutex); s->error = c3::Busy; return false; }
  RpcRelease releaseRpc(s->rpcMutex);
  // Save the desired binding even if startup fails, so begin() can retry it.
  remembered_ = binding;
#if ESPLINK_CI13XX_RTOS
  bool stopWorker;
  {
    Lock lock(s->mutex);
    stopWorker = s->task && (!s->running || !(s->binding == binding));
    if (stopWorker) {
#if ESPLINK_CI13XX_TASK_NOTIFY
      if (s->running) xTaskNotifyGive(s->task);
#endif
      s->running = s->online = s->pending = false; s->session = 0;
    }
  }
  if (stopWorker) {
    if (xSemaphoreTake(s->stopped, ticks(remaining())) != pdTRUE) {
      Lock lock(s->mutex); s->error = c3::Timeout; return false;
    }
    { Lock lock(s->mutex); s->task = nullptr; }
    if (s->binding.stop) s->binding.stop(s->binding.serial);
    s->binding = Binding{};
  }
  const bool needsStart = !s->task;
#else
  const bool needsStart = !s->running || !(s->binding == binding);
  if (needsStart && s->binding.serial) {
#if ESPLINK_CI13XX_RTOS && ESPLINK_CI13XX_TASK_NOTIFY
    // Notify under the exit-check lock while the worker is known to be alive.
    // A repeated stop may retain an already deleted TaskHandle.
    if (s->running && s->task) xTaskNotifyGive(s->task);
#endif
    s->running = s->online = s->pending = false; s->session = 0;
    if (s->binding.stop) s->binding.stop(s->binding.serial);
    s->binding = Binding{};
  }
#endif
  if (needsStart) {
    if (!remaining()) { Lock lock(s->mutex); s->error = c3::Timeout; return false; }
#if ESPLINK_CI13XX_RTOS
    uint8_t* rx = s->rxStorage; const size_t rxSize = sizeof(s->rxStorage);
#else
    uint8_t* rx = nullptr; const size_t rxSize = 0;
#endif
    if (binding.start && !binding.start(binding.serial, binding.baud, rx, rxSize)) {
      Lock lock(s->mutex); s->error = c3::IoError; return false;
    }
    s->binding = binding; s->decoder.reset();
    {
      Lock lock(s->mutex);
      s->running = true; s->online = false; s->session = 0;
      s->pending = s->sent = s->complete = false;
      s->lastRx = s->lastPing = millis();
    }
#if ESPLINK_CI13XX_RTOS
    (void)xSemaphoreTake(s->done, 0); (void)xSemaphoreTake(s->stopped, 0);
    BaseType_t created;
    {
      Lock lock(s->mutex);
      created = xTaskCreate(taskEntry, "esp-link", ESPLINK_CI13XX_TASK_STACK_WORDS, this, ESPLINK_CI13XX_TASK_PRIORITY, &s->task);
      if (created != pdPASS) { s->running = false; s->task = nullptr; s->error = c3::NoMemory; }
    }
    if (created != pdPASS) {
      if (s->binding.stop) s->binding.stop(s->binding.serial);
      s->binding = Binding{}; return false;
    }
#endif
  }
  if (ready()) return true;
  if (!remaining()) { Lock lock(s->mutex); s->error = c3::Timeout; return false; }
  auto& hello = s->request;
  {
    Lock lock(s->mutex);
    hello.type = c3::Type::Hello; hello.id = nextNonce();
    hello.session = 0; hello.opcode = 0; hello.size = 0; hello.status = c3::Ok;
  }
  uint8_t response[80]; size_t size = sizeof(response);
  const int32_t status = transact(hello, response, size, remaining());
  if (status == c3::Ok) {
    c3::Reader r(response, size); ESPLinkCapabilities caps;
    caps.maxPayload = r.u16(); caps.features = r.u32(); caps.bootID = r.u32();
    caps.maxSockets = r.u8(); caps.maxBLEConnections = r.u8(); caps.maxHciPacket = r.u16();
    r.string(caps.firmwareVersion, sizeof(caps.firmwareVersion));
    Lock lock(s->mutex);
    if (r.done() && caps.maxPayload == c3::MaxPayload && s->response.session) {
      s->session = s->response.session; s->nextId = 0; s->capabilities = caps;
      s->online = true; s->error = c3::Ok; s->lastRx = millis(); ++s->stats.sessionChanges;
    } else { s->online = false; s->session = 0; s->error = c3::ProtocolError; }
  }
  return ready();
}

void ESPLinkClass::end() {
  State* s = state_; if (!s) return;
#if !ESPLINK_CI13XX_RTOS
  if (s->pumping) { s->error = c3::Busy; return; }
#endif
#if ESPLINK_CI13XX_RTOS
  { Lock lock(s->mutex); if (xTaskGetCurrentTaskHandle() == s->task) return; }
#endif
  if (!acquire(s->rpcMutex, 2000)) { Lock lock(s->mutex); s->error = c3::Busy; return; }
  RpcRelease releaseRpc(s->rpcMutex);
  {
    Lock lock(s->mutex);
#if ESPLINK_CI13XX_RTOS && ESPLINK_CI13XX_TASK_NOTIFY
    if (s->running && s->task) xTaskNotifyGive(s->task);
#endif
    s->running = s->online = s->pending = false; s->session = 0; s->error = c3::NotConnected;
  }
#if ESPLINK_CI13XX_RTOS
  if (s->task) {
    if (xSemaphoreTake(s->stopped, ticks(2000)) != pdTRUE) {
      Lock lock(s->mutex); s->error = c3::Timeout; return;
    }
    { Lock lock(s->mutex); s->task = nullptr; }
  }
#endif
  if (s->binding.stop) s->binding.stop(s->binding.serial);
  s->binding = Binding{};
}
bool ESPLinkClass::ready() const {
  if (!state_) return false;
  Lock lock(state_->mutex); return state_->running && state_->online;
}
uint32_t ESPLinkClass::session() const {
  if (!state_) return 0;
  Lock lock(state_->mutex); return state_->online ? state_->session : 0;
}
int32_t ESPLinkClass::lastError() const {
  if (!state_) return c3::NotConnected;
  Lock lock(state_->mutex); return state_->error;
}
ESPLinkCapabilities ESPLinkClass::capabilities() const {
  if (!state_) return {};
  Lock lock(state_->mutex); return state_->capabilities;
}
ESPLinkStats ESPLinkClass::stats() const {
  if (!state_) return {};
  Lock lock(state_->mutex); return state_->stats;
}

int32_t ESPLinkClass::request(uint16_t opcode, const uint8_t* data, size_t size,
    uint8_t* response, size_t& responseSize, uint32_t timeoutMs) {
  State* s = state_;
  if (!s) { responseSize = 0; return c3::NotConnected; }
  if (size > c3::MaxPayload || (size && !data) || (responseSize && !response) || !timeoutMs) {
    responseSize = 0; return c3::InvalidArgument;
  }
#if ESPLINK_CI13XX_RTOS
  { Lock lock(s->mutex); if (xTaskGetCurrentTaskHandle() == s->task) { responseSize = 0; return c3::Busy; } }
#endif
#if !ESPLINK_CI13XX_RTOS
  if (s->pumping) { responseSize = 0; return c3::Busy; }
#endif
  const uint32_t waitStarted = millis();
  if (!acquire(s->rpcMutex, timeoutMs)) { responseSize = 0; return c3::Busy; }
  RpcRelease releaseRpc(s->rpcMutex);
  auto& frame = s->request;
  {
    Lock lock(s->mutex);
    if (!s->online || s->nextId == 0xffffffffU) {
      responseSize = 0; s->online = false; s->session = 0; return c3::LinkLost;
    }
    frame.session = s->session; frame.id = ++s->nextId;
    frame.type = c3::Type::Request; frame.opcode = opcode;
    frame.size = static_cast<uint16_t>(size); frame.status = c3::Ok;
    if (size) memcpy(frame.payload, data, size);
  }
  const uint32_t elapsed = millis() - waitStarted;
  if (elapsed >= timeoutMs) { responseSize = 0; return c3::Busy; }
  return transact(frame, response, responseSize, timeoutMs - elapsed);
}
int32_t ESPLinkClass::transact(c3::Frame& frame, uint8_t* response, size_t& responseSize,
    uint32_t timeoutMs) {
  State* s = state_;
#if ESPLINK_CI13XX_RTOS
  (void)xSemaphoreTake(s->done, 0);
#endif
  {
    Lock lock(s->mutex);
    copyFrame(s->request, frame); s->pending = true; s->sent = s->complete = false;
    s->started = millis(); s->deadlineMs = timeoutMs; s->lastTx = 0; s->result = c3::Timeout;
#if ESPLINK_CI13XX_RTOS && ESPLINK_CI13XX_TASK_NOTIFY
    if (s->running && s->task) xTaskNotifyGive(s->task);
#endif
  }
#if ESPLINK_CI13XX_RTOS
  (void)xSemaphoreTake(s->done, ticks(timeoutMs));
#else
  while (!s->complete && uint32_t(millis() - s->started) < timeoutMs) {
    poll();
    if (!s->complete) yield();
  }
#endif
  Lock lock(s->mutex);
  if (!s->complete) {
    s->pending = false; s->online = false; s->session = 0;
    s->result = s->sent && frame.type == c3::Type::Request ? c3::OutcomeUnknown : c3::Timeout;
    ++s->stats.timeouts;
  }
  int32_t result = s->result;
  if (s->complete && result == c3::Ok) {
    if (s->response.size > responseSize) { responseSize = s->response.size; result = c3::BufferTooSmall; }
    else {
      responseSize = s->response.size;
      if (responseSize) memcpy(response, s->response.payload, responseSize);
    }
  } else responseSize = 0;
  s->error = result; return result;
}
bool ESPLinkClass::ping(uint32_t timeoutMs) {
  uint8_t response[4]; size_t size = sizeof(response);
  const uint8_t token[4] = {'E', 'S', 'P', 'L'};
  return request(c3::Opcode::Echo, token, sizeof(token), response, size, timeoutMs) == c3::Ok &&
    size == sizeof(token) && memcmp(token, response, size) == 0;
}
bool ESPLinkClass::transmit(const c3::Frame& frame) {
  State* s = state_;
  const size_t size = c3::encode(frame, s->wire, sizeof(s->wire));
  if (!size) return false;
  // Partial/nonblocking writes share the transaction's remaining budget. A
  // Stream with an internally blocking write must impose its own finite bound.
  uint32_t budget = 1000;
  if (frame.type == c3::Type::Request || frame.type == c3::Type::Hello || frame.type == c3::Type::Ack) {
    Lock lock(s->mutex);
    if (frame.id != s->request.id ||
        (frame.type != c3::Type::Ack && !s->pending)) return false;
    const uint32_t elapsed = millis() - s->started;
    if (elapsed >= s->deadlineMs) return false;
    const uint32_t remaining = s->deadlineMs - elapsed;
    if (remaining < budget) budget = remaining;
  }
  bool result;
  if (s->binding.write) result = s->binding.write(s->binding.serial, s->wire, size, budget) == size;
  else {
    // Arduino Stream does not define a timed write API. The selected Stream
    // must have a bounded write implementation; retries preserve the request ID.
    size_t done = 0; const uint32_t began = millis();
    do {
      const size_t n = s->binding.serial->write(s->wire + done, size - done);
      if (n > size - done) break;
      done += n;
      if (done < size) yield();
    } while (done < size && uint32_t(millis() - began) < budget);
    result = done == size;
  }
  if (result) { Lock lock(s->mutex); ++s->stats.transmitted; }
  return result;
}
void ESPLinkClass::receive(const c3::Frame& frame) {
  State* s = state_; bool ack = false;
  {
    Lock lock(s->mutex); ++s->stats.received;
    const bool isHello = s->pending && s->request.type == c3::Type::Hello &&
      frame.type == c3::Type::HelloReply && frame.id == s->request.id;
    if (!isHello && (!s->online || frame.session != s->session)) { ++s->stats.ignored; return; }
    s->lastRx = millis();
    if (frame.type == c3::Type::Pong || frame.type == c3::Type::Ack) return;
    if (!isHello && (frame.type != c3::Type::Response || !s->pending || frame.id != s->request.id || frame.opcode != s->request.opcode)) {
      ++s->stats.ignored; return;
    }
    copyFrame(s->response, frame); s->result = frame.status; s->complete = true; s->pending = false;
    if (frame.status == c3::LinkLost) { s->online = false; s->session = 0; }
    ack = !isHello;
#if ESPLINK_CI13XX_RTOS
    xSemaphoreGive(s->done);
#endif
  }
  if (ack) {
    auto& confirm = s->outgoing;
    confirm.type = c3::Type::Ack; confirm.session = frame.session;
    confirm.id = frame.id; confirm.opcode = frame.opcode; confirm.size = 0; confirm.status = c3::Ok;
    transmit(confirm);
  }
}
bool ESPLinkClass::pump() {
  State* s = state_;
  unsigned bytes = 0, batches = 0;
#if ESPLINK_CI13XX_RTOS && ESPLINK_CI13XX_UART_BULK_RX
  if (s->binding.read) {
    while (bytes < ESPLINK_RX_PUMP_BUDGET) {
      size_t capacity = ESPLINK_RX_PUMP_BUDGET - bytes;
      if (capacity > sizeof(s->rxChunk)) capacity = sizeof(s->rxChunk);
      const size_t count = s->binding.read(s->binding.serial, s->rxChunk, capacity);
      if (!count || count > capacity) break;
      ++batches; bytes += static_cast<unsigned>(count);
      for (size_t i = 0; i < count; ++i)
        if (s->decoder.feed(s->rxChunk[i], s->incoming)) receive(s->incoming);
    }
  } else
#endif
  {
    while (bytes < ESPLINK_RX_PUMP_BUDGET && s->binding.serial->available()) {
      const int byte = s->binding.serial->read();
      if (byte < 0) break;
      ++bytes; ++batches;
      if (s->decoder.feed(static_cast<uint8_t>(byte), s->incoming)) receive(s->incoming);
    }
  }
  bool send = false;
  {
    Lock lock(s->mutex);
    s->stats.rxBytes += bytes; s->stats.rxBatches += batches;
    s->stats.crcErrors = s->decoder.crcErrors; s->stats.malformed = s->decoder.malformed;
    s->stats.oversized = s->decoder.overflows;
    const uint32_t now = millis(); auto& frame = s->outgoing;
    if (s->pending && uint32_t(now - s->started) < s->deadlineMs &&
        (!s->sent || uint32_t(now - s->lastTx) >= c3::RetryMs)) {
      copyFrame(frame, s->request); if (s->sent) ++s->stats.retries;
      s->sent = true; s->lastTx = now; send = true;
    } else if (s->online && uint32_t(now - s->lastPing) >= 1000) {
      frame.type = c3::Type::Ping; frame.session = s->session; frame.id = 0;
      frame.opcode = 0; frame.size = 0; frame.status = c3::Ok;
      s->lastPing = now; send = true;
    }
    if (s->online && uint32_t(now - s->lastRx) > 5000) {
      s->online = false; s->session = 0; s->error = c3::LinkLost;
      if (s->pending) {
        s->result = s->sent ? c3::OutcomeUnknown : c3::LinkLost;
        s->complete = true; s->pending = false; s->response.size = 0;
#if ESPLINK_CI13XX_RTOS
        xSemaphoreGive(s->done);
#endif
      }
      send = false;
    }
  }
  if (send) transmit(s->outgoing);
  return bytes == ESPLINK_RX_PUMP_BUDGET;
}
void ESPLinkClass::poll() {
#if !ESPLINK_CI13XX_RTOS
  State* s = state_;
  if (!s || !s->running || s->pumping) return;
  s->pumping = true; pump(); s->pumping = false;
#endif
}
#if ESPLINK_CI13XX_RTOS
void ESPLinkClass::taskEntry(void* context) {
  static_cast<ESPLinkClass*>(context)->run(); vTaskDelete(nullptr);
}
void ESPLinkClass::run() {
  State* s = state_;
  for (;;) {
    { Lock lock(s->mutex); if (!s->running) break; ++s->stats.workerWakeups; }
    // Continuous serial noise must not starve lower-priority application tasks.
    if (pump()) { vTaskDelay(1); continue; }
    uint32_t waitMs = ESPLINK_CI13XX_POLL_MS;
#if ESPLINK_CI13XX_TASK_NOTIFY
    if (s->binding.wait) waitMs = ESPLINK_CI13XX_IDLE_WAIT_MS;
    {
      Lock lock(s->mutex);
      if (!s->running) break;
      const uint32_t now = millis();
      const auto limit = [&waitMs](uint32_t elapsed, uint32_t period) {
        const uint32_t remaining = elapsed < period ? period - elapsed : 0;
        if (remaining < waitMs) waitMs = remaining;
      };
      if (s->pending) {
        const uint32_t elapsed = now - s->started;
        if (elapsed < s->deadlineMs) {
          if (!s->sent) waitMs = 0;
          else limit(now - s->lastTx, c3::RetryMs);
          limit(elapsed, s->deadlineMs);
        } else {
          // Let the waiting caller observe its timeout; no priority-3 spin.
          if (waitMs > ESPLINK_CI13XX_POLL_MS) waitMs = ESPLINK_CI13XX_POLL_MS;
        }
      }
      if (s->online) limit(now - s->lastPing, 1000);
    }
#endif
    // Check pending AFTER DMA/TX calls that can consume task notifications.
    // A submission between this check and wait retains its notification count.
    if (!waitMs) continue;
    if (s->binding.wait) s->binding.wait(s->binding.serial, waitMs);
#if ESPLINK_CI13XX_TASK_NOTIFY
    else (void)ulTaskNotifyTake(pdTRUE, ticks(waitMs));
#else
    else vTaskDelay(ticks(waitMs));
#endif
  }
  xSemaphoreGive(s->stopped);
}
#endif
