// Arduino host + ESP32-C3 over TX/RX/GND; choose the available UART below.
// Keep the console UART separate. Both link ends must use 115200 baud.
// Install ArduinoMqttClient 0.1.8 (or a compatible version) from Library Manager.
// MQTT and BLE application callbacks execute on this Arduino host.
#include <ESPLink.h>
#include <WiFi.h>
#include <ArduinoMqttClient.h>
#include <BLE.h>
#include <BLEESPLink.h>

#if defined(ARDUINO_ARCH_STM32)
// STM32duino USART1 RX=PA10 / TX=PA9, including NUCLEO-F411RE.
#if __has_include(<Serial.h>)
#include <Serial.h>
Uart LinkSerial(PA10, PA9);  // STM32duino 3.x.
#else
HardwareSerial LinkSerial(PA10, PA9);  // STM32duino 2.x.
#endif
#define LINK_UART LinkSerial
#elif defined(ARDUINO_ARCH_CI13XX)
#define LINK_UART Serial2
#else
// Replace Serial1 if another UART is available on your Arduino board.
#define LINK_UART Serial1
#endif

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
  if (!ESPLink.begin(LINK_UART, 115200)) return;
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
  ESPLink.poll();
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
