// Connect ESP32-C3 to the board's last hardware UART; link baud is 921600.
#include <WiFi.h>

const char *ssid = "YOUR_SSID";
const char *password = "YOUR_PASSWORD";
WiFiClient client;

void setup() {
  Serial.begin(115200);
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
