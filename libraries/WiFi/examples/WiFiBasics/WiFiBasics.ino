// CI1306 wiring example: Serial2 is the dedicated C3 UART.
// For STM32 or another Arduino core see PortableWiFi; select your own UART.
#include <WiFi.h>

const char *ssid = "YOUR_SSID";
const char *password = "YOUR_PASSWORD";
WiFiClient client;

void setup() {
  Serial.begin(115200);
  // Serial2 is reserved for the ESP32-C3 link; do not print application logs to it.
  if (!ESPLink.begin(Serial2, 115200)) {
    Serial.println("C3 link unavailable");
    return;
  }
  WiFi.begin(ssid, password);
  if (WiFi.waitForConnectResult(20000) != WL_CONNECTED) {
    Serial.println("WiFi connection failed");
    return;
  }
  Serial.println(WiFi.localIP());
  if (client.connect("example.com", 80)) {
    client.print("GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n");
  }
}

void loop() {
  WiFi.poll();
  uint8_t block[128];
  if (client.available()) {
    int count = client.read(block, sizeof(block));
    if (count > 0) Serial.write(block, count);
  }
  delay(1);
}
