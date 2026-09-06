#include "WiFi.h"
#include "Network.h"
#include "WiFiTransport.h"

#ifndef ESPLINK_WIFI_MAX_SCAN_RESULTS
#define ESPLINK_WIFI_MAX_SCAN_RESULTS 32
#endif
WiFiClass WiFi;
NetworkClass Network;
WiFiClass::WiFiClass() : _mac{}, _bssid{}, _apMac{}, _channel(0), _stations(0),
 _mode(WIFI_OFF), _status(WL_NO_SHIELD), _eventStatus(WL_NO_SHIELD), _eventMode(WIFI_OFF), _eventStations(0), _eventScanState(WIFI_SCAN_FAILED), _error(c3::Ok), _rssi(0), _reason(0),
 _session(0), _lastStatus(0), _nextHandler(1), _scanState(WIFI_SCAN_FAILED),
 _autoReconnect(true), _sleep(true), _polling(false), _power(WIFI_POWER_19_5dBm) {}
bool WiFiClass::ensure() {
  if (!ESPLink.ready() && !ESPLink.begin()) { _error = c3::LinkLost; _status = WL_NO_SHIELD; return false; }
  if (_session != ESPLink.session()) {
    _session = ESPLink.session(); _status = WL_DISCONNECTED; _mode = WIFI_OFF;
    _lastStatus = 0; _scan.clear(); _scanState = WIFI_SCAN_FAILED;
    _ip = IPAddress(); _gateway = IPAddress(); _subnet = IPAddress(); _apIP = IPAddress();
  }
  return true;
}
bool WiFiClass::refresh(bool force) {
  if (!ensure()) return false;
  if (!force && _lastStatus && uint32_t(millis() - _lastStatus) < 250) return true;
  uint8_t reply[320]; size_t n = sizeof(reply);
  _error = espwifi::call(c3::Opcode::WifiStatus, nullptr, 0, reply, n, 2000);
  if (_error != c3::Ok) return false;
  c3::Reader r(reply, n); const auto status = static_cast<wl_status_t>(r.u8());
  const auto mode = static_cast<wifi_mode_t>(r.u8()); const int32_t rssi = r.i32();
  const uint8_t channel = r.u8(); uint8_t mac[6], bssid[6];
  r.bytes(mac, 6); r.bytes(bssid, 6); char ssid[33], hostname[65];
  r.string(ssid, sizeof(ssid)); r.string(hostname, sizeof(hostname));
  const auto ip = espwifi::address(r, _error), gateway = espwifi::address(r, _error), subnet = espwifi::address(r, _error);
  const auto dns1 = espwifi::address(r, _error), dns2 = espwifi::address(r, _error);
  if (!r.done()) { _error = c3::ProtocolError; return false; }
  if (_error != c3::Ok) return false;
  _status = status; _mode = mode; _rssi = rssi; _channel = channel;
  memcpy(_mac, mac, 6); memcpy(_bssid, bssid, 6);
  _ssid = ssid; _hostname = hostname; _ip = ip; _gateway = gateway; _subnet = subnet;
  _dns[0] = dns1; _dns[1] = dns2; _lastStatus = millis(); return true;
}
bool WiFiClass::refreshAP() {
  if (!ensure()) return false;
  uint8_t reply[80]; size_t n = sizeof(reply);
  _error = espwifi::call(c3::Opcode::WifiAPStatus, nullptr, 0, reply, n, 2000);
  if (_error != c3::Ok) return false;
  c3::Reader r(reply, n); const uint8_t stations = r.u8(); char ssid[33]; uint8_t mac[6];
  r.string(ssid, sizeof(ssid)); r.bytes(mac, 6); const auto ip = espwifi::address(r, _error);
  if (!r.done()) { _error = c3::ProtocolError; return false; }
  if (_error != c3::Ok) return false;
  _stations = stations; _apSSID = ssid; memcpy(_apMac, mac, 6); _apIP = ip; return true;
}
bool WiFiClass::mode(wifi_mode_t mode) {
  if (mode > WIFI_AP_STA) { _error = c3::InvalidArgument; return false; }
  if (!ensure()) return false;
  uint8_t request = mode;
  _error = espwifi::command(c3::Opcode::WifiMode, &request, 1);
  if (_error == c3::Ok) { _mode = mode; _lastStatus = 0; }
  return _error == c3::Ok;
}
wifi_mode_t WiFiClass::getMode() { refresh(); return _mode; }
wl_status_t WiFiClass::begin(const char *ssid, const char *password, int32_t channel,
 const uint8_t *bssid, bool connect) {
  if (!ssid || !*ssid || strlen(ssid) > 32 || (password && strlen(password) > 64) || channel < 0 || channel > 14) {
    _error = c3::InvalidArgument; return WL_CONNECT_FAILED;
  }
  _ssid = ssid; _password = password ? password : "";
  if (!connect) { _error = c3::Unsupported; return WL_CONNECT_FAILED; }
  if (!ensure()) return WL_NO_SHIELD;
  uint8_t request[112], zero[6] = {}; c3::Writer w(request, sizeof(request));
  w.string(ssid); w.string(password); w.i32(channel); w.u8(bssid != nullptr); w.bytes(bssid ? bssid : zero, 6);
  _error = espwifi::command(c3::Opcode::WifiBegin, request, w.size());
  if (_error != c3::Ok) return WL_CONNECT_FAILED;
  _lastStatus = 0; refresh(true); return _status;
}
wl_status_t WiFiClass::begin() { if (!reconnect()) return WL_CONNECT_FAILED; return status(); }
bool WiFiClass::disconnect(bool off, bool erase) {
  if (!ensure()) return false;
  uint8_t b[2] = {uint8_t(erase), uint8_t(off)};
  _error = espwifi::command(c3::Opcode::WifiDisconnect, b, sizeof(b));
  if (_error == c3::Ok) { _lastStatus = 0; if (erase) _password = ""; }
  return _error == c3::Ok;
}
bool WiFiClass::reconnect() {
  if (!ensure()) return false;
  _error = espwifi::command(c3::Opcode::WifiReconnect, nullptr, 0); _lastStatus = 0;
  return _error == c3::Ok;
}
bool WiFiClass::setAutoReconnect(bool enabled) {
  if (!ensure()) return false;
  uint8_t b = enabled; _error = espwifi::command(c3::Opcode::WifiAutoReconnect, &b, 1);
  if (_error == c3::Ok) _autoReconnect = enabled;
  return _error == c3::Ok;
}
wl_status_t WiFiClass::status() { refresh(); return _status; }
wl_status_t WiFiClass::waitForConnectResult(unsigned long timeout) {
  uint32_t started = millis();
  do {
    poll();
    if (_status == WL_CONNECTED || _status == WL_NO_SHIELD || _status == WL_CONNECT_FAILED) return _status;
    delay(10);
  } while (uint32_t(millis() - started) < timeout);
  _error = c3::Timeout; return _status;
}
bool WiFiClass::config(IPAddress local, IPAddress gateway, IPAddress subnet, IPAddress dns1, IPAddress dns2) {
  if (!ensure()) return false;
  uint8_t b[90]; c3::Writer w(b, sizeof(b));
  espwifi::address(w, local); espwifi::address(w, gateway); espwifi::address(w, subnet);
  espwifi::address(w, dns1); espwifi::address(w, dns2);
  _error = espwifi::command(c3::Opcode::WifiConfig, b, w.size()); _lastStatus = 0;
  return _error == c3::Ok;
}
bool WiFiClass::setDNS(IPAddress dns1, IPAddress dns2) {
  if (!ensure()) return false;
  uint8_t b[36]; c3::Writer w(b, sizeof(b)); espwifi::address(w, dns1); espwifi::address(w, dns2);
  _error = espwifi::command(c3::Opcode::WifiDNS, b, w.size()); _lastStatus = 0;
  return _error == c3::Ok;
}
bool WiFiClass::setHostname(const char *name) {
  if (!name || !*name || strlen(name) > 63) { _error = c3::InvalidArgument; return false; }
  if (!ensure()) return false;
  uint8_t b[65]; c3::Writer w(b, sizeof(b)); w.string(name);
  _error = espwifi::command(c3::Opcode::WifiHostname, b, w.size());
  if (_error == c3::Ok) _hostname = name;
  return _error == c3::Ok;
}
const char *WiFiClass::getHostname() { refresh(); return _hostname.c_str(); }
IPAddress WiFiClass::localIP() { refresh(); return _ip; }
IPAddress WiFiClass::gatewayIP() { refresh(); return _gateway; }
IPAddress WiFiClass::subnetMask() { refresh(); return _subnet; }
IPAddress WiFiClass::dnsIP(uint8_t index) { if (index > 1) { _error = c3::InvalidArgument; return IPAddress(); } refresh(); return _dns[index]; }
IPAddress WiFiClass::localIPv6() { _error = c3::Unsupported; return IPAddress(); }
String WiFiClass::SSID() { refresh(); return _ssid; }
String WiFiClass::BSSIDstr() { refresh(); return espwifi::macString(_bssid); }
uint8_t *WiFiClass::BSSID() { refresh(); return _bssid; }
int32_t WiFiClass::RSSI() { refresh(); return _rssi; }
uint8_t WiFiClass::channel() { refresh(); return _channel; }
String WiFiClass::macAddress() { refresh(); return espwifi::macString(_mac); }
uint8_t *WiFiClass::macAddress(uint8_t *mac) { if (!mac) { _error = c3::InvalidArgument; return nullptr; } refresh(); memcpy(mac, _mac, 6); return mac; }
bool WiFiClass::softAP(const char *ssid, const char *pass, int channel, int hidden, int maxClients, bool ftm) {
  if (ftm) { _error = c3::Unsupported; return false; }
  if (!ssid || !*ssid || strlen(ssid) > 32 || (pass && strlen(pass) > 63) || channel < 1 || channel > 14 || maxClients < 1 || maxClients > 10) {
    _error = c3::InvalidArgument; return false;
  }
  if (!ensure()) return false;
  uint8_t b[104]; c3::Writer w(b, sizeof(b));
  w.string(ssid); w.string(pass); w.u8(channel); w.u8(hidden != 0); w.u8(maxClients);
  _error = espwifi::command(c3::Opcode::WifiAPBegin, b, w.size());
  if (_error == c3::Ok) { _apSSID = ssid; _lastStatus = 0; return true; }
  return false;
}
bool WiFiClass::softAPConfig(IPAddress local, IPAddress gateway, IPAddress subnet) {
  if (!ensure()) return false;
  uint8_t b[54]; c3::Writer w(b, sizeof(b)); espwifi::address(w, local); espwifi::address(w, gateway); espwifi::address(w, subnet);
  _error = espwifi::command(c3::Opcode::WifiAPConfig, b, w.size()); return _error == c3::Ok;
}
bool WiFiClass::softAPdisconnect(bool off) {
  if (!ensure()) return false;
  uint8_t b = off; _error = espwifi::command(c3::Opcode::WifiAPStop, &b, 1); _lastStatus = 0; return _error == c3::Ok;
}
IPAddress WiFiClass::softAPIP() { refreshAP(); return _apIP; }
String WiFiClass::softAPSSID() { refreshAP(); return _apSSID; }
String WiFiClass::softAPmacAddress() { refreshAP(); return espwifi::macString(_apMac); }
uint8_t *WiFiClass::softAPmacAddress(uint8_t *mac) { if (!mac) { _error = c3::InvalidArgument; return nullptr; } refreshAP(); memcpy(mac, _apMac, 6); return mac; }
uint8_t WiFiClass::softAPgetStationNum() { refreshAP(); return _stations; }
int16_t WiFiClass::scanNetworks(bool async, bool hidden, bool passive, uint32_t maxMs, uint8_t channel) {
  if (maxMs > 65535 || channel > 14) { _error = c3::InvalidArgument; return WIFI_SCAN_FAILED; }
  if (!ensure()) return WIFI_SCAN_FAILED;
  uint8_t b[5]; c3::Writer w(b, sizeof(b)); w.u8(hidden); w.u8(passive); w.u16(maxMs); w.u8(channel);
  _error = espwifi::command(c3::Opcode::WifiScanStart, b, w.size());
  if (_error != c3::Ok) return WIFI_SCAN_FAILED;
  _scan.clear(); _scanState = WIFI_SCAN_RUNNING; _eventScanState = WIFI_SCAN_RUNNING;
  if (async) return _scanState;
  const uint32_t start = millis(), timeout = std::min<uint32_t>(120000, 3000 + maxMs * (channel ? 1 : 14));
  while (scanComplete() == WIFI_SCAN_RUNNING && uint32_t(millis() - start) < timeout) delay(20);
  if (_scanState == WIFI_SCAN_RUNNING) { _error = c3::Timeout; return WIFI_SCAN_FAILED; }
  return _scanState;
}
int16_t WiFiClass::scanComplete() {
  if (_scanState != WIFI_SCAN_RUNNING) return _scanState;
  uint8_t reply[100]; size_t n = sizeof(reply);
  _error = espwifi::call(c3::Opcode::WifiScanStatus, nullptr, 0, reply, n);
  if (_error != c3::Ok) return WIFI_SCAN_FAILED;
  c3::Reader r(reply, n); const int32_t count = r.i32();
  if (!r.done() || count < -2) { _error = c3::ProtocolError; return WIFI_SCAN_FAILED; }
  if (count < 0) { _scanState = count; return _scanState; }
  const size_t wanted = std::min<size_t>(count, ESPLINK_WIFI_MAX_SCAN_RESULTS);
  _scan.clear();
  for (size_t i = 0; i < wanted; ++i) {
    uint8_t request[2]; c3::put16(request, i); n = sizeof(reply);
    _error = espwifi::call(c3::Opcode::WifiScanResult, request, sizeof(request), reply, n);
    if (_error != c3::Ok) { _scan.clear(); _scanState = WIFI_SCAN_FAILED; return _scanState; }
    c3::Reader row(reply, n); char ssid[33]; ScanResult entry;
    row.string(ssid, sizeof(ssid)); entry.rssi = row.i32(); entry.auth = row.u8();
    entry.channel = row.u8(); row.bytes(entry.bssid, 6); row.u8();
    if (!row.done()) { _error = c3::ProtocolError; _scan.clear(); _scanState = WIFI_SCAN_FAILED; return _scanState; }
    entry.ssid = ssid; _scan.push_back(entry);
  }
  _scanState = static_cast<int16_t>(_scan.size());
  if (wanted < static_cast<size_t>(count)) _error = c3::BufferTooSmall;
  return _scanState;
}
void WiFiClass::scanDelete() { _scan.clear(); _scanState = WIFI_SCAN_FAILED; if (ESPLink.ready()) _error = espwifi::command(c3::Opcode::WifiScanDelete, nullptr, 0); }
String WiFiClass::SSID(uint8_t i) const { return i < _scan.size() ? _scan[i].ssid : String(); }
int32_t WiFiClass::RSSI(uint8_t i) const { return i < _scan.size() ? _scan[i].rssi : 0; }
uint8_t *WiFiClass::BSSID(uint8_t i) { return i < _scan.size() ? _scan[i].bssid : nullptr; }
String WiFiClass::BSSIDstr(uint8_t i) const { return i < _scan.size() ? espwifi::macString(_scan[i].bssid) : String(); }
uint8_t WiFiClass::channel(uint8_t i) const { return i < _scan.size() ? _scan[i].channel : 0; }
wifi_auth_mode_t WiFiClass::encryptionType(uint8_t i) const { return i < _scan.size() ? static_cast<wifi_auth_mode_t>(_scan[i].auth) : WIFI_AUTH_MAX; }
bool WiFiClass::getNetworkInfo(uint8_t i, String &ssid, uint8_t &auth, int32_t &rssi, uint8_t *&bssid, int32_t &channel) {
  if (i >= _scan.size()) { _error = c3::InvalidArgument; return false; }
  auto &entry = _scan[i]; ssid = entry.ssid; auth = entry.auth; rssi = entry.rssi; bssid = entry.bssid; channel = entry.channel; return true;
}
int WiFiClass::hostByName(const char *host, IPAddress &ip, uint32_t timeout) {
  if (!host || !*host || strlen(host) > 253) { _error = c3::InvalidArgument; return 0; }
  if (ip.fromString(host)) { _error = c3::Ok; return 1; }
  if (strchr(host, ':')) { _error = c3::Unsupported; return 0; }
  if (!ensure()) return 0;
  uint8_t b[256], reply[18]; c3::Writer w(b, sizeof(b)); w.string(host); w.u8(espwifi::dnsFamily(ip));
  size_t n = sizeof(reply); _error = espwifi::call(c3::Opcode::DnsResolve, b, w.size(), reply, n, timeout);
  if (_error != c3::Ok) return 0;
  c3::Reader r(reply, n); const auto value = espwifi::address(r, _error);
  if (!r.done()) { _error = c3::ProtocolError; return 0; }
  if (_error != c3::Ok) return 0;
  ip = value; return 1;
}
bool WiFiClass::configTime(int32_t gmt, int32_t daylight, const char *server1, const char *server2) {
  if (!server1 || !*server1 || strlen(server1) > 253 || (server2 && strlen(server2) > 253)) { _error = c3::InvalidArgument; return false; }
  if (!ensure()) return false;
  uint8_t b[520]; c3::Writer w(b, sizeof(b)); w.i32(gmt); w.i32(daylight); w.string(server1); w.string(server2);
  _error = espwifi::command(c3::Opcode::TimeConfig, b, w.size()); return _error == c3::Ok;
}
uint64_t WiFiClass::getTime() {
  if (!ensure()) return 0;
  uint8_t reply[8]; size_t n = sizeof(reply);
  _error = espwifi::call(c3::Opcode::TimeGet, nullptr, 0, reply, n);
  if (_error != c3::Ok) return 0;
  c3::Reader r(reply, n); uint64_t value = r.u32(); value |= uint64_t(r.u32()) << 32;
  if (!r.done()) { _error = c3::ProtocolError; return 0; } return value;
}
bool WiFiClass::setSleep(bool enabled) {
  if (!ensure()) return false;
  uint8_t b[5]; c3::Writer w(b, sizeof(b)); w.u8(static_cast<uint8_t>(c3::WifiOption::Sleep)); w.i32(enabled);
  _error = espwifi::command(c3::Opcode::WifiSetOption, b, w.size()); if (_error == c3::Ok) _sleep = enabled; return _error == c3::Ok;
}
bool WiFiClass::getSleep() {
  if (!ensure()) return false;
  uint8_t b = static_cast<uint8_t>(c3::WifiOption::Sleep), reply[4]; size_t n = sizeof(reply);
  _error = espwifi::call(c3::Opcode::WifiGetOption, &b, 1, reply, n);
  if (_error != c3::Ok) return false;
  c3::Reader r(reply, n); const int32_t value = r.i32();
  if (!r.done()) { _error = c3::ProtocolError; return false; } _sleep = value != 0; return _sleep;
}
bool WiFiClass::setTxPower(wifi_power_t power) {
  if (!ensure()) return false;
  uint8_t b[5]; c3::Writer w(b, sizeof(b)); w.u8(static_cast<uint8_t>(c3::WifiOption::TxPower)); w.i32(power);
  _error = espwifi::command(c3::Opcode::WifiSetOption, b, w.size()); if (_error == c3::Ok) _power = power; return _error == c3::Ok;
}
wifi_power_t WiFiClass::getTxPower() {
  if (!ensure()) return _power;
  uint8_t b = static_cast<uint8_t>(c3::WifiOption::TxPower), reply[4]; size_t n = sizeof(reply);
  _error = espwifi::call(c3::Opcode::WifiGetOption, &b, 1, reply, n);
  if (_error != c3::Ok) return _power;
  c3::Reader r(reply, n); const int32_t value = r.i32();
  if (!r.done()) { _error = c3::ProtocolError; return _power; } _power = static_cast<wifi_power_t>(value); return _power;
}
const char *WiFiClass::firmwareVersion() {
  if (!ensure()) return "";
  uint8_t reply[128]; size_t n = sizeof(reply); _error = espwifi::call(c3::Opcode::SystemInfo, nullptr, 0, reply, n);
  if (_error != c3::Ok) return "";
  char value[65]; c3::Reader r(reply, n); r.string(value, sizeof(value));
  r.u32(); r.u32(); r.u32(); r.u32();
  if (!r.done()) { _error = c3::ProtocolError; return ""; } _firmware = value; return _firmware.c_str();
}
wifi_event_id_t WiFiClass::onEvent(WiFiEventCb callback, WiFiEvent_t filter) {
  if (!callback) return 0;
  const auto id = _nextHandler++; _handlers.push_back({id, filter, callback, nullptr}); return id;
}
wifi_event_id_t WiFiClass::onEvent(WiFiEventFuncCb callback, WiFiEvent_t filter) {
  if (!callback) return 0;
  const auto id = _nextHandler++; _handlers.push_back({id, filter, nullptr, callback}); return id;
}
void WiFiClass::removeEvent(wifi_event_id_t id) {
  _handlers.erase(std::remove_if(_handlers.begin(), _handlers.end(), [id](const Handler &h){ return h.id == id; }), _handlers.end());
}
void WiFiClass::dispatch(WiFiEvent_t event, const WiFiEventInfo_t &info) {
  const auto handlers = _handlers; // Removal/addition during a callback is safe.
  for (const auto &h : handlers) if (h.filter == ARDUINO_EVENT_MAX || h.filter == event) {
    if (h.simple) h.simple(event); else if (h.detailed) h.detailed(event, info);
  }
}
void WiFiClass::poll() {
  ESPLink.poll();
  if (_polling || !ESPLink.ready()) return;
  const bool due = !_lastStatus || uint32_t(millis() - _lastStatus) >= 250;
  const bool changed = _eventStatus != _status || _eventMode != _mode ||
    _eventStations != _stations || (_eventScanState == WIFI_SCAN_RUNNING && _scanState >= 0);
  if (!due && !changed) return;
  _polling = true;
  const wl_status_t old = _eventStatus; const wifi_mode_t oldMode = _eventMode;
  const uint8_t oldStations = _eventStations; const int16_t oldScan = _eventScanState;
  if (!due || refresh(true)) {
    WiFiEventInfo_t info; info.ip = _ip;
    if (!(oldMode & WIFI_STA) && (_mode & WIFI_STA)) dispatch(ARDUINO_EVENT_WIFI_STA_START, info);
    if ((oldMode & WIFI_STA) && !(_mode & WIFI_STA)) dispatch(ARDUINO_EVENT_WIFI_STA_STOP, info);
    if (old != WL_CONNECTED && _status == WL_CONNECTED) { dispatch(ARDUINO_EVENT_WIFI_STA_CONNECTED, info); dispatch(ARDUINO_EVENT_WIFI_STA_GOT_IP, info); }
    if (old == WL_CONNECTED && _status != WL_CONNECTED) { dispatch(ARDUINO_EVENT_WIFI_STA_DISCONNECTED, info); dispatch(ARDUINO_EVENT_WIFI_STA_LOST_IP, info); }
    if (!(oldMode & WIFI_AP) && (_mode & WIFI_AP)) dispatch(ARDUINO_EVENT_WIFI_AP_START, info);
    if ((oldMode & WIFI_AP) && !(_mode & WIFI_AP)) dispatch(ARDUINO_EVENT_WIFI_AP_STOP, info);
    if ((_mode & WIFI_AP) && refreshAP()) {
      if (_stations > oldStations) dispatch(ARDUINO_EVENT_WIFI_AP_STACONNECTED, info);
      if (_stations < oldStations) dispatch(ARDUINO_EVENT_WIFI_AP_STADISCONNECTED, info);
    }
    if (_scanState == WIFI_SCAN_RUNNING) scanComplete();
    if (oldScan == WIFI_SCAN_RUNNING && _scanState >= 0) dispatch(ARDUINO_EVENT_WIFI_SCAN_DONE, info);
  }
  _eventStatus = _status; _eventMode = _mode; _eventStations = _stations; _eventScanState = _scanState;
  _polling = false;
}
uint16_t WiFiClass::reasonCode() { _error = c3::Unsupported; return 0; }
void WiFiClass::end() { if (ESPLink.ready()) mode(WIFI_OFF); _scan.clear(); }
#if defined(ARDUINO_ARCH_CI13XX)
extern "C" void chipintelli_wifi_poll_hook() { WiFi.poll(); }
#endif
