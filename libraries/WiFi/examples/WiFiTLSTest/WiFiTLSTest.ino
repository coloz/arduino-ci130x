// Connect ESP32-C3 to the board's last hardware UART; link baud is 921600.
#include <WiFi.h>

WiFiClientSecure client;
void setup() {
  Serial.begin(115200);
  WiFi.begin("YOUR_SSID", "YOUR_PASSWORD");
  if (WiFi.waitForConnectResult(20000) != WL_CONNECTED) return;
  WiFi.configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
  const uint32_t started = millis();
  while (WiFi.getTime() < 1700000000ULL && millis() - started < 15000) delay(100);
  if (WiFi.getTime() < 1700000000ULL) {
    Serial.println("C3 time sync failed; certificate validation needs a valid clock");
    return;
  }
  // C3 firmware owns the CA bundle and performs certificate and hostname checks.
  client.useBuiltinCACertBundle();
  client.setHandshakeTimeout(15);
  if (!client.connect("example.com", 443)) {
    Serial.print("TLS failed: "); Serial.println((long)client.lastError());
    return;
  }
  client.print("GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n");
}
void loop() {
  WiFi.poll();
  uint8_t data[128];
  if (client.available()) {
    int count = client.read(data, sizeof(data));
    if (count > 0) Serial.write(data, count);
  }
  delay(1);
}
