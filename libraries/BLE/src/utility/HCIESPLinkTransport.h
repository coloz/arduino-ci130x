// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (c) 2026 ChipIntelli Arduino contributors.
#pragma once
#include "HCITransport.h"

enum BLEESPLinkError : int32_t {
  BLE_ESPLINK_OK = 0,
  BLE_ESPLINK_NOT_STARTED = -2000,
  BLE_ESPLINK_SESSION_CHANGED = -2001,
  BLE_ESPLINK_BAD_REPLY = -2002,
  BLE_ESPLINK_CONTROLLER_FAULT = -2003,
  BLE_ESPLINK_INVALID_PACKET = -2004,
  BLE_ESPLINK_CREDIT_TIMEOUT = -2005
};

class HCIESPLinkTransportClass : public HCITransportInterface {
public:
  int begin() override;
  void end() override;
  void wait(unsigned long timeout) override;
  int available() override;
  int peek() override;
  int read() override;
  size_t write(const uint8_t* data, size_t length) override;
  bool healthy() override;
  int32_t lastError() const { return _error; }
  uint32_t session() const { return _session; }
  uint32_t controllerDropped() const { return _controllerDropped; }
  uint32_t queuedBytes() const { return _queued; }
  void fail(int32_t error);

private:
  bool readStatus();
  bool checkSession();
  bool _started = false;
  int32_t _error = BLE_ESPLINK_NOT_STARTED;
  uint32_t _session = 0;
  uint32_t _lastFetch = 0;
  uint32_t _lastStatus = 0;
  uint32_t _controllerDropped = 0;
  uint32_t _queued = 0;
  uint16_t _maxPacket = 0;
  uint16_t _position = 0;
  uint16_t _count = 0;
  uint8_t _buffer[256] = {};
};

// Use lastError()/healthy() to distinguish a disconnected UART controller from
// the normal absence of a BLE peer. A failed session requires BLE.end()/begin().
extern HCIESPLinkTransportClass BLEESPLink;
