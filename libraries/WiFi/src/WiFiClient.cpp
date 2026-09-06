#include "WiFiClient.h"
#include "WiFiConnection.h"

static int32_t socketStatus(ESPWiFiConnection &s, uint32_t *writable = nullptr) {
  uint8_t request[4], reply[49]; c3::put32(request, s.handle);
  size_t n = sizeof(reply);
  const int32_t error = espwifi::call(c3::Opcode::SocketStatus, request, sizeof(request), reply, n);
  if (error != c3::Ok) return error;
  c3::Reader r(reply, n);
  const bool open = r.u8(); const uint32_t available = r.u32(), space = r.u32();
  int32_t addressError = c3::Ok;
  const IPAddress peer = espwifi::address(r, addressError);
  const uint16_t peerPort = r.u16(), localPort = r.u16();
  const IPAddress local = espwifi::address(r, addressError);
  if (!r.done()) return c3::ProtocolError;
  if (addressError != c3::Ok) return addressError;
  s.open = open; s.remoteAvailable = available; s.peer = peer; s.local = local;
  s.peerPort = peerPort; s.localPort = localPort; s.checkedAt = millis();
  if (writable) *writable = space;
  return c3::Ok;
}

int WiFiClient::connectTransport(const char *host, uint16_t port, uint32_t timeout,
 uint8_t kind, uint32_t ca, uint32_t cert, uint32_t key, bool insecure, uint32_t handshakeTimeout) {
  // Ownership of uploaded credential handles transfers here, including failure paths.
  espwifi::SharedPtr<ESPWiFiConnection> state(new (std::nothrow) ESPWiFiConnection(0, ESPLink.session()));
  if (!state) {
    for (auto handle : {ca, cert, key}) if (handle && ESPLink.ready()) {
      uint8_t b[4]; c3::put32(b, handle); espwifi::command(c3::Opcode::CertDelete, b, 4);
    }
    _error = c3::NoMemory; return 0;
  }
  state->certificates[0] = ca; state->certificates[1] = cert; state->certificates[2] = key;
  if (!host || !*host || strlen(host) > 253 || !port) { _error = c3::InvalidArgument; return 0; }
  if (!ESPLink.ready() && !ESPLink.begin()) { _error = c3::LinkLost; return 0; }
  state->session = ESPLink.session();
  uint8_t request[300], reply[8]; size_t n = sizeof(reply);
  request[0] = kind;
  _error = espwifi::call(c3::Opcode::SocketOpen, request, 1, reply, n);
  if (_error != c3::Ok) return 0;
  c3::Reader opened(reply, n); state->handle = opened.u32();
  if (!opened.done() || !state->handle) { _error = c3::ProtocolError; return 0; }
  if (kind == static_cast<uint8_t>(c3::SocketKind::TLS)) {
    c3::Writer w(request, sizeof(request));
    w.u32(state->handle); w.u32(ca); w.u32(cert); w.u32(key); w.u8(insecure); w.u32(handshakeTimeout);
    _error = espwifi::command(c3::Opcode::SocketTLS, request, w.size());
    if (_error != c3::Ok) return 0;
  }
  c3::Writer w(request, sizeof(request));
  w.u32(state->handle); w.string(host); w.u16(port); w.u32(timeout);
  const uint32_t rpcBudget = 10000 + timeout + handshakeTimeout + 3000;
  _error = espwifi::command(c3::Opcode::SocketConnect, request, w.size(), rpcBudget);
  if (_error != c3::Ok) return 0;
  _error = socketStatus(*state);
  if (_error != c3::Ok) return 0;
  _state = state; return 1;
}

int WiFiClient::fetch() {
  if (!valid()) return -1;
  if (_state->buffered()) return static_cast<int>(_state->buffered());
  uint8_t request[6], reply[770]; c3::Writer w(request, sizeof(request));
  w.u32(_state->handle); w.u16(sizeof(_state->rx));
  size_t n = sizeof(reply);
  _error = espwifi::call(c3::Opcode::SocketRead, request, w.size(), reply, n);
  if (_error == c3::WouldBlock) return 0;
  if (_error == c3::NotConnected) { _state->open = false; return 0; }
  if (_error != c3::Ok) return -1;
  c3::Reader r(reply, n); const size_t count = r.u16();
  if (count > sizeof(_state->rx) || r.remaining() != count) { _error = c3::ProtocolError; return -1; }
  r.bytes(_state->rx, count);
  if (!r.done()) { _error = c3::ProtocolError; return -1; }
  _state->head = 0; _state->tail = count;
  return static_cast<int>(count);
}

size_t WiFiClient::write(const uint8_t *data, size_t length) {
  if (!length) return 0;
  if (!data) { _error = c3::InvalidArgument; return 0; }
  if (!valid()) return 0;
  size_t total = 0;
  uint8_t request[772], reply[4];
  while (total < length) {
    const size_t count = std::min(length - total, sizeof(request) - 4);
    c3::Writer w(request, sizeof(request)); w.u32(_state->handle); w.bytes(data + total, count);
    size_t n = sizeof(reply);
    _error = espwifi::call(c3::Opcode::SocketWrite, request, w.size(), reply, n, std::max<uint32_t>(_timeout, 1000) + 3000);
    if (_error != c3::Ok) { setWriteError(); break; }
    c3::Reader r(reply, n); const uint32_t accepted = r.u32();
    if (!r.done() || accepted > count) { _error = c3::ProtocolError; setWriteError(); break; }
    total += accepted;
    // Never replay an already accepted prefix. The application receives the exact count.
    if (accepted < count) { _error = c3::WouldBlock; break; }
  }
  return total;
}
int WiFiClient::availableForWrite() {
  if (!valid()) return 0;
  uint32_t space = 0; _error = socketStatus(*_state, &space);
  return _error == c3::Ok ? static_cast<int>(std::min<uint32_t>(space, 0x7fffffffU)) : 0;
}
uint8_t WiFiClient::connected() {
  if (!valid()) return 0;
  if (_state->buffered()) return 1;
  _error = socketStatus(*_state);
  return _error == c3::Ok && (_state->open || _state->remoteAvailable);
}
void WiFiClient::flush() {
  // write() is synchronous through remote acceptance. There is no pending local TX.
  // Neither TCP peer acknowledgement nor peer application processing is implied.
  if (!valid()) return;
}
void WiFiClient::clear() {
  if (!valid()) return;
  uint8_t b[4]; c3::put32(b, _state->handle);
  _error = espwifi::command(c3::Opcode::SocketClearRx, b, sizeof(b));
  if (_error == c3::Ok) _state->head = _state->tail = 0;
}
int WiFiClient::setNoDelay(bool enabled) {
  if (!valid()) return 0;
  uint8_t b[9]; c3::Writer w(b, sizeof(b));
  w.u32(_state->handle); w.u8(static_cast<uint8_t>(c3::SocketOption::NoDelay)); w.u32(enabled);
  _error = espwifi::command(c3::Opcode::SocketOption, b, w.size());
  if (_error == c3::Ok) _state->noDelay = enabled;
  return _error == c3::Ok;
}
bool WiFiClient::getNoDelay() { return valid() && _state->noDelay; }
bool WiFiClient::setKeepAlive(uint32_t idle, uint32_t interval, uint32_t count) {
  if (!valid()) return false;
  if (!idle || !interval || !count) { _error = c3::InvalidArgument; return false; }
  const uint32_t values[] = {idle, interval, count};
  for (uint8_t i = 0; i < 3; ++i) {
    uint8_t b[9]; c3::Writer w(b, sizeof(b));
    w.u32(_state->handle); w.u8(static_cast<uint8_t>(c3::SocketOption::KeepAliveIdle) + i); w.u32(values[i]);
    _error = espwifi::command(c3::Opcode::SocketOption, b, w.size());
    if (_error != c3::Ok) return false;
  }
  return true;
}
