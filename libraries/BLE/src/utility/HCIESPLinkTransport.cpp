// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (c) 2026 ChipIntelli Arduino contributors.
#include "BLEConfig.h"
#if BLE_USE_ESPLINK
#include "HCIESPLinkTransport.h"
#include <ESPLink.h>
#include <C3Protocol.h>

namespace {
uint16_t le16(const uint8_t* p) {
  return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}
uint32_t le32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
         (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
constexpr uint32_t rpcTimeout = 2000;
}

void HCIESPLinkTransportClass::fail(int32_t error) {
  _error = error ? error : BLE_ESPLINK_CONTROLLER_FAULT;
  _position = _count = 0;
}

bool HCIESPLinkTransportClass::checkSession() {
  if (!_started || _error != BLE_ESPLINK_OK) return false;
  ESPLink.poll();
  if (!ESPLink.ready() || ESPLink.session() != _session) {
    fail(BLE_ESPLINK_SESSION_CHANGED);
    return false;
  }
  return true;
}

bool HCIESPLinkTransportClass::healthy() {
  return checkSession();
}

bool HCIESPLinkTransportClass::readStatus() {
  uint8_t reply[12];
  size_t size = sizeof(reply);
  const int32_t result = ESPLink.request(static_cast<uint16_t>(c3::Opcode::HciStatus), nullptr, 0, reply, size, rpcTimeout);
  if (result != 0) { fail(result); return false; }
  if (size != sizeof(reply)) { fail(BLE_ESPLINK_BAD_REPLY); return false; }
  _queued = le32(reply + 1);
  _controllerDropped = le32(reply + 5);
  _maxPacket = le16(reply + 9);
  _lastStatus = millis();
  if (!reply[0] || reply[11] || _controllerDropped || _maxPacket < 258) {
    fail(BLE_ESPLINK_CONTROLLER_FAULT);
    return false;
  }
  return true;
}

int HCIESPLinkTransportClass::begin() {
  if (checkSession()) return 1;
  _started = false;
  _position = _count = 0;
  _error = BLE_ESPLINK_OK;
  _controllerDropped = _queued = 0;
  // Start the default UART on first use, or reuse the application's explicit
  // ESPLink.begin(uart, baud) binding without changing its configuration.
  if (!ESPLink.ready() && !ESPLink.begin()) {
    fail(BLE_ESPLINK_NOT_STARTED);
    return 0;
  }
  _session = ESPLink.session();
  size_t size = 0;
  const int32_t result = ESPLink.request(static_cast<uint16_t>(c3::Opcode::HciBegin), nullptr, 0, nullptr, size, 5000);
  if (result != 0) { fail(result); return 0; }
  _started = true;
  if (size != 0) { fail(BLE_ESPLINK_BAD_REPLY); return 0; }
  if (!readStatus() || !checkSession()) return 0;
  _lastFetch = millis() - 1;
  return 1;
}

void HCIESPLinkTransportClass::end() {
  // A different session must never receive a stale controller shutdown.
  if (_started && ESPLink.ready() && ESPLink.session() == _session) {
    size_t size = 0;
    const int32_t result = ESPLink.request(static_cast<uint16_t>(c3::Opcode::HciEnd), nullptr, 0, nullptr, size, rpcTimeout);
    if (result != 0) _error = result;
    else if (size != 0) _error = BLE_ESPLINK_BAD_REPLY;
  }
  _started = false;
  _position = _count = 0;
  _queued = 0;
}

int HCIESPLinkTransportClass::available() {
  if (!checkSession()) return 0;
  if (_position < _count) return _count - _position;
  if (uint32_t(millis() - _lastStatus) >= 1000 && !readStatus()) return 0;
  if (!checkSession()) return 0;
  if (uint32_t(millis() - _lastFetch) < 1) return 0;
  _lastFetch = millis();
  const uint8_t request[2] = {uint8_t(sizeof(_buffer) & 0xff), uint8_t(sizeof(_buffer) >> 8)};
  size_t size = sizeof(_buffer);
  const int32_t result = ESPLink.request(static_cast<uint16_t>(c3::Opcode::HciRead), request, sizeof(request),
                                       _buffer, size, rpcTimeout);
  if (result != 0) { fail(result); return 0; }
  if (!checkSession()) return 0;
  if (size > sizeof(_buffer)) { fail(BLE_ESPLINK_BAD_REPLY); return 0; }
  _position = 0;
  _count = uint16_t(size);
  return _count;
}

int HCIESPLinkTransportClass::peek() {
  return available() ? _buffer[_position] : -1;
}

int HCIESPLinkTransportClass::read() {
  return available() ? _buffer[_position++] : -1;
}

void HCIESPLinkTransportClass::wait(unsigned long timeout) {
  const uint32_t started = millis();
  while (healthy() && !available() && uint32_t(millis() - started) < timeout) {
    delay(1);
  }
}

size_t HCIESPLinkTransportClass::write(const uint8_t* data, size_t length) {
  if (!checkSession()) return 0;
  // ArduinoBLE writes a complete H4 command or ACL packet in one call.
  // No HCI retry belongs here: ESPLink owns retry/deduplication and treats an
  // exhausted RPC as an uncertain outcome, invalidating its session.
  if (!data || length < 4 || length > _maxPacket ||
      !((data[0] == 0x01 && length == size_t(4) + data[3]) ||
        (data[0] == 0x02 && length >= 5 && length == size_t(5) + le16(data + 3)))) {
    fail(BLE_ESPLINK_INVALID_PACKET);
    return 0;
  }
  uint8_t reply[2];
  size_t size = sizeof(reply);
  const int32_t result = ESPLink.request(static_cast<uint16_t>(c3::Opcode::HciWrite), data, length,
                                       reply, size, rpcTimeout);
  if (result != 0) { fail(result); return 0; }
  if (!checkSession()) return 0;
  if (size != sizeof(reply) || le16(reply) != length) {
    fail(BLE_ESPLINK_BAD_REPLY);
    return 0;
  }
  return length;
}

HCIESPLinkTransportClass BLEESPLink;
HCITransportInterface& HCITransport = BLEESPLink;
#endif
