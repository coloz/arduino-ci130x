// Connect ESP32-C3 to the board's last hardware UART; link baud is 921600.
#include <WiFi.h>
WiFiUDP udp;
void setup() {
  Serial.begin(115200);
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
