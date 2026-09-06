// ESPLink networking with an explicitly selected UART on a 32-bit Arduino board.
// Change the UART/pins below to match your board; keep the console UART separate.
#include <ESPLink.h>
#include <WiFi.h>

#if defined(ARDUINO_ARCH_STM32)
// STM32duino HardwareSerial constructor uses RX, TX (here USART1 on PA10/PA9).
// These pins are an example for boards exposing USART1, including F411/F446.
#if __has_include(<Serial.h>)
Uart ESPSerial(PA10, PA9); // STM32duino 3.x
#else
HardwareSerial ESPSerial(PA10, PA9); // STM32duino 2.x
#endif
#elif defined(ARDUINO_ARCH_CI13XX)
#define ESPSerial Serial2
#else
// Boards such as SAMD and RP2040 commonly expose a separate Serial1 hardware UART.
#define ESPSerial Serial1
#endif

WiFiClient client;

void onWiFiEvent(WiFiEvent_t event) {
  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    Serial.print("IP: "); Serial.println(WiFi.localIP());
  }
}

void setup() {
  Serial.begin(115200);
  // TX -> C3 RX, RX <- C3 TX, shared GND; both ends must use the same baud.
  if (!ESPLink.begin(ESPSerial, 115200)) {
    Serial.println("ESPLink handshake failed");
    return;
  }
  WiFi.onEvent(onWiFiEvent);
  WiFi.begin("YOUR_SSID", "YOUR_PASSWORD");
  if (WiFi.waitForConnectResult(20000) != WL_CONNECTED) return;
  if (client.connect("example.com", 80)) {
    client.print("GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n");
  }
}

void loop() {
  // Portable cooperative mode: services ESPLink and dispatches application events.
  WiFi.poll();
  uint8_t data[128];
  if (client.available()) {
    const int length = client.read(data, sizeof(data));
    if (length > 0) Serial.write(data, length);
  }
  delay(1);
}
