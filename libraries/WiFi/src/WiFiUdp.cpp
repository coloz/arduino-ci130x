#include "WiFiUdp.h"
#include "WiFiTransport.h"
struct ESPWiFiDatagram {
  uint32_t handle = 0, session = 0;
  uint8_t rx[768]; size_t head = 0, tail = 0, remaining = 0, txCount = 0;
  bool txActive = false, txFailed = false;
  IPAddress peer; uint16_t peerPort = 0, localPort = 0;
  ~ESPWiFiDatagram() { espwifi::close(handle, session); }
  size_t buffered() const { return tail - head; }
};
static int32_t udpFetch(ESPWiFiDatagram &s) {
  if (s.buffered() || !s.remaining) return c3::Ok;
  uint8_t b[6], reply[770]; c3::Writer w(b, sizeof(b));
  w.u32(s.handle); w.u16(std::min(s.remaining, sizeof(s.rx)));
  size_t n = sizeof(reply); const int32_t error = espwifi::call(c3::Opcode::UdpRead, b, w.size(), reply, n);
  if (error != c3::Ok) return error;
  c3::Reader r(reply, n); const size_t count = r.u16();
  if (!count || count > sizeof(s.rx) || count > s.remaining || r.remaining() != count) return c3::ProtocolError;
  r.bytes(s.rx, count); s.head = 0; s.tail = count; return c3::Ok;
}
WiFiUDP::WiFiUDP() : _error(c3::Ok) {}
WiFiUDP::~WiFiUDP() = default;
bool WiFiUDP::valid() const {
  if (!_state || !_state->handle) { _error = c3::NotConnected; return false; }
  if (!espwifi::current(_state->session)) {
    _state->handle = 0; _state->head = _state->tail = _state->remaining = 0;
    _state->txActive = false; _error = c3::StaleHandle; return false;
  }
  return true;
}
uint8_t WiFiUDP::open(IPAddress address, uint16_t port, bool multicast) {
  stop();
  if (!ESPLink.ready() && !ESPLink.begin()) { _error = c3::LinkLost; return 0; }
  espwifi::SharedPtr<ESPWiFiDatagram> state(new (std::nothrow) ESPWiFiDatagram);
  if (!state) { _error = c3::NoMemory; return 0; }
  state->session = ESPLink.session();
  uint8_t b[24], reply[4]; b[0] = static_cast<uint8_t>(c3::SocketKind::UDP); size_t n = sizeof(reply);
  _error = espwifi::call(c3::Opcode::SocketOpen, b, 1, reply, n);
  if (_error != c3::Ok) return 0;
  c3::Reader r(reply, n); state->handle = r.u32();
  if (!r.done() || !state->handle) { _error = c3::ProtocolError; return 0; }
  c3::Writer w(b, sizeof(b)); w.u32(state->handle); w.u16(port);
  if (multicast) espwifi::address(w, address); else { uint8_t any[18] = {}; w.bytes(any, sizeof(any)); }
  _error = espwifi::command(c3::Opcode::UdpBind, b, w.size());
  if (_error != c3::Ok) return 0;
  state->localPort = port; _state = state; return 1;
}
uint8_t WiFiUDP::begin(uint16_t port) { return open(IPAddress(), port, false); }
uint8_t WiFiUDP::begin(IPAddress address, uint16_t port) {
  if (espwifi::isIPv6(address) || uint32_t(address) != 0) { _error = c3::Unsupported; return 0; }
  return begin(port);
}
uint8_t WiFiUDP::beginMulticast(IPAddress address, uint16_t port) { return open(address, port, true); }
void WiFiUDP::stop() {
  if (!_state) return;
  espwifi::close(_state->handle, _state->session); _state->handle = 0;
  _state->remaining = _state->head = _state->tail = 0; _state->txActive = false;
}
int WiFiUDP::beginPacket(IPAddress address, uint16_t port) { return beginPacket(espwifi::addressString(address).c_str(), port); }
int WiFiUDP::beginPacket(const char *host, uint16_t port) {
  if (!host || !*host || strlen(host) > 253 || !port) { _error = c3::InvalidArgument; return 0; }
  if (!valid() && !begin(0)) return 0;
  uint8_t b[264]; c3::Writer w(b, sizeof(b)); w.u32(_state->handle); w.string(host); w.u16(port);
  _error = espwifi::command(c3::Opcode::UdpTxBegin, b, w.size(), 13000);
  _state->txActive = _error == c3::Ok; _state->txFailed = false; _state->txCount = 0;
  return _state->txActive;
}
size_t WiFiUDP::write(uint8_t byte) { return write(&byte, 1); }
size_t WiFiUDP::write(const uint8_t *data, size_t length) {
  if (!length) return 0;
  if (!valid()) return 0;
  if (!data || !_state->txActive || _state->txFailed) { _error = c3::InvalidArgument; return 0; }
  if (length > 1472 - _state->txCount) { _error = c3::BufferTooSmall; _state->txFailed = true; setWriteError(); return 0; }
  size_t total = 0; uint8_t b[772], reply[2];
  while (total < length) {
    const size_t count = std::min(length - total, sizeof(b) - 4);
    c3::Writer w(b, sizeof(b)); w.u32(_state->handle); w.bytes(data + total, count);
    size_t n = sizeof(reply); _error = espwifi::call(c3::Opcode::UdpTxData, b, w.size(), reply, n);
    if (_error != c3::Ok) { _state->txFailed = true; setWriteError(); break; }
    c3::Reader r(reply, n); const size_t accepted = r.u16();
    if (!r.done() || accepted > count) { _error = c3::ProtocolError; _state->txFailed = true; setWriteError(); break; }
    total += accepted; _state->txCount += accepted;
    if (accepted != count) { _error = c3::WouldBlock; _state->txFailed = true; break; }
  }
  return total;
}
int WiFiUDP::endPacket() {
  if (!valid()) return 0;
  if (!_state->txActive || _state->txFailed) { _error = c3::InvalidArgument; return 0; }
  uint8_t b[4]; c3::put32(b, _state->handle);
  _error = espwifi::command(c3::Opcode::UdpTxEnd, b, sizeof(b)); _state->txActive = false;
  return _error == c3::Ok;
}
int WiFiUDP::parsePacket() {
  if (!valid()) return 0;
  flush();
  if (_state->remaining) return 0;
  uint8_t b[4], reply[22]; c3::put32(b, _state->handle); size_t n = sizeof(reply);
  _error = espwifi::call(c3::Opcode::UdpRxBegin, b, sizeof(b), reply, n);
  if (_error != c3::Ok) return 0;
  c3::Reader r(reply, n); const size_t count = r.u16(); const auto peer = espwifi::address(r, _error); const uint16_t port = r.u16();
  if (!r.done() || count > 1472) { _error = c3::ProtocolError; return 0; }
  if (_error != c3::Ok) return 0;
  _state->remaining = count; _state->peer = peer; _state->peerPort = port;
  return count;
}
int WiFiUDP::available() { return valid() ? static_cast<int>(_state->remaining) : 0; }
int WiFiUDP::read() { uint8_t byte; return read(&byte, 1) == 1 ? byte : -1; }
int WiFiUDP::read(uint8_t *data, size_t length) {
  if (!valid()) return -1;
  if (!data && length) { _error = c3::InvalidArgument; return -1; }
  size_t total = 0;
  while (total < length && _state->remaining) {
    _error = udpFetch(*_state); if (_error != c3::Ok) break;
    const size_t count = std::min(length - total, _state->buffered());
    memcpy(data + total, _state->rx + _state->head, count);
    total += count; _state->head += count; _state->remaining -= count;
    if (_state->head == _state->tail) _state->head = _state->tail = 0;
  }
  return total;
}
int WiFiUDP::peek() {
  if (!valid() || !_state->remaining) return -1;
  _error = udpFetch(*_state);
  return _error == c3::Ok && _state->buffered() ? _state->rx[_state->head] : -1;
}
void WiFiUDP::flush() {
  if (!valid()) return;
  while (_state->remaining) {
    if (!_state->buffered()) { _error = udpFetch(*_state); if (_error != c3::Ok) return; }
    _state->remaining -= _state->buffered(); _state->head = _state->tail = 0;
  }
}
IPAddress WiFiUDP::remoteIP() { return valid() ? _state->peer : IPAddress(); }
uint16_t WiFiUDP::remotePort() { return valid() ? _state->peerPort : 0; }
uint16_t WiFiUDP::localPort() const { return valid() ? _state->localPort : 0; }
int32_t WiFiUDP::lastError() const { return _error; }
