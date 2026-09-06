#pragma once
#include <Server.h>
#include "WiFiClient.h"
struct ESPWiFiListener;
class WiFiServer : public Server {
public:
  explicit WiFiServer(uint16_t port = 80, uint8_t maxClients = 4);
  WiFiServer(IPAddress address, uint16_t port, uint8_t maxClients = 4);
  virtual ~WiFiServer();
  void begin() override;
  void begin(uint16_t port);
  WiFiClient available();
  WiFiClient accept();
  void end();
  void stop() { end(); }
  operator bool() const;
  bool setNoDelay(bool enabled);
  bool getNoDelay() const { return _noDelay; }
  size_t write(uint8_t byte) override;
  size_t write(const uint8_t *buffer, size_t length) override;
  using Print::write;
  int32_t lastError() const { return _error; }
private:
  IPAddress _address;
  uint16_t _port;
  uint8_t _maxClients;
  bool _noDelay;
  int32_t _error;
  espwifi::SharedPtr<ESPWiFiListener> _listener;
};
