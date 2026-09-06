#pragma once

#include "Stream.h"
#include "IPAddress.h"

// Transport-independent Arduino datagram interface.
class UDP : public Stream {
public:
  virtual uint8_t begin(uint16_t port) = 0;
  virtual uint8_t beginMulticast(IPAddress address, uint16_t port) = 0;
  virtual void stop() = 0;
  virtual int beginPacket(IPAddress address, uint16_t port) = 0;
  virtual int beginPacket(const char *host, uint16_t port) = 0;
  virtual int endPacket() = 0;
  virtual size_t write(uint8_t data) = 0;
  virtual size_t write(const uint8_t *data, size_t length) = 0;
  virtual int parsePacket() = 0;
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int read(uint8_t *data, size_t length) = 0;
  virtual int read(char *data, size_t length) {
    return read(reinterpret_cast<uint8_t *>(data), length);
  }
  virtual int peek() = 0;
  virtual void flush() = 0;
  virtual IPAddress remoteIP() = 0;
  virtual uint16_t remotePort() = 0;
};
