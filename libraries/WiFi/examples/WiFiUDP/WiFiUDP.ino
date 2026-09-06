// CI1306 wiring example: Serial2 is the dedicated C3 UART.
// For STM32 or another Arduino core see PortableWiFi; select your own UART.
#include <WiFi.h>
WiFiUDP udp;
void setup() {
  Serial.begin(115200);
  if (!ESPLink.begin(Serial2, 115200)) return;
  WiFi.begin("YOUR_SSID", "YOUR_PASSWORD");
  if (WiFi.waitForConnectResult(20000) == WL_CONNECTED) udp.begin(9000);
}
void loop() {
  WiFi.poll();
  int packet = udp.parsePacket();
  if (packet > 0) {
    IPAddress sender = udp.remoteIP();
    uint16_t port = udp.remotePort();
    uint8_t block[128];
    int count = udp.read(block, sizeof(block));
    // Echo the first 128 bytes. flush discards this datagram's unread remainder.
    udp.flush();
    if (count > 0 && udp.beginPacket(sender, port)) {
      udp.write(block, count);
      udp.endPacket();
    }
  }
  delay(1);
}
