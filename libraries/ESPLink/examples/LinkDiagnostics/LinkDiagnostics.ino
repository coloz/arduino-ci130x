// Default: last hardware UART at 921600 baud; flash the matching C3 image.
#include <ESPLink.h>

#if defined(ARDUINO_ARCH_CI13XX)
#if defined(ESPLINK_DEFAULT_SERIAL)
auto &diagnosticUart = ESPLINK_DEFAULT_SERIAL;
#else
auto &diagnosticUart = Serial2; // CI1306 default: TX=PB1, RX=PB2.
#endif
#endif

uint32_t passed = 0, failed = 0, lastReportMs = 0;
uint32_t windowRequests = 0, windowMaxUs = 0;
uint64_t windowPayloadBytes = 0, windowRequestUs = 0;
// Static storage keeps maximum-frame tests off the Arduino task stack.
uint8_t pattern[c3::MaxPayload], reply[c3::MaxPayload];

void setup() {
  Serial.begin(115200);
  if (!ESPLink.begin()) {
    Serial.println("ESPLink handshake failed: check UART pins, shared GND and baud");
    Serial.println(ESPLink.lastError());
    return;
  }
  Serial.println(ESPLink.capabilities().firmwareVersion);
  Serial.print("baud="); Serial.print(ESPLINK_DEFAULT_BAUD);
  Serial.print(" crcTable="); Serial.print(C3_CRC32C_USE_TABLE);
#if defined(ARDUINO_ARCH_CI13XX)
  Serial.print(" rtos="); Serial.print(ESPLINK_CI13XX_RTOS);
  Serial.print(" bulkRx="); Serial.print(ESPLINK_CI13XX_UART_BULK_RX);
  Serial.print(" taskNotify="); Serial.print(ESPLINK_CI13XX_TASK_NOTIFY);
  Serial.print(" txDMA="); Serial.print(diagnosticUart.txDMAEnabled());
#endif
  Serial.println();
  Serial.println("payload_Bps counts successful Echo TX+RX; avg_us/max_us cover all requests in this window");
  for (size_t i = 0; i < sizeof(pattern); ++i) pattern[i] = uint8_t(i);
  lastReportMs = millis();
}

void loop() {
  ESPLink.poll();
  if (!ESPLink.ready()) { delay(100); return; }
  size_t size = sizeof(reply);
  const uint32_t startedUs = micros();
  const int32_t result = ESPLink.request(c3::Opcode::Echo, pattern, sizeof(pattern),
                                        reply, size, 2000);
  // Unsigned subtraction also handles one micros() rollover during a request.
  const uint32_t requestUs = uint32_t(micros() - startedUs);
  windowRequestUs += uint64_t(requestUs);
  ++windowRequests;
  if (requestUs > windowMaxUs) windowMaxUs = requestUs;
  if (result == c3::Ok && size == sizeof(pattern) && !memcmp(pattern, reply, size)) {
    ++passed;
    windowPayloadBytes += uint64_t(sizeof(pattern)) + uint64_t(size);
  } else {
    ++failed; Serial.print("Echo error: "); Serial.println(result);
  }
  ++pattern[0];
  const uint32_t nowMs = millis();
  const uint32_t windowMs = uint32_t(nowMs - lastReportMs);
  if (windowMs >= 1000) {
    const auto stats = ESPLink.stats();
    const uint32_t payloadBps = uint32_t(windowPayloadBytes * 1000ULL / windowMs);
    const uint32_t averageUs = windowRequests ? uint32_t(windowRequestUs / windowRequests) : 0;
    Serial.print("passed="); Serial.print(passed);
    Serial.print(" failed="); Serial.print(failed);
    Serial.print(" payload_Bps="); Serial.print(payloadBps);
    Serial.print(" avg_us="); Serial.print(averageUs);
    Serial.print(" max_us="); Serial.print(windowMaxUs);
    Serial.print(" rxBytes="); Serial.print(stats.rxBytes);
    Serial.print(" rxBatches="); Serial.print(stats.rxBatches);
    Serial.print(" workerWakeups="); Serial.print(stats.workerWakeups);
    Serial.print(" crc="); Serial.print(stats.crcErrors);
    Serial.print(" retries="); Serial.print(stats.retries);
    Serial.print(" timeouts="); Serial.print(stats.timeouts);
#if defined(ARDUINO_ARCH_CI13XX)
    const auto errors = diagnosticUart.errorCounts();
    Serial.print(" txDMA="); Serial.print(diagnosticUart.txDMAEnabled());
    Serial.print(" uartOverrun="); Serial.print(errors.hardwareOverrun);
    Serial.print(" rxOverflow="); Serial.print(errors.bufferOverflow);
#endif
    Serial.println();
    // The next wall-time window includes this report's printing and delay(1).
    lastReportMs = nowMs;
    windowPayloadBytes = 0; windowRequestUs = 0;
    windowRequests = 0; windowMaxUs = 0;
  }
  delay(1);
}
