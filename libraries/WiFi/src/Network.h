#pragma once
#include "WiFi.h"
#include "NetworkClient.h"
#include "NetworkServer.h"
#include "NetworkUdp.h"
#include "NetworkClientSecure.h"
// This manager controls the shared serial coprocessor, with STA/AP interfaces.
class NetworkClass {
public:
  bool begin() { return ESPLink.ready() || ESPLink.begin(); }
  void end() { WiFi.end(); }
  int hostByName(const char *host, IPAddress &ip) { return WiFi.hostByName(host, ip); }
  wifi_event_id_t onEvent(WiFiEventCb cb, WiFiEvent_t event = ARDUINO_EVENT_MAX) { return WiFi.onEvent(cb, event); }
  wifi_event_id_t onEvent(WiFiEventFuncCb cb, WiFiEvent_t event = ARDUINO_EVENT_MAX) { return WiFi.onEvent(cb, event); }
  void removeEvent(wifi_event_id_t id) { WiFi.removeEvent(id); }
};
extern NetworkClass Network;
