// CI1306 wiring example: Serial2 is the dedicated C3 UART.
// For STM32 or another Arduino core see PortableWiFi; select your own UART.
// Install ArduinoMqttClient 0.1.8 (or a compatible version) from Library Manager.
// MQTT and BLE application callbacks execute on CI1306, in this Arduino task.
#include <WiFi.h>
#include <ArduinoMqttClient.h>
#include <BLE.h>
#include <BLEESPLink.h>

WiFiClient network;
MqttClient mqtt(network);
BLEService service("19B10000-E8F2-537E-4F6C-D104768A1214");
BLEByteCharacteristic output("19B10001-E8F2-537E-4F6C-D104768A1214",
                             BLERead | BLEWrite | BLENotify);
bool bleStarted = false;
bool valuePending = false;
byte pendingValue = 0;

void onMqttMessage(int messageSize) {
  // The broker sends a single ASCII digit to ci1306/output.
  if (messageSize == 1 && mqtt.available()) {
    const int c = mqtt.read();
    if (c >= '0' && c <= '9') { pendingValue = byte(c - '0'); valuePending = true; }
  }
  while (mqtt.available()) mqtt.read();
}

void setup() {
  Serial.begin(115200);
  if (!ESPLink.begin(Serial2, 115200)) return;
  WiFi.begin("YOUR_SSID", "YOUR_PASSWORD");
  if (WiFi.waitForConnectResult(20000) != WL_CONNECTED) return;
  mqtt.setId("ci1306-mqtt-ble");
  mqtt.onMessage(onMqttMessage);
  // Establish the synchronous network connection before starting the BLE Host.
  // For TLS use WiFiClientSecure and configure CA / valid C3 time first.
  if (!mqtt.connect("192.168.1.10", 1883)) return;
  mqtt.subscribe("ci1306/output");

  if (!BLE.begin()) return;
  service.addCharacteristic(output);
  BLE.addService(service);
  BLE.setLocalName("CI1306-MQTT-BLE");
  BLE.setAdvertisedService(service);
  output.writeValue(byte(0));
  bleStarted = BLE.advertise();
}

void loop() {
  WiFi.poll();
  mqtt.poll(); // Invokes onMqttMessage on CI1306.
  if (!bleStarted) { delay(10); return; }
  BLE.poll();
  if (!BLEESPLink.healthy()) {
    Serial.println("BLE/link lost; restore ESPLink and restart BLE before reconnecting");
    BLE.end(); bleStarted = false; return;
  }
  if (valuePending) {
    valuePending = false;
    output.writeValue(pendingValue);
    Serial.println("MQTT command handled on CI1306");
  }
  if (output.written() && mqtt.connected()) {
    mqtt.beginMessage("ci1306/ble-value");
    mqtt.print(output.value());
    mqtt.endMessage();
  }
  delay(1);
}
