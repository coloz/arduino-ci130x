// CI1306 wiring example: Serial2 is the dedicated C3 UART.
// For STM32 or another Arduino core see PortableWiFi; select your own UART.
// Install ArduinoMqttClient from the Arduino Library Manager for this example.
#include <WiFi.h>
#include <ArduinoMqttClient.h>
WiFiClient transport;
MqttClient mqtt(transport);
void setup() {
  Serial.begin(115200);
  if (!ESPLink.begin(Serial2, 115200)) return;
  WiFi.begin("YOUR_SSID", "YOUR_PASSWORD");
  if (WiFi.waitForConnectResult(20000) != WL_CONNECTED) return;
  mqtt.setId("ci1306-example");
  // Replace with your broker; use WiFiClientSecure transport for TLS.
  if (!mqtt.connect("192.168.1.10", 1883)) return;
  mqtt.beginMessage("ci1306/status");
  mqtt.print("online");
  mqtt.endMessage();
  mqtt.subscribe("ci1306/commands");
}
void loop() {
  WiFi.poll();
  mqtt.poll();
  if (mqtt.parseMessage()) {
    while (mqtt.available()) Serial.write((uint8_t)mqtt.read());
  }
  delay(1);
}
