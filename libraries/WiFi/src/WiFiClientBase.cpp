#include "WiFiClient.h"
#include "WiFiConnection.h"
#include <string.h>

WiFiClient::WiFiClient() : _connectTimeout(10000), _error(c3::Ok) {}
WiFiClient::WiFiClient(uint32_t handle, uint32_t session) : WiFiClient() {
  auto *connection = new (std::nothrow) ESPWiFiConnection(handle, session);
  if (!connection) { espwifi::close(handle, session); _error = c3::NoMemory; return; }
  _state.reset(connection); // reset deletes connection (and closes it) if its control allocation fails.
  if (!_state) _error = c3::NoMemory;
}
WiFiClient::~WiFiClient() = default;
bool WiFiClient::valid() const {
  if (!_state) { _error = c3::NotConnected; return false; }
  if (!_state->valid()) { _error = _state->error; return false; }
  return true;
}
int32_t WiFiClient::lastError() const { return _error; }
bool WiFiClient::operator==(const WiFiClient &other) const { return _state == other._state; }
WiFiClient::operator bool() { return connected() != 0; }
int WiFiClient::connect(IPAddress ip, uint16_t port) { return connect(espwifi::addressString(ip).c_str(), port); }
int WiFiClient::connect(const char *host, uint16_t port) { return connect(host, port, _connectTimeout); }
int WiFiClient::connect(IPAddress ip, uint16_t port, int32_t timeout) { return connect(espwifi::addressString(ip).c_str(), port, timeout); }
int WiFiClient::connect(const char *host, uint16_t port, int32_t timeout) {
  if (timeout <= 0 || timeout > 30000) { _error = c3::InvalidArgument; return 0; }
  return connectTransport(host, port, timeout, static_cast<uint8_t>(c3::SocketKind::TCP), 0, 0, 0, false);
}
size_t WiFiClient::write(uint8_t data) { return write(&data, 1); }
int WiFiClient::read() {
  uint8_t value;
  return read(&value, 1) == 1 ? value : -1;
}
int WiFiClient::read(uint8_t *data, size_t length) {
  if (!length) return 0;
  if (!data) { _error = c3::InvalidArgument; return -1; }
  if (!valid()) return -1;
  if (!_state->buffered()) fetch();
  const size_t count = std::min(length, _state->buffered());
  if (count) memcpy(data, _state->rx + _state->head, count);
  _state->head += count;
  if (_state->head == _state->tail) _state->head = _state->tail = 0;
  return static_cast<int>(count);
}
int WiFiClient::peek() {
  if (!valid()) return -1;
  if (!_state->buffered()) fetch();
  return _state->buffered() ? _state->rx[_state->head] : -1;
}
int WiFiClient::available() {
  if (!valid()) return 0;
  if (!_state->buffered()) fetch();
  return static_cast<int>(_state->buffered());
}
bool WiFiClient::waitForData(unsigned long timeout) {
  const uint32_t start = millis();
  do {
    if (available() > 0) return true;
    if (!valid() || !_state->open) return false;
    delay(1);
  } while (static_cast<uint32_t>(millis() - start) < timeout);
  return false;
}
void WiFiClient::stop() {
  if (_state) { _state->release(); _error = c3::Ok; }
}
IPAddress WiFiClient::remoteIP() const { return valid() ? _state->peer : IPAddress(); }
uint16_t WiFiClient::remotePort() const { return valid() ? _state->peerPort : 0; }
IPAddress WiFiClient::localIP() const { return valid() ? _state->local : IPAddress(); }
uint16_t WiFiClient::localPort() const { return valid() ? _state->localPort : 0; }
