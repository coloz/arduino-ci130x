#pragma once
#include <Arduino.h>
#include <Udp.h>
#include "WiFiSharedPtr.h"
struct ESPWiFiDatagram;
class WiFiUDP : public UDP {
public:
  WiFiUDP();
  virtual ~WiFiUDP();
  uint8_t begin(uint16_t port) override;
  uint8_t begin(IPAddress address, uint16_t port);
  uint8_t beginMulticast(IPAddress address, uint16_t port) override;
  void stop() override;
  int beginPacket(IPAddress address, uint16_t port) override;
  int beginPacket(const char *host, uint16_t port) override;
  int endPacket() override;
  size_t write(uint8_t data) override;
  size_t write(const uint8_t *data, size_t length) override;
  using Print::write;
  int parsePacket() override;
  int available() override;
  int read() override;
  int read(uint8_t *buffer, size_t length) override;
  int read(char *buffer, size_t length) override { return read((uint8_t *)buffer, length); }
  int peek() override;
  void flush() override;
  IPAddress remoteIP() override;
  uint16_t remotePort() override;
  uint16_t localPort() const;
  int32_t lastError() const;
private:
  espwifi::SharedPtr<ESPWiFiDatagram> _state;
  mutable int32_t _error;
  uint8_t open(IPAddress address, uint16_t port, bool multicast);
  bool valid() const;
};
