#include "WiFiServer.h"
#include "WiFiConnection.h"
#include <vector>
struct ESPWiFiListener {
  uint32_t handle = 0, session = 0;
  std::vector<WiFiClient> clients;
  ~ESPWiFiListener() { espwifi::close(handle, session); }
};
WiFiServer::WiFiServer(uint16_t port, uint8_t count) : _port(port), _maxClients(count), _noDelay(false), _error(c3::Ok) {}
WiFiServer::WiFiServer(IPAddress address, uint16_t port, uint8_t count) : WiFiServer(port, count) { _address = address; }
WiFiServer::~WiFiServer() = default;
WiFiServer::operator bool() const { return _listener && _listener->handle && espwifi::current(_listener->session); }
void WiFiServer::begin(uint16_t port) { _port = port; begin(); }
void WiFiServer::begin() {
  if (operator bool()) return;
  if (espwifi::isIPv6(_address) || uint32_t(_address) != 0) { _error = c3::Unsupported; return; }
  if (!_port || !_maxClients || _maxClients > 8) { _error = c3::InvalidArgument; return; }
  if (!ESPLink.ready() && !ESPLink.begin()) { _error = c3::LinkLost; return; }
  espwifi::SharedPtr<ESPWiFiListener> listener(new (std::nothrow) ESPWiFiListener);
  if (!listener) { _error = c3::NoMemory; return; }
  listener->session = ESPLink.session();
  uint8_t b[7], reply[4]; b[0] = static_cast<uint8_t>(c3::SocketKind::Server); size_t n = sizeof(reply);
  _error = espwifi::call(c3::Opcode::SocketOpen, b, 1, reply, n);
  if (_error != c3::Ok) return;
  c3::Reader r(reply, n); listener->handle = r.u32();
  if (!r.done() || !listener->handle) { _error = c3::ProtocolError; return; }
  c3::Writer w(b, sizeof(b)); w.u32(listener->handle); w.u16(_port); w.u8(_maxClients);
  _error = espwifi::command(c3::Opcode::SocketListen, b, w.size());
  if (_error == c3::Ok) _listener = listener;
}
WiFiClient WiFiServer::accept() {
  if (!operator bool()) { _error = c3::NotConnected; return WiFiClient(); }
  uint8_t b[4], reply[4]; c3::put32(b, _listener->handle); size_t n = sizeof(reply);
  _error = espwifi::call(c3::Opcode::SocketAccept, b, sizeof(b), reply, n);
  if (_error != c3::Ok) return WiFiClient();
  c3::Reader r(reply, n); const uint32_t handle = r.u32();
  if (!r.done()) { _error = c3::ProtocolError; return WiFiClient(); }
  if (!handle) return WiFiClient();
  WiFiClient child(handle, _listener->session);
  if (child.valid()) {
    child.availableForWrite(); // Fetch peer address and ports once.
    if (child.lastError() != c3::Ok) {
      _error = child.lastError(); child.stop(); return WiFiClient();
    }
    if (_noDelay) child.setNoDelay(true);
  }
  return child;
}
WiFiClient WiFiServer::available() {
  if (!operator bool()) { _error = c3::NotConnected; return WiFiClient(); }
  auto &clients = _listener->clients;
  for (auto it = clients.begin(); it != clients.end();) {
    if (it->available() > 0) return *it;
    if (!it->connected()) it = clients.erase(it); else ++it;
  }
  if (clients.size() < _maxClients) {
    auto child = accept();
    if (child.valid()) {
      clients.push_back(child);
      if (child.available() > 0) return child;
    }
  }
  return WiFiClient();
}
void WiFiServer::end() {
  if (!_listener) return;
  espwifi::close(_listener->handle, _listener->session); _listener->handle = 0;
  for (auto &client : _listener->clients) client.stop();
  _listener->clients.clear();
}
bool WiFiServer::setNoDelay(bool enabled) {
  if (_listener) for (auto &client : _listener->clients) if (!client.setNoDelay(enabled)) { _error = client.lastError(); return false; }
  _noDelay = enabled; return true;
}
size_t WiFiServer::write(uint8_t byte) { return write(&byte, 1); }
size_t WiFiServer::write(const uint8_t *data, size_t length) {
  if (!data && length) { _error = c3::InvalidArgument; return 0; }
  if (!operator bool() || _listener->clients.empty()) { _error = c3::NotConnected; return 0; }
  size_t accepted = length;
  for (auto &client : _listener->clients) {
    const size_t n = client.write(data, length); accepted = std::min(accepted, n);
    if (n < length) _error = client.lastError();
  }
  return accepted;
}
