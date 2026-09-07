// ESP32-C3 uses the board's last hardware UART at 921600 baud.
// Keep the console UART separate from the C3 link.
#include <WiFi.h>

WiFiClient client;

void onWiFiEvent(WiFiEvent_t event) {
  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    Serial.print("IP: "); Serial.println(WiFi.localIP());
  }
}

void setup() {
  Serial.begin(115200);
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
