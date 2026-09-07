// SPDX-License-Identifier: MIT
#pragma once
#include <Arduino.h>
#include "C3Protocol.h"
#include "ESPLinkConfig.h"

struct ESPLinkCapabilities {
  uint32_t features = 0, bootID = 0;
  uint16_t maxPayload = 0, maxHciPacket = 0;
  uint8_t maxSockets = 0, maxBLEConnections = 0;
  char firmwareVersion[40] = {};
};
struct ESPLinkStats {
  uint32_t transmitted = 0, received = 0, retries = 0, timeouts = 0;
  uint32_t crcErrors = 0, malformed = 0, oversized = 0, ignored = 0;
  uint32_t sessionChanges = 0;
  uint32_t rxBytes = 0, rxBatches = 0, workerWakeups = 0;
};

// Shared transport for Arduino boards with an external ESP32-C3.
// begin() selects the last hardware UART by default. Generic ports use
// cooperative poll(); the CI13XX HardwareSerial backend has an RTOS worker.
// The object and selected Stream must outlive all use. State is retained after
// end(); destruction releases it after all callers have stopped. Do not destroy
// from a callback or ISR. Generic access is single-task; no interrupt calls.
class ESPLinkClass {
 public:
  ESPLinkClass() = default;
  ~ESPLinkClass();
  ESPLinkClass(const ESPLinkClass&) = delete;
  ESPLinkClass& operator=(const ESPLinkClass&) = delete;

  // Borrow a preconfigured Stream. Does not call its begin()/end().
  bool begin(Stream& serial, uint32_t timeoutMs = 10000);
  // Start/manage an Arduino serial object with begin(baud) and end().
  template<class SerialT>
  bool begin(SerialT& serial, uint32_t baud, uint32_t timeoutMs = 10000) {
    Binding binding;
    binding.serial = &serial; binding.baud = baud;
    binding.start = [](Stream* port, uint32_t speed, uint8_t*, size_t) -> bool {
      static_cast<SerialT*>(port)->begin(speed); return true;
    };
    binding.stop = [](Stream* port) { static_cast<SerialT*>(port)->end(); };
    return beginBinding(binding, timeoutMs);
  }
#if ESPLINK_CI13XX_RTOS
  // Optional optimized backend; never required by another Arduino core.
  bool begin(HardwareSerial& serial, uint32_t baud, uint32_t timeoutMs = 10000);
#endif
  // Reuse the previous binding; otherwise select the last hardware UART at
  // ESPLINK_DEFAULT_BAUD (921600). Never guess an unknown core's USB Serial.
  bool begin();
  void end();
  void poll();
  bool ready() const;
  bool connected() const { return ready(); }
  uint32_t session() const;
  int32_t lastError() const;
  ESPLinkCapabilities capabilities() const;
  ESPLinkStats stats() const;
  int32_t request(uint16_t opcode, const uint8_t* data, size_t size,
                  uint8_t* response, size_t& responseSize, uint32_t timeoutMs = 5000);
  int32_t request(c3::Opcode opcode, const uint8_t* data, size_t size,
                  uint8_t* response, size_t& responseSize, uint32_t timeoutMs = 5000) {
    return request(static_cast<uint16_t>(opcode), data, size, response, responseSize, timeoutMs);
  }
  bool ping(uint32_t timeoutMs = 1000);
 private:
  struct Binding {
    Stream* serial = nullptr;
    uint32_t baud = 0;
    bool (*start)(Stream*, uint32_t, uint8_t*, size_t) = nullptr;
    void (*stop)(Stream*) = nullptr;
    size_t (*write)(Stream*, const uint8_t*, size_t, uint32_t) = nullptr;
    void (*wait)(Stream*, uint32_t) = nullptr;
#if ESPLINK_CI13XX_RTOS && ESPLINK_CI13XX_UART_BULK_RX
    size_t (*read)(Stream*, uint8_t*, size_t) = nullptr;
#endif
    bool operator==(const Binding& b) const {
      return serial == b.serial && baud == b.baud && start == b.start &&
             stop == b.stop && write == b.write && wait == b.wait
#if ESPLINK_CI13XX_RTOS && ESPLINK_CI13XX_UART_BULK_RX
             && read == b.read
#endif
             ;
    }
  };
  struct State;
  State* state_ = nullptr;
  Binding remembered_;
  bool beginBinding(const Binding&, uint32_t timeoutMs);
  static void taskEntry(void* context);
  void run();
  bool pump();
  bool transmit(const c3::Frame& frame);
  void receive(const c3::Frame& frame);
  int32_t transact(c3::Frame& frame, uint8_t* response, size_t& responseSize,
                   uint32_t timeoutMs);
};
extern ESPLinkClass ESPLink;
