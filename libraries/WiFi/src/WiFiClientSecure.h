#pragma once
#include "WiFiClient.h"
struct ESPWiFiCredentials;

class WiFiClientSecure : public WiFiClient {
public:
  WiFiClientSecure();
  ~WiFiClientSecure() override;
  int connect(IPAddress ip, uint16_t port) override;
  int connect(const char *host, uint16_t port) override;
  int connect(IPAddress ip, uint16_t port, int32_t timeoutMs) override;
  int connect(const char *host, uint16_t port, int32_t timeoutMs) override;
  // set* retains the PEM pointer until connect(); load* owns a bounded copy.
  void setCACert(const char *pem);
  void setCertificate(const char *pem);
  void setPrivateKey(const char *pem);
  bool loadCACert(Stream &stream, size_t length);
  bool loadCertificate(Stream &stream, size_t length);
  bool loadPrivateKey(Stream &stream, size_t length);
  void setInsecure();
  void useBuiltinCACertBundle();
  void setHandshakeTimeout(unsigned long seconds);
  int lastError(char *buffer, size_t length) const;
  using WiFiClient::lastError;

private:
  espwifi::SharedPtr<ESPWiFiCredentials> _credentials;
  bool _insecure;
  uint32_t _handshakeTimeout;
  bool loadPEM(Stream &stream, size_t length, uint8_t kind);
};
using WiFiSSLClient = WiFiClientSecure;
