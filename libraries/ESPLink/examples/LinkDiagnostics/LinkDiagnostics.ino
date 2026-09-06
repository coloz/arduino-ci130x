// Set LINK_BAUD to the baud used by the C3 image: 115200 or 921600.
#include <ESPLink.h>

// Choose pins/ports available on your board; keep Serial free for diagnostics.
#if defined(ARDUINO_ARCH_STM32)
// STM32duino NUCLEO-F411RE: USART1 RX=PA10, TX=PA9.
#if __has_include(<Serial.h>)
#include <Serial.h>
Uart LinkSerial(PA10, PA9);  // STM32duino 3.x concrete UART type.
#else
HardwareSerial LinkSerial(PA10, PA9);  // STM32duino 2.x.
#endif
#define LINK_UART LinkSerial
#elif defined(ARDUINO_ARCH_CI13XX)
// CI1306 UART2: RX=PB2, TX=PB1. Keep the concrete type for its fast backend.
#define LINK_UART Serial2
#else
// Replace Serial1 if your Arduino board uses another hardware UART.
#define LINK_UART Serial1
#endif
#ifndef LINK_BAUD
#define LINK_BAUD 115200UL
#endif
static_assert(LINK_BAUD == 115200UL || LINK_BAUD == 921600UL,
              "Select the baud of the matching ESP32-C3 firmware image");
uint32_t passed = 0, failed = 0, lastReportMs = 0;
uint32_t windowRequests = 0, windowMaxUs = 0;
uint64_t windowPayloadBytes = 0, windowRequestUs = 0;
// Static storage keeps maximum-frame tests off the Arduino task stack.
uint8_t pattern[c3::MaxPayload], reply[c3::MaxPayload];

void setup() {
  Serial.begin(115200);
  if (!ESPLink.begin(LINK_UART, LINK_BAUD)) {
    Serial.println("ESPLink handshake failed: check UART pins, shared GND and baud");
    Serial.println(ESPLink.lastError());
    return;
  }
  Serial.println(ESPLink.capabilities().firmwareVersion);
  Serial.print("baud="); Serial.print(LINK_BAUD);
  Serial.print(" crcTable="); Serial.print(C3_CRC32C_USE_TABLE);
#if defined(ARDUINO_ARCH_CI13XX)
  Serial.print(" rtos="); Serial.print(ESPLINK_CI13XX_RTOS);
  Serial.print(" bulkRx="); Serial.print(ESPLINK_CI13XX_UART_BULK_RX);
  Serial.print(" taskNotify="); Serial.print(ESPLINK_CI13XX_TASK_NOTIFY);
  Serial.print(" txDMA="); Serial.print(Serial2.txDMAEnabled());
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
    const auto errors = Serial2.errorCounts();
    Serial.print(" txDMA="); Serial.print(Serial2.txDMAEnabled());
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