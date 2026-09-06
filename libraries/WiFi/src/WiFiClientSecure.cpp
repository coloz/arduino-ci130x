#include "WiFiClientSecure.h"
#include "WiFiTransport.h"
#include <vector>

struct ESPWiFiCredentials {
  const char *pem[3] = {};
  std::vector<char> owned[3];
};
static void deleteCertificate(uint32_t handle, uint32_t session) {
  if (!handle || !espwifi::current(session)) return;
  uint8_t b[4]; c3::put32(b, handle);
  espwifi::command(c3::Opcode::CertDelete, b, sizeof(b));
}
static int32_t uploadCertificate(uint8_t kind, const char *pem, uint32_t &handle) {
  handle = 0;
  if (!pem || !*pem) return c3::Ok;
  size_t total = 0;
  while (total <= 8192 && pem[total]) ++total;
  if (total > 8192) return c3::BufferTooSmall;
  uint8_t request[776], reply[4];
  c3::Writer begin(request, sizeof(request)); begin.u8(kind); begin.u32(total);
  size_t n = sizeof(reply);
  int32_t error = espwifi::call(c3::Opcode::CertBegin, request, begin.size(), reply, n);
  if (error != c3::Ok) return error;
  c3::Reader r(reply, n); handle = r.u32();
  if (!r.done() || !handle) return c3::ProtocolError;
  for (size_t offset = 0; offset < total;) {
    const size_t chunk = std::min(total - offset, sizeof(request) - 8);
    c3::Writer w(request, sizeof(request)); w.u32(handle); w.u32(offset); w.bytes(pem + offset, chunk);
    error = espwifi::command(c3::Opcode::CertWrite, request, w.size());
    if (error != c3::Ok) return error;
    offset += chunk;
  }
  c3::put32(request, handle);
  return espwifi::command(c3::Opcode::CertCommit, request, 4);
}
WiFiClientSecure::WiFiClientSecure() : _credentials(new (std::nothrow) ESPWiFiCredentials),
  _insecure(false), _handshakeTimeout(15000) {}
WiFiClientSecure::~WiFiClientSecure() = default;
void WiFiClientSecure::setCACert(const char *pem) {
  if (!_credentials) { _error = c3::NoMemory; return; }
  _credentials->owned[0].clear(); _credentials->pem[0] = pem; _insecure = false;
}
void WiFiClientSecure::setCertificate(const char *pem) {
  if (!_credentials) { _error = c3::NoMemory; return; }
  _credentials->owned[1].clear(); _credentials->pem[1] = pem;
}
void WiFiClientSecure::setPrivateKey(const char *pem) {
  if (!_credentials) { _error = c3::NoMemory; return; }
  _credentials->owned[2].clear(); _credentials->pem[2] = pem;
}
void WiFiClientSecure::setInsecure() { _insecure = true; }
void WiFiClientSecure::useBuiltinCACertBundle() { setCACert(nullptr); }
void WiFiClientSecure::setHandshakeTimeout(unsigned long seconds) {
  if (!seconds || seconds > 30) { _error = c3::InvalidArgument; return; }
  _handshakeTimeout = static_cast<uint32_t>(seconds) * 1000;
}
bool WiFiClientSecure::loadPEM(Stream &stream, size_t length, uint8_t kind) {
  if (!_credentials) { _error = c3::NoMemory; return false; }
  if (!length || length > 8192) { _error = c3::InvalidArgument; return false; }
  std::vector<char> buffer(length + 1, 0);
  if (stream.readBytes(buffer.data(), length) != length) { _error = c3::Timeout; return false; }
  // A trailing NUL is accepted; embedded NUL bytes are not valid PEM text.
  size_t textLength = length;
  if (buffer[textLength - 1] == 0) --textLength;
  if (memchr(buffer.data(), 0, textLength)) { _error = c3::InvalidArgument; return false; }
  _credentials->owned[kind].swap(buffer);
  _credentials->pem[kind] = _credentials->owned[kind].data();
  if (kind == 0) _insecure = false;
  _error = c3::Ok; return true;
}
bool WiFiClientSecure::loadCACert(Stream &s, size_t n) { return loadPEM(s, n, 0); }
bool WiFiClientSecure::loadCertificate(Stream &s, size_t n) { return loadPEM(s, n, 1); }
bool WiFiClientSecure::loadPrivateKey(Stream &s, size_t n) { return loadPEM(s, n, 2); }
int WiFiClientSecure::connect(IPAddress ip, uint16_t port) { return connect(espwifi::addressString(ip).c_str(), port); }
int WiFiClientSecure::connect(const char *host, uint16_t port) { return connect(host, port, _connectTimeout); }
int WiFiClientSecure::connect(IPAddress ip, uint16_t port, int32_t timeout) { return connect(espwifi::addressString(ip).c_str(), port, timeout); }
int WiFiClientSecure::connect(const char *host, uint16_t port, int32_t timeout) {
  if (!_credentials) { _error = c3::NoMemory; return 0; }
  if (timeout <= 0 || timeout > 30000 || !host || !*host || !port) { _error = c3::InvalidArgument; return 0; }
  if (!!_credentials->pem[1] != !!_credentials->pem[2]) { _error = c3::InvalidArgument; return 0; }
  if (!ESPLink.ready() && !ESPLink.begin()) { _error = c3::LinkLost; return 0; }
  uint32_t handles[3] = {}, session = ESPLink.session();
  for (uint8_t i = 0; i < 3; ++i) {
    if (i == 0 && _insecure) continue;
    _error = uploadCertificate(i, _credentials->pem[i], handles[i]);
    if (_error != c3::Ok) {
      for (auto h : handles) deleteCertificate(h, session);
      return 0;
    }
  }
  return connectTransport(host, port, timeout,
    static_cast<uint8_t>(c3::SocketKind::TLS), handles[0], handles[1], handles[2], _insecure, _handshakeTimeout);
}
int WiFiClientSecure::lastError(char *buffer, size_t length) const {
  if (buffer && length) snprintf(buffer, length, "ESPLink/TLS status %ld", static_cast<long>(_error));
  return _error;
}
