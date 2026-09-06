#pragma once
#include <Arduino.h>
#include <Client.h>
#include "WiFiSharedPtr.h"

struct ESPWiFiConnection;
class WiFiClient : public Client {
public:
  WiFiClient();
  virtual ~WiFiClient();
  WiFiClient(const WiFiClient &) = default;
  WiFiClient &operator=(const WiFiClient &) = default;
  int connect(IPAddress ip, uint16_t port) override;
  int connect(const char *host, uint16_t port) override;
  virtual int connect(IPAddress ip, uint16_t port, int32_t timeoutMs);
  virtual int connect(const char *host, uint16_t port, int32_t timeoutMs);
  size_t write(uint8_t data) override;
  size_t write(const uint8_t *data, size_t length) override;
  using Print::write;
  int available() override;
  int availableForWrite();
  int read() override;
  int read(uint8_t *data, size_t length) override;
  int read(char *data, size_t length) { return read((uint8_t *)data, length); }
  int peek() override;
  void flush() override;
  void clear();
  void stop() override;
  uint8_t connected() override;
  operator bool() override;
  bool operator==(const WiFiClient &other) const;
  bool operator!=(const WiFiClient &other) const { return !(*this == other); }
  void setConnectionTimeout(uint32_t milliseconds) { _connectTimeout = milliseconds; }
  int setNoDelay(bool enabled);
  bool getNoDelay();
  bool setKeepAlive(uint32_t idleSeconds, uint32_t intervalSeconds, uint32_t count);
  IPAddress remoteIP() const;
  uint16_t remotePort() const;
  IPAddress localIP() const;
  uint16_t localPort() const;
  int32_t lastError() const;

protected:
  espwifi::SharedPtr<ESPWiFiConnection> _state;
  uint32_t _connectTimeout;
  mutable int32_t _error;
  int connectTransport(const char *host, uint16_t port, uint32_t timeoutMs,
                       uint8_t kind, uint32_t ca, uint32_t cert, uint32_t key,
                       bool insecure, uint32_t handshakeTimeoutMs = 0);
  bool valid() const;
  int fetch();
  bool waitForData(unsigned long timeout); // Optional core Stream extension.
  explicit WiFiClient(uint32_t handle, uint32_t session);
  friend class WiFiServer;
};
