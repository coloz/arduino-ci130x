#pragma once
#include <Arduino.h>
#include <ESPLink.h>
#include "WiFiTypes.h"
#include "WiFiClient.h"
#include "WiFiServer.h"
#include "WiFiUdp.h"
#include "WiFiClientSecure.h"
#include <vector>

class WiFiClass {
public:
  WiFiClass();
  bool mode(wifi_mode_t mode);
  wifi_mode_t getMode();
  wl_status_t begin(const char *ssid, const char *password = nullptr,
                    int32_t channel = 0, const uint8_t *bssid = nullptr,
                    bool connect = true);
  wl_status_t begin(const String &ssid, const String &password) { return begin(ssid.c_str(), password.c_str()); }
  wl_status_t begin();
  bool disconnect(bool wifiOff = false, bool eraseCredentials = false);
  bool reconnect();
  bool setAutoReconnect(bool enabled);
  bool getAutoReconnect() const { return _autoReconnect; }
  wl_status_t status();
  bool isConnected() { return status() == WL_CONNECTED; }
  wl_status_t waitForConnectResult(unsigned long timeoutMs = 15000);
  bool config(IPAddress local, IPAddress gateway, IPAddress subnet,
              IPAddress dns1 = IPAddress(), IPAddress dns2 = IPAddress());
  bool setDNS(IPAddress dns1, IPAddress dns2 = IPAddress());
  bool setHostname(const char *hostname);
  const char *getHostname();
  IPAddress localIP();
  IPAddress gatewayIP();
  IPAddress subnetMask();
  IPAddress dnsIP(uint8_t index = 0);
  IPAddress localIPv6();
  String SSID();
  String BSSIDstr();
  uint8_t *BSSID();
  int32_t RSSI();
  uint8_t channel();
  String macAddress();
  uint8_t *macAddress(uint8_t *mac);
  bool softAP(const char *ssid, const char *password = nullptr, int channel = 1,
              int hidden = 0, int maxConnection = 4, bool ftmResponder = false);
  bool softAPConfig(IPAddress local, IPAddress gateway, IPAddress subnet);
  bool softAPdisconnect(bool wifiOff = false);
  IPAddress softAPIP();
  String softAPSSID();
  String softAPmacAddress();
  uint8_t *softAPmacAddress(uint8_t *mac);
  uint8_t softAPgetStationNum();
  int16_t scanNetworks(bool async = false, bool showHidden = false,
                       bool passive = false, uint32_t maxMsPerChannel = 300,
                       uint8_t channel = 0);
  int16_t scanComplete();
  void scanDelete();
  String SSID(uint8_t index) const;
  int32_t RSSI(uint8_t index) const;
  uint8_t *BSSID(uint8_t index);
  String BSSIDstr(uint8_t index) const;
  uint8_t channel(uint8_t index) const;
  wifi_auth_mode_t encryptionType(uint8_t index) const;
  bool getNetworkInfo(uint8_t index, String &ssid, uint8_t &auth,
                      int32_t &rssi, uint8_t *&bssid, int32_t &channel);
  int hostByName(const char *host, IPAddress &result, uint32_t timeoutMs = 13000);
  bool configTime(int32_t gmtOffsetSeconds, int32_t daylightOffsetSeconds,
                  const char *server1, const char *server2 = "");
  uint64_t getTime();
  bool setSleep(bool enabled);
  bool getSleep();
  bool setTxPower(wifi_power_t power);
  wifi_power_t getTxPower();
  const char *firmwareVersion();
  uint16_t reasonCode();
  int32_t lastError() const { return _error; }
  wifi_event_id_t onEvent(WiFiEventCb callback, WiFiEvent_t event = ARDUINO_EVENT_MAX);
  wifi_event_id_t onEvent(WiFiEventFuncCb callback, WiFiEvent_t event = ARDUINO_EVENT_MAX);
  void removeEvent(wifi_event_id_t id);
  void poll();
  void end();

private:
  struct ScanResult { String ssid; int32_t rssi; uint8_t bssid[6], channel, auth; };
  struct Handler { wifi_event_id_t id; WiFiEvent_t filter; WiFiEventCb simple; WiFiEventFuncCb detailed; };
  std::vector<ScanResult> _scan;
  std::vector<Handler> _handlers;
  String _ssid, _password, _hostname, _apSSID, _firmware;
  IPAddress _ip, _gateway, _subnet, _dns[2], _ip6, _apIP;
  uint8_t _mac[6], _bssid[6], _apMac[6], _channel, _stations;
  wifi_mode_t _mode;
  wl_status_t _status, _eventStatus;
  wifi_mode_t _eventMode;
  uint8_t _eventStations;
  int16_t _eventScanState;
  int32_t _error, _rssi;
  uint16_t _reason;
  uint32_t _session, _lastStatus, _nextHandler;
  int16_t _scanState;
  bool _autoReconnect, _sleep, _polling;
  wifi_power_t _power;
  bool ensure();
  bool refresh(bool force = false);
  void dispatch(WiFiEvent_t event, const WiFiEventInfo_t &info);
  bool refreshAP();
};
extern WiFiClass WiFi;
