// Arduino host + ESP32-C3 over TX/RX/GND, last hardware UART at 921600 baud.
// Keep the console UART separate from the C3 link.
// Install ArduinoMqttClient 0.1.8 (or a compatible version) from Library Manager.
// MQTT and BLE application callbacks execute on this Arduino host.
#include <WiFi.h>
#include <ArduinoMqttClient.h>
#include <BLE.h>

WiFiClient network;
MqttClient mqtt(network);
BLEService service("19B10000-E8F2-537E-4F6C-D104768A1214");
BLEByteCharacteristic output("19B10001-E8F2-537E-4F6C-D104768A1214",
                             BLERead | BLEWrite | BLENotify);
bool bleStarted = false;
bool valuePending = false;
byte pendingValue = 0;

void onMqttMessage(int messageSize) {
  // The broker sends a single ASCII digit to esplink/output.
  if (messageSize == 1 && mqtt.available()) {
    const int c = mqtt.read();
    if (c >= '0' && c <= '9') { pendingValue = byte(c - '0'); valuePending = true; }
  }
  while (mqtt.available()) mqtt.read();
}

void setup() {
  Serial.begin(115200);
  WiFi.begin("YOUR_SSID", "YOUR_PASSWORD");
  if (WiFi.waitForConnectResult(20000) != WL_CONNECTED) return;
  mqtt.setId("arduino-esplink-mqtt-ble");
  mqtt.onMessage(onMqttMessage);
  // Establish the synchronous network connection before starting the BLE Host.
  // For TLS use WiFiClientSecure and configure CA / valid C3 time first.
  if (!mqtt.connect("192.168.1.10", 1883)) return;
  mqtt.subscribe("esplink/output");

  if (!BLE.begin()) return;
  service.addCharacteristic(output);
  BLE.addService(service);
  BLE.setLocalName("ESPLink-MQTT-BLE");
  BLE.setAdvertisedService(service);
  output.writeValue(byte(0));
  bleStarted = BLE.advertise();
}

void loop() {
  WiFi.poll();
  mqtt.poll(); // Invokes onMqttMessage on this Arduino host.
  if (!bleStarted) { delay(10); return; }
  BLE.poll();
  if (!BLEESPLink.healthy()) {
    Serial.println("BLE/link lost; restore ESPLink and restart BLE before reconnecting");
    BLE.end(); bleStarted = false; return;
  }
  if (valuePending) {
    valuePending = false;
    output.writeValue(pendingValue);
    Serial.println("MQTT command handled on this Arduino host");
  }
  if (output.written() && mqtt.connected()) {
    mqtt.beginMessage("esplink/ble-value");
    mqtt.print(output.value());
    mqtt.endMessage();
  }
  delay(1);
}
