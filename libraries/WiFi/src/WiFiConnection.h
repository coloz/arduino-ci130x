#pragma once
#include "WiFiTransport.h"

struct ESPWiFiConnection {
  uint32_t handle, session;
  uint32_t certificates[3] = {};
  uint8_t rx[768];
  size_t head = 0, tail = 0;
  bool open = true, noDelay = false;
  int32_t error = c3::Ok;
  IPAddress peer, local;
  uint16_t peerPort = 0, localPort = 0;
  uint32_t checkedAt = 0, remoteAvailable = 0;
  ESPWiFiConnection(uint32_t h, uint32_t s) : handle(h), session(s) {}
  void release() {
    espwifi::close(handle, session); handle = 0; open = false; head = tail = 0;
    if (espwifi::current(session)) {
      for (auto &cert : certificates) if (cert) {
        uint8_t b[4]; c3::put32(b, cert);
        espwifi::command(c3::Opcode::CertDelete, b, sizeof(b), 1500); cert = 0;
      }
    }
  }
  ~ESPWiFiConnection() { release(); }
  size_t buffered() const { return tail - head; }
  bool valid() {
    if (!espwifi::current(session)) {
      handle = 0; head = tail = 0; open = false; error = c3::StaleHandle;
    }
    if (!handle && error == c3::Ok) error = c3::NotConnected;
    return handle != 0;
  }
};
